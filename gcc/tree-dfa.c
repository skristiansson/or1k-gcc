/* Data flow functions for trees.
   Copyright (C) 2001, 2002, 2003, 2004, 2005, 2007, 2008, 2009, 2010
   Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "hashtab.h"
#include "pointer-set.h"
#include "tree.h"
#include "tm_p.h"
#include "basic-block.h"
#include "timevar.h"
#include "ggc.h"
#include "langhooks.h"
#include "flags.h"
#include "function.h"
#include "tree-pretty-print.h"
#include "tree-dump.h"
#include "gimple.h"
#include "tree-flow.h"
#include "tree-inline.h"
#include "tree-pass.h"
#include "convert.h"
#include "params.h"
#include "cgraph.h"

/* Build and maintain data flow information for trees.  */

/* Counters used to display DFA and SSA statistics.  */
struct dfa_stats_d
{
  long num_var_anns;
  long num_defs;
  long num_uses;
  long num_phis;
  long num_phi_args;
  size_t max_num_phi_args;
  long num_vdefs;
  long num_vuses;
};


/* Local functions.  */
static void collect_dfa_stats (struct dfa_stats_d *);


/*---------------------------------------------------------------------------
			Dataflow analysis (DFA) routines
---------------------------------------------------------------------------*/
/* Find all the variables referenced in the function.  This function
   builds the global arrays REFERENCED_VARS and CALL_CLOBBERED_VARS.

   Note that this function does not look for statement operands, it simply
   determines what variables are referenced in the program and detects
   various attributes for each variable used by alias analysis and the
   optimizer.  */

static unsigned int
find_referenced_vars (void)
{
  basic_block bb;
  gimple_stmt_iterator si;

  FOR_EACH_BB (bb)
    {
      for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
	{
	  gimple stmt = gsi_stmt (si);
	  if (is_gimple_debug (stmt))
	    continue;
	  find_referenced_vars_in (gsi_stmt (si));
	}

      for (si = gsi_start_phis (bb); !gsi_end_p (si); gsi_next (&si))
	find_referenced_vars_in (gsi_stmt (si));
    }

  return 0;
}

struct gimple_opt_pass pass_referenced_vars =
{
 {
  GIMPLE_PASS,
  "*referenced_vars",			/* name */
  NULL,					/* gate */
  find_referenced_vars,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_FIND_REFERENCED_VARS,		/* tv_id */
  PROP_gimple_leh | PROP_cfg,		/* properties_required */
  PROP_referenced_vars,			/* properties_provided */
  0,					/* properties_destroyed */
  0,                     		/* todo_flags_start */
  0                                     /* todo_flags_finish */
 }
};


/* Renumber all of the gimple stmt uids.  */

void
renumber_gimple_stmt_uids (void)
{
  basic_block bb;

  set_gimple_stmt_max_uid (cfun, 0);
  FOR_ALL_BB (bb)
    {
      gimple_stmt_iterator bsi;
      for (bsi = gsi_start_phis (bb); !gsi_end_p (bsi); gsi_next (&bsi))
	{
	  gimple stmt = gsi_stmt (bsi);
	  gimple_set_uid (stmt, inc_gimple_stmt_max_uid (cfun));
	}
      for (bsi = gsi_start_bb (bb); !gsi_end_p (bsi); gsi_next (&bsi))
	{
	  gimple stmt = gsi_stmt (bsi);
	  gimple_set_uid (stmt, inc_gimple_stmt_max_uid (cfun));
	}
    }
}

/* Like renumber_gimple_stmt_uids, but only do work on the basic blocks
   in BLOCKS, of which there are N_BLOCKS.  Also renumbers PHIs.  */

void
renumber_gimple_stmt_uids_in_blocks (basic_block *blocks, int n_blocks)
{
  int i;

  set_gimple_stmt_max_uid (cfun, 0);
  for (i = 0; i < n_blocks; i++)
    {
      basic_block bb = blocks[i];
      gimple_stmt_iterator bsi;
      for (bsi = gsi_start_phis (bb); !gsi_end_p (bsi); gsi_next (&bsi))
	{
	  gimple stmt = gsi_stmt (bsi);
	  gimple_set_uid (stmt, inc_gimple_stmt_max_uid (cfun));
	}
      for (bsi = gsi_start_bb (bb); !gsi_end_p (bsi); gsi_next (&bsi))
	{
	  gimple stmt = gsi_stmt (bsi);
	  gimple_set_uid (stmt, inc_gimple_stmt_max_uid (cfun));
	}
    }
}

/* Build a temporary.  Make sure and register it to be renamed.  */

tree
make_rename_temp (tree type, const char *prefix)
{
  tree t = create_tmp_reg (type, prefix);

  if (gimple_referenced_vars (cfun))
    add_referenced_var (t);
  if (gimple_in_ssa_p (cfun))
    mark_sym_for_renaming (t);

  return t;
}



/*---------------------------------------------------------------------------
			      Debugging functions
---------------------------------------------------------------------------*/
/* Dump the list of all the referenced variables in the current function to
   FILE.  */

void
dump_referenced_vars (FILE *file)
{
  tree var;
  referenced_var_iterator rvi;

  fprintf (file, "\nReferenced variables in %s: %u\n\n",
	   get_name (current_function_decl), (unsigned) num_referenced_vars);

  FOR_EACH_REFERENCED_VAR (cfun, var, rvi)
    {
      fprintf (file, "Variable: ");
      dump_variable (file, var);
    }

  fprintf (file, "\n");
}


/* Dump the list of all the referenced variables to stderr.  */

DEBUG_FUNCTION void
debug_referenced_vars (void)
{
  dump_referenced_vars (stderr);
}


/* Dump variable VAR and its may-aliases to FILE.  */

void
dump_variable (FILE *file, tree var)
{
  if (TREE_CODE (var) == SSA_NAME)
    {
      if (POINTER_TYPE_P (TREE_TYPE (var)))
	dump_points_to_info_for (file, var);
      var = SSA_NAME_VAR (var);
    }

  if (var == NULL_TREE)
    {
      fprintf (file, "<nil>");
      return;
    }

  print_generic_expr (file, var, dump_flags);

  fprintf (file, ", UID D.%u", (unsigned) DECL_UID (var));
  if (DECL_PT_UID (var) != DECL_UID (var))
    fprintf (file, ", PT-UID D.%u", (unsigned) DECL_PT_UID (var));

  fprintf (file, ", ");
  print_generic_expr (file, TREE_TYPE (var), dump_flags);

  if (TREE_ADDRESSABLE (var))
    fprintf (file, ", is addressable");

  if (is_global_var (var))
    fprintf (file, ", is global");

  if (TREE_THIS_VOLATILE (var))
    fprintf (file, ", is volatile");

  if (cfun && gimple_default_def (cfun, var))
    {
      fprintf (file, ", default def: ");
      print_generic_expr (file, gimple_default_def (cfun, var), dump_flags);
    }

  if (DECL_INITIAL (var))
    {
      fprintf (file, ", initial: ");
      print_generic_expr (file, DECL_INITIAL (var), dump_flags);
    }

  fprintf (file, "\n");
}


/* Dump variable VAR and its may-aliases to stderr.  */

DEBUG_FUNCTION void
debug_variable (tree var)
{
  dump_variable (stderr, var);
}


/* Dump various DFA statistics to FILE.  */

void
dump_dfa_stats (FILE *file)
{
  struct dfa_stats_d dfa_stats;

  unsigned long size, total = 0;
  const char * const fmt_str   = "%-30s%-13s%12s\n";
  const char * const fmt_str_1 = "%-30s%13lu%11lu%c\n";
  const char * const fmt_str_3 = "%-43s%11lu%c\n";
  const char *funcname
    = lang_hooks.decl_printable_name (current_function_decl, 2);

  collect_dfa_stats (&dfa_stats);

  fprintf (file, "\nDFA Statistics for %s\n\n", funcname);

  fprintf (file, "---------------------------------------------------------\n");
  fprintf (file, fmt_str, "", "  Number of  ", "Memory");
  fprintf (file, fmt_str, "", "  instances  ", "used ");
  fprintf (file, "---------------------------------------------------------\n");

  size = num_referenced_vars * sizeof (tree);
  total += size;
  fprintf (file, fmt_str_1, "Referenced variables", (unsigned long)num_referenced_vars,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_var_anns * sizeof (struct var_ann_d);
  total += size;
  fprintf (file, fmt_str_1, "Variables annotated", dfa_stats.num_var_anns,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_uses * sizeof (tree *);
  total += size;
  fprintf (file, fmt_str_1, "USE operands", dfa_stats.num_uses,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_defs * sizeof (tree *);
  total += size;
  fprintf (file, fmt_str_1, "DEF operands", dfa_stats.num_defs,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_vuses * sizeof (tree *);
  total += size;
  fprintf (file, fmt_str_1, "VUSE operands", dfa_stats.num_vuses,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_vdefs * sizeof (tree *);
  total += size;
  fprintf (file, fmt_str_1, "VDEF operands", dfa_stats.num_vdefs,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_phis * sizeof (struct gimple_statement_phi);
  total += size;
  fprintf (file, fmt_str_1, "PHI nodes", dfa_stats.num_phis,
	   SCALE (size), LABEL (size));

  size = dfa_stats.num_phi_args * sizeof (struct phi_arg_d);
  total += size;
  fprintf (file, fmt_str_1, "PHI arguments", dfa_stats.num_phi_args,
 	   SCALE (size), LABEL (size));

  fprintf (file, "---------------------------------------------------------\n");
  fprintf (file, fmt_str_3, "Total memory used by DFA/SSA data", SCALE (total),
	   LABEL (total));
  fprintf (file, "---------------------------------------------------------\n");
  fprintf (file, "\n");

  if (dfa_stats.num_phis)
    fprintf (file, "Average number of arguments per PHI node: %.1f (max: %ld)\n",
	     (float) dfa_stats.num_phi_args / (float) dfa_stats.num_phis,
	     (long) dfa_stats.max_num_phi_args);

  fprintf (file, "\n");
}


/* Dump DFA statistics on stderr.  */

DEBUG_FUNCTION void
debug_dfa_stats (void)
{
  dump_dfa_stats (stderr);
}


/* Collect DFA statistics and store them in the structure pointed to by
   DFA_STATS_P.  */

static void
collect_dfa_stats (struct dfa_stats_d *dfa_stats_p ATTRIBUTE_UNUSED)
{
  basic_block bb;
  referenced_var_iterator vi;
  tree var;

  gcc_assert (dfa_stats_p);

  memset ((void *)dfa_stats_p, 0, sizeof (struct dfa_stats_d));

  /* Count all the variable annotations.  */
  FOR_EACH_REFERENCED_VAR (cfun, var, vi)
    if (var_ann (var))
      dfa_stats_p->num_var_anns++;

  /* Walk all the statements in the function counting references.  */
  FOR_EACH_BB (bb)
    {
      gimple_stmt_iterator si;

      for (si = gsi_start_phis (bb); !gsi_end_p (si); gsi_next (&si))
	{
	  gimple phi = gsi_stmt (si);
	  dfa_stats_p->num_phis++;
	  dfa_stats_p->num_phi_args += gimple_phi_num_args (phi);
	  if (gimple_phi_num_args (phi) > dfa_stats_p->max_num_phi_args)
	    dfa_stats_p->max_num_phi_args = gimple_phi_num_args (phi);
	}

      for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
	{
	  gimple stmt = gsi_stmt (si);
	  dfa_stats_p->num_defs += NUM_SSA_OPERANDS (stmt, SSA_OP_DEF);
	  dfa_stats_p->num_uses += NUM_SSA_OPERANDS (stmt, SSA_OP_USE);
	  dfa_stats_p->num_vdefs += gimple_vdef (stmt) ? 1 : 0;
	  dfa_stats_p->num_vuses += gimple_vuse (stmt) ? 1 : 0;
	}
    }
}


/*---------------------------------------------------------------------------
			     Miscellaneous helpers
---------------------------------------------------------------------------*/
/* Callback for walk_tree.  Used to collect variables referenced in
   the function.  */

static tree
find_vars_r (tree *tp, int *walk_subtrees, void *data)
{
  struct function *fn = (struct function *) data;

  /* If we are reading the lto info back in, we need to rescan the
     referenced vars.  */
  if (TREE_CODE (*tp) == SSA_NAME)
    add_referenced_var_1 (SSA_NAME_VAR (*tp), fn);

  /* If T is a regular variable that the optimizers are interested
     in, add it to the list of variables.  */
  else if ((TREE_CODE (*tp) == VAR_DECL
	    && !is_global_var (*tp))
	   || TREE_CODE (*tp) == PARM_DECL
	   || TREE_CODE (*tp) == RESULT_DECL)
    add_referenced_var_1 (*tp, fn);

  /* Type, _DECL and constant nodes have no interesting children.
     Ignore them.  */
  else if (IS_TYPE_OR_DECL_P (*tp) || CONSTANT_CLASS_P (*tp))
    *walk_subtrees = 0;

  return NULL_TREE;
}

/* Find referenced variables in STMT.  */

void
find_referenced_vars_in (gimple stmt)
{
  size_t i;

  if (gimple_code (stmt) != GIMPLE_PHI)
    {
      for (i = 0; i < gimple_num_ops (stmt); i++)
	walk_tree (gimple_op_ptr (stmt, i), find_vars_r, cfun, NULL);
    }
  else
    {
      walk_tree (gimple_phi_result_ptr (stmt), find_vars_r, cfun, NULL);

      for (i = 0; i < gimple_phi_num_args (stmt); i++)
	{
	  tree arg = gimple_phi_arg_def (stmt, i);
	  walk_tree (&arg, find_vars_r, cfun, NULL);
	}
    }
}


/* Lookup UID in the referenced_vars hashtable and return the associated
   variable.  */

tree
referenced_var_lookup (struct function *fn, unsigned int uid)
{
  tree h;
  struct tree_decl_minimal in;
  in.uid = uid;
  h = (tree) htab_find_with_hash (gimple_referenced_vars (fn), &in, uid);
  return h;
}

/* Check if TO is in the referenced_vars hash table and insert it if not.
   Return true if it required insertion.  */

static bool
referenced_var_check_and_insert (tree to, struct function *fn)
{
  tree *loc;
  struct tree_decl_minimal in;
  unsigned int uid = DECL_UID (to);

  in.uid = uid;
  loc = (tree *) htab_find_slot_with_hash (gimple_referenced_vars (fn),
					   &in, uid, INSERT);
  if (*loc)
    {
      /* DECL_UID has already been entered in the table.  Verify that it is
	 the same entry as TO.  See PR 27793.  */
      gcc_assert (*loc == to);
      return false;
    }

  *loc = to;
  return true;
}

/* Lookup VAR UID in the default_defs hashtable and return the associated
   variable.  */

tree
gimple_default_def (struct function *fn, tree var)
{
  struct tree_decl_minimal ind;
  struct tree_ssa_name in;
  gcc_assert (SSA_VAR_P (var));
  in.var = (tree)&ind;
  ind.uid = DECL_UID (var);
  return (tree) htab_find_with_hash (DEFAULT_DEFS (fn), &in, DECL_UID (var));
}

/* Insert the pair VAR's UID, DEF into the default_defs hashtable.  */

void
set_default_def (tree var, tree def)
{
  struct tree_decl_minimal ind;
  struct tree_ssa_name in;
  void **loc;

  gcc_assert (SSA_VAR_P (var));
  in.var = (tree)&ind;
  ind.uid = DECL_UID (var);
  if (!def)
    {
      loc = htab_find_slot_with_hash (DEFAULT_DEFS (cfun), &in,
            DECL_UID (var), INSERT);
      gcc_assert (*loc);
      htab_remove_elt (DEFAULT_DEFS (cfun), *loc);
      return;
    }
  gcc_assert (TREE_CODE (def) == SSA_NAME && SSA_NAME_VAR (def) == var);
  loc = htab_find_slot_with_hash (DEFAULT_DEFS (cfun), &in,
                                  DECL_UID (var), INSERT);

  /* Default definition might be changed by tail call optimization.  */
  if (*loc)
    SSA_NAME_IS_DEFAULT_DEF (*(tree *) loc) = false;
  *(tree *) loc = def;

   /* Mark DEF as the default definition for VAR.  */
   SSA_NAME_IS_DEFAULT_DEF (def) = true;
}

/* Add VAR to the list of referenced variables if it isn't already there.  */

bool
add_referenced_var_1 (tree var, struct function *fn)
{
  gcc_checking_assert (TREE_CODE (var) == VAR_DECL
		       || TREE_CODE (var) == PARM_DECL
		       || TREE_CODE (var) == RESULT_DECL);

  gcc_checking_assert ((TREE_CODE (var) == VAR_DECL
			&& VAR_DECL_IS_VIRTUAL_OPERAND (var))
		       || !is_global_var (var));

  /* Insert VAR into the referenced_vars hash table if it isn't present
     and allocate its var-annotation.  */
  if (referenced_var_check_and_insert (var, fn))
    {
      gcc_checking_assert (!*DECL_VAR_ANN_PTR (var));
      *DECL_VAR_ANN_PTR (var) = ggc_alloc_cleared_var_ann_d ();
      return true;
    }

  return false;
}

/* Remove VAR from the list of referenced variables and clear its
   var-annotation.  */

void
remove_referenced_var (tree var)
{
  var_ann_t v_ann;
  struct tree_decl_minimal in;
  void **loc;
  unsigned int uid = DECL_UID (var);

  gcc_checking_assert (TREE_CODE (var) == VAR_DECL
		       || TREE_CODE (var) == PARM_DECL
		       || TREE_CODE (var) == RESULT_DECL);

  gcc_checking_assert (!is_global_var (var));

  v_ann = var_ann (var);
  ggc_free (v_ann);
  *DECL_VAR_ANN_PTR (var) = NULL;

  in.uid = uid;
  loc = htab_find_slot_with_hash (gimple_referenced_vars (cfun), &in, uid,
				  NO_INSERT);
  htab_clear_slot (gimple_referenced_vars (cfun), loc);
}


/* If EXP is a handled component reference for a structure, return the
   base variable.  The access range is delimited by bit positions *POFFSET and
   *POFFSET + *PMAX_SIZE.  The access size is *PSIZE bits.  If either
   *PSIZE or *PMAX_SIZE is -1, they could not be determined.  If *PSIZE
   and *PMAX_SIZE are equal, the access is non-variable.  */

tree
get_ref_base_and_extent (tree exp, HOST_WIDE_INT *poffset,
			 HOST_WIDE_INT *psize,
			 HOST_WIDE_INT *pmax_size)
{
  HOST_WIDE_INT bitsize = -1;
  HOST_WIDE_INT maxsize = -1;
  tree size_tree = NULL_TREE;
  double_int bit_offset = double_int_zero;
  HOST_WIDE_INT hbit_offset;
  bool seen_variable_array_ref = false;
  tree base_type;

  /* First get the final access size from just the outermost expression.  */
  if (TREE_CODE (exp) == COMPONENT_REF)
    size_tree = DECL_SIZE (TREE_OPERAND (exp, 1));
  else if (TREE_CODE (exp) == BIT_FIELD_REF)
    size_tree = TREE_OPERAND (exp, 1);
  else if (!VOID_TYPE_P (TREE_TYPE (exp)))
    {
      enum machine_mode mode = TYPE_MODE (TREE_TYPE (exp));
      if (mode == BLKmode)
	size_tree = TYPE_SIZE (TREE_TYPE (exp));
      else
	bitsize = GET_MODE_BITSIZE (mode);
    }
  if (size_tree != NULL_TREE)
    {
      if (! host_integerp (size_tree, 1))
	bitsize = -1;
      else
	bitsize = TREE_INT_CST_LOW (size_tree);
    }

  /* Initially, maxsize is the same as the accessed element size.
     In the following it will only grow (or become -1).  */
  maxsize = bitsize;

  /* Compute cumulative bit-offset for nested component-refs and array-refs,
     and find the ultimate containing object.  */
  while (1)
    {
      base_type = TREE_TYPE (exp);

      switch (TREE_CODE (exp))
	{
	case BIT_FIELD_REF:
	  bit_offset
	    = double_int_add (bit_offset,
			      tree_to_double_int (TREE_OPERAND (exp, 2)));
	  break;

	case COMPONENT_REF:
	  {
	    tree field = TREE_OPERAND (exp, 1);
	    tree this_offset = component_ref_field_offset (exp);

	    if (this_offset && TREE_CODE (this_offset) == INTEGER_CST)
	      {
		double_int doffset = tree_to_double_int (this_offset);
		doffset = double_int_lshift (doffset,
					     BITS_PER_UNIT == 8
					     ? 3 : exact_log2 (BITS_PER_UNIT),
					     HOST_BITS_PER_DOUBLE_INT, true);
		doffset = double_int_add (doffset,
					  tree_to_double_int
					  (DECL_FIELD_BIT_OFFSET (field)));
		bit_offset = double_int_add (bit_offset, doffset);

		/* If we had seen a variable array ref already and we just
		   referenced the last field of a struct or a union member
		   then we have to adjust maxsize by the padding at the end
		   of our field.  */
		if (seen_variable_array_ref && maxsize != -1)
		  {
		    tree stype = TREE_TYPE (TREE_OPERAND (exp, 0));
		    tree next = DECL_CHAIN (field);
		    while (next && TREE_CODE (next) != FIELD_DECL)
		      next = DECL_CHAIN (next);
		    if (!next
			|| TREE_CODE (stype) != RECORD_TYPE)
		      {
			tree fsize = DECL_SIZE_UNIT (field);
			tree ssize = TYPE_SIZE_UNIT (stype);
			if (host_integerp (fsize, 0)
			    && host_integerp (ssize, 0)
			    && double_int_fits_in_shwi_p (doffset))
			  maxsize += ((TREE_INT_CST_LOW (ssize)
				       - TREE_INT_CST_LOW (fsize))
				      * BITS_PER_UNIT
					- double_int_to_shwi (doffset));
			else
			  maxsize = -1;
		      }
		  }
	      }
	    else
	      {
		tree csize = TYPE_SIZE (TREE_TYPE (TREE_OPERAND (exp, 0)));
		/* We need to adjust maxsize to the whole structure bitsize.
		   But we can subtract any constant offset seen so far,
		   because that would get us out of the structure otherwise.  */
		if (maxsize != -1
		    && csize
		    && host_integerp (csize, 1)
		    && double_int_fits_in_shwi_p (bit_offset))
		  maxsize = TREE_INT_CST_LOW (csize)
			    - double_int_to_shwi (bit_offset);
		else
		  maxsize = -1;
	      }
	  }
	  break;

	case ARRAY_REF:
	case ARRAY_RANGE_REF:
	  {
	    tree index = TREE_OPERAND (exp, 1);
	    tree low_bound, unit_size;

	    /* If the resulting bit-offset is constant, track it.  */
	    if (TREE_CODE (index) == INTEGER_CST
		&& (low_bound = array_ref_low_bound (exp),
 		    TREE_CODE (low_bound) == INTEGER_CST)
		&& (unit_size = array_ref_element_size (exp),
		    TREE_CODE (unit_size) == INTEGER_CST))
	      {
		double_int doffset
		  = double_int_sext
		    (double_int_sub (TREE_INT_CST (index),
				     TREE_INT_CST (low_bound)),
		     TYPE_PRECISION (TREE_TYPE (index)));
		doffset = double_int_mul (doffset,
					  tree_to_double_int (unit_size));
		doffset = double_int_lshift (doffset,
					     BITS_PER_UNIT == 8
					     ? 3 : exact_log2 (BITS_PER_UNIT),
					     HOST_BITS_PER_DOUBLE_INT, true);
		bit_offset = double_int_add (bit_offset, doffset);

		/* An array ref with a constant index up in the structure
		   hierarchy will constrain the size of any variable array ref
		   lower in the access hierarchy.  */
		seen_variable_array_ref = false;
	      }
	    else
	      {
		tree asize = TYPE_SIZE (TREE_TYPE (TREE_OPERAND (exp, 0)));
		/* We need to adjust maxsize to the whole array bitsize.
		   But we can subtract any constant offset seen so far,
		   because that would get us outside of the array otherwise.  */
		if (maxsize != -1
		    && asize
		    && host_integerp (asize, 1)
		    && double_int_fits_in_shwi_p (bit_offset))
		  maxsize = TREE_INT_CST_LOW (asize)
			    - double_int_to_shwi (bit_offset);
		else
		  maxsize = -1;

		/* Remember that we have seen an array ref with a variable
		   index.  */
		seen_variable_array_ref = true;
	      }
	  }
	  break;

	case REALPART_EXPR:
	  break;

	case IMAGPART_EXPR:
	  bit_offset
	    = double_int_add (bit_offset, uhwi_to_double_int (bitsize));
	  break;

	case VIEW_CONVERT_EXPR:
	  break;

	case MEM_REF:
	  /* Hand back the decl for MEM[&decl, off].  */
	  if (TREE_CODE (TREE_OPERAND (exp, 0)) == ADDR_EXPR)
	    {
	      if (integer_zerop (TREE_OPERAND (exp, 1)))
		exp = TREE_OPERAND (TREE_OPERAND (exp, 0), 0);
	      else
		{
		  double_int off = mem_ref_offset (exp);
		  off = double_int_lshift (off,
					   BITS_PER_UNIT == 8
					   ? 3 : exact_log2 (BITS_PER_UNIT),
					   HOST_BITS_PER_DOUBLE_INT, true);
		  off = double_int_add (off, bit_offset);
		  if (double_int_fits_in_shwi_p (off))
		    {
		      bit_offset = off;
		      exp = TREE_OPERAND (TREE_OPERAND (exp, 0), 0);
		    }
		}
	    }
	  goto done;

	case TARGET_MEM_REF:
	  /* Hand back the decl for MEM[&decl, off].  */
	  if (TREE_CODE (TMR_BASE (exp)) == ADDR_EXPR)
	    {
	      /* Via the variable index or index2 we can reach the
		 whole object.  */
	      if (TMR_INDEX (exp) || TMR_INDEX2 (exp))
		{
		  exp = TREE_OPERAND (TMR_BASE (exp), 0);
		  bit_offset = double_int_zero;
		  maxsize = -1;
		  goto done;
		}
	      if (integer_zerop (TMR_OFFSET (exp)))
		exp = TREE_OPERAND (TMR_BASE (exp), 0);
	      else
		{
		  double_int off = mem_ref_offset (exp);
		  off = double_int_lshift (off,
					   BITS_PER_UNIT == 8
					   ? 3 : exact_log2 (BITS_PER_UNIT),
					   HOST_BITS_PER_DOUBLE_INT, true);
		  off = double_int_add (off, bit_offset);
		  if (double_int_fits_in_shwi_p (off))
		    {
		      bit_offset = off;
		      exp = TREE_OPERAND (TMR_BASE (exp), 0);
		    }
		}
	    }
	  goto done;

	default:
	  goto done;
	}

      exp = TREE_OPERAND (exp, 0);
    }
 done:

  if (!double_int_fits_in_shwi_p (bit_offset))
    {
      *poffset = 0;
      *psize = bitsize;
      *pmax_size = -1;

      return exp;
    }

  hbit_offset = double_int_to_shwi (bit_offset);

  /* We need to deal with variable arrays ending structures such as
       struct { int length; int a[1]; } x;           x.a[d]
       struct { struct { int a; int b; } a[1]; } x;  x.a[d].a
       struct { struct { int a[1]; } a[1]; } x;      x.a[0][d], x.a[d][0]
       struct { int len; union { int a[1]; struct X x; } u; } x; x.u.a[d]
     where we do not know maxsize for variable index accesses to
     the array.  The simplest way to conservatively deal with this
     is to punt in the case that offset + maxsize reaches the
     base type boundary.  This needs to include possible trailing padding
     that is there for alignment purposes.  */

  if (seen_variable_array_ref
      && maxsize != -1
      && (!host_integerp (TYPE_SIZE (base_type), 1)
	  || (hbit_offset + maxsize
	      == (signed) TREE_INT_CST_LOW (TYPE_SIZE (base_type)))))
    maxsize = -1;

  /* In case of a decl or constant base object we can do better.  */

  if (DECL_P (exp))
    {
      /* If maxsize is unknown adjust it according to the size of the
         base decl.  */
      if (maxsize == -1
	  && host_integerp (DECL_SIZE (exp), 1))
	maxsize = TREE_INT_CST_LOW (DECL_SIZE (exp)) - hbit_offset;
    }
  else if (CONSTANT_CLASS_P (exp))
    {
      /* If maxsize is unknown adjust it according to the size of the
         base type constant.  */
      if (maxsize == -1
	  && host_integerp (TYPE_SIZE (TREE_TYPE (exp)), 1))
	maxsize = TREE_INT_CST_LOW (TYPE_SIZE (TREE_TYPE (exp))) - hbit_offset;
    }

  /* ???  Due to negative offsets in ARRAY_REF we can end up with
     negative bit_offset here.  We might want to store a zero offset
     in this case.  */
  *poffset = hbit_offset;
  *psize = bitsize;
  *pmax_size = maxsize;

  return exp;
}

/* Returns the base object and a constant BITS_PER_UNIT offset in *POFFSET that
   denotes the starting address of the memory access EXP.
   Returns NULL_TREE if the offset is not constant or any component
   is not BITS_PER_UNIT-aligned.  */

tree
get_addr_base_and_unit_offset (tree exp, HOST_WIDE_INT *poffset)
{
  return get_addr_base_and_unit_offset_1 (exp, poffset, NULL);
}

/* Returns true if STMT references an SSA_NAME that has
   SSA_NAME_OCCURS_IN_ABNORMAL_PHI set, otherwise false.  */

bool
stmt_references_abnormal_ssa_name (gimple stmt)
{
  ssa_op_iter oi;
  use_operand_p use_p;

  FOR_EACH_SSA_USE_OPERAND (use_p, stmt, oi, SSA_OP_USE)
    {
      if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (USE_FROM_PTR (use_p)))
	return true;
    }

  return false;
}
