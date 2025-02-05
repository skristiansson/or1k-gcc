/* Write and read the cgraph to the memory mapped representation of a
   .o file.

   Copyright 2009, 2010, 2011 Free Software Foundation, Inc.
   Contributed by Kenneth Zadeck <zadeck@naturalbridge.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "expr.h"
#include "flags.h"
#include "params.h"
#include "input.h"
#include "hashtab.h"
#include "langhooks.h"
#include "basic-block.h"
#include "tree-flow.h"
#include "cgraph.h"
#include "function.h"
#include "ggc.h"
#include "diagnostic-core.h"
#include "except.h"
#include "vec.h"
#include "timevar.h"
#include "pointer-set.h"
#include "lto-streamer.h"
#include "data-streamer.h"
#include "tree-streamer.h"
#include "gcov-io.h"

static void output_varpool (cgraph_node_set, varpool_node_set);
static void output_cgraph_opt_summary (cgraph_node_set set);
static void input_cgraph_opt_summary (VEC (cgraph_node_ptr, heap) * nodes);

/* Number of LDPR values known to GCC.  */
#define LDPR_NUM_KNOWN (LDPR_PREVAILING_DEF_IRONLY_EXP + 1)

/* All node orders are ofsetted by ORDER_BASE.  */
static int order_base;

/* Cgraph streaming is organized as set of record whose type
   is indicated by a tag.  */
enum LTO_cgraph_tags
{
  /* Must leave 0 for the stopper.  */

  /* Cgraph node without body available.  */
  LTO_cgraph_unavail_node = 1,
  /* Cgraph node with function body.  */
  LTO_cgraph_analyzed_node,
  /* Cgraph edges.  */
  LTO_cgraph_edge,
  LTO_cgraph_indirect_edge,
  LTO_cgraph_last_tag
};

/* Create a new cgraph encoder.  */

lto_cgraph_encoder_t
lto_cgraph_encoder_new (void)
{
  lto_cgraph_encoder_t encoder = XCNEW (struct lto_cgraph_encoder_d);
  encoder->map = pointer_map_create ();
  encoder->nodes = NULL;
  encoder->body = pointer_set_create ();
  return encoder;
}


/* Delete ENCODER and its components.  */

void
lto_cgraph_encoder_delete (lto_cgraph_encoder_t encoder)
{
   VEC_free (cgraph_node_ptr, heap, encoder->nodes);
   pointer_map_destroy (encoder->map);
   pointer_set_destroy (encoder->body);
   free (encoder);
}


/* Return the existing reference number of NODE in the cgraph encoder in
   output block OB.  Assign a new reference if this is the first time
   NODE is encoded.  */

int
lto_cgraph_encoder_encode (lto_cgraph_encoder_t encoder,
			   struct cgraph_node *node)
{
  int ref;
  void **slot;

  slot = pointer_map_contains (encoder->map, node);
  if (!slot)
    {
      ref = VEC_length (cgraph_node_ptr, encoder->nodes);
      slot = pointer_map_insert (encoder->map, node);
      *slot = (void *) (intptr_t) ref;
      VEC_safe_push (cgraph_node_ptr, heap, encoder->nodes, node);
    }
  else
    ref = (int) (intptr_t) *slot;

  return ref;
}

#define LCC_NOT_FOUND	(-1)

/* Look up NODE in encoder.  Return NODE's reference if it has been encoded
   or LCC_NOT_FOUND if it is not there.  */

int
lto_cgraph_encoder_lookup (lto_cgraph_encoder_t encoder,
			   struct cgraph_node *node)
{
  void **slot = pointer_map_contains (encoder->map, node);
  return (slot ? (int) (intptr_t) *slot : LCC_NOT_FOUND);
}


/* Return the cgraph node corresponding to REF using ENCODER.  */

struct cgraph_node *
lto_cgraph_encoder_deref (lto_cgraph_encoder_t encoder, int ref)
{
  if (ref == LCC_NOT_FOUND)
    return NULL;

  return VEC_index (cgraph_node_ptr, encoder->nodes, ref);
}


/* Return TRUE if we should encode initializer of NODE (if any).  */

bool
lto_cgraph_encoder_encode_body_p (lto_cgraph_encoder_t encoder,
				  struct cgraph_node *node)
{
  return pointer_set_contains (encoder->body, node);
}

/* Return TRUE if we should encode body of NODE (if any).  */

static void
lto_set_cgraph_encoder_encode_body (lto_cgraph_encoder_t encoder,
				    struct cgraph_node *node)
{
  pointer_set_insert (encoder->body, node);
}

/* Create a new varpool encoder.  */

lto_varpool_encoder_t
lto_varpool_encoder_new (void)
{
  lto_varpool_encoder_t encoder = XCNEW (struct lto_varpool_encoder_d);
  encoder->map = pointer_map_create ();
  encoder->initializer = pointer_set_create ();
  encoder->nodes = NULL;
  return encoder;
}


/* Delete ENCODER and its components.  */

void
lto_varpool_encoder_delete (lto_varpool_encoder_t encoder)
{
   VEC_free (varpool_node_ptr, heap, encoder->nodes);
   pointer_map_destroy (encoder->map);
   pointer_set_destroy (encoder->initializer);
   free (encoder);
}


/* Return the existing reference number of NODE in the varpool encoder in
   output block OB.  Assign a new reference if this is the first time
   NODE is encoded.  */

int
lto_varpool_encoder_encode (lto_varpool_encoder_t encoder,
			   struct varpool_node *node)
{
  int ref;
  void **slot;

  slot = pointer_map_contains (encoder->map, node);
  if (!slot)
    {
      ref = VEC_length (varpool_node_ptr, encoder->nodes);
      slot = pointer_map_insert (encoder->map, node);
      *slot = (void *) (intptr_t) ref;
      VEC_safe_push (varpool_node_ptr, heap, encoder->nodes, node);
    }
  else
    ref = (int) (intptr_t) *slot;

  return ref;
}

/* Look up NODE in encoder.  Return NODE's reference if it has been encoded
   or LCC_NOT_FOUND if it is not there.  */

int
lto_varpool_encoder_lookup (lto_varpool_encoder_t encoder,
			   struct varpool_node *node)
{
  void **slot = pointer_map_contains (encoder->map, node);
  return (slot ? (int) (intptr_t) *slot : LCC_NOT_FOUND);
}


/* Return the varpool node corresponding to REF using ENCODER.  */

struct varpool_node *
lto_varpool_encoder_deref (lto_varpool_encoder_t encoder, int ref)
{
  if (ref == LCC_NOT_FOUND)
    return NULL;

  return VEC_index (varpool_node_ptr, encoder->nodes, ref);
}


/* Return TRUE if we should encode initializer of NODE (if any).  */

bool
lto_varpool_encoder_encode_initializer_p (lto_varpool_encoder_t encoder,
					  struct varpool_node *node)
{
  return pointer_set_contains (encoder->initializer, node);
}

/* Return TRUE if we should encode initializer of NODE (if any).  */

static void
lto_set_varpool_encoder_encode_initializer (lto_varpool_encoder_t encoder,
					    struct varpool_node *node)
{
  pointer_set_insert (encoder->initializer, node);
}

/* Output the cgraph EDGE to OB using ENCODER.  */

static void
lto_output_edge (struct lto_simple_output_block *ob, struct cgraph_edge *edge,
		 lto_cgraph_encoder_t encoder)
{
  unsigned int uid;
  intptr_t ref;
  struct bitpack_d bp;

  if (edge->indirect_unknown_callee)
    streamer_write_enum (ob->main_stream, LTO_cgraph_tags, LTO_cgraph_last_tag,
			 LTO_cgraph_indirect_edge);
  else
    streamer_write_enum (ob->main_stream, LTO_cgraph_tags, LTO_cgraph_last_tag,
			 LTO_cgraph_edge);

  ref = lto_cgraph_encoder_lookup (encoder, edge->caller);
  gcc_assert (ref != LCC_NOT_FOUND);
  streamer_write_hwi_stream (ob->main_stream, ref);

  if (!edge->indirect_unknown_callee)
    {
      ref = lto_cgraph_encoder_lookup (encoder, edge->callee);
      gcc_assert (ref != LCC_NOT_FOUND);
      streamer_write_hwi_stream (ob->main_stream, ref);
    }

  streamer_write_hwi_stream (ob->main_stream, edge->count);

  bp = bitpack_create (ob->main_stream);
  uid = (!gimple_has_body_p (edge->caller->symbol.decl)
	 ? edge->lto_stmt_uid : gimple_uid (edge->call_stmt));
  bp_pack_enum (&bp, cgraph_inline_failed_enum,
	        CIF_N_REASONS, edge->inline_failed);
  bp_pack_var_len_unsigned (&bp, uid);
  bp_pack_var_len_unsigned (&bp, edge->frequency);
  bp_pack_value (&bp, edge->indirect_inlining_edge, 1);
  bp_pack_value (&bp, edge->call_stmt_cannot_inline_p, 1);
  bp_pack_value (&bp, edge->can_throw_external, 1);
  if (edge->indirect_unknown_callee)
    {
      int flags = edge->indirect_info->ecf_flags;
      bp_pack_value (&bp, (flags & ECF_CONST) != 0, 1);
      bp_pack_value (&bp, (flags & ECF_PURE) != 0, 1);
      bp_pack_value (&bp, (flags & ECF_NORETURN) != 0, 1);
      bp_pack_value (&bp, (flags & ECF_MALLOC) != 0, 1);
      bp_pack_value (&bp, (flags & ECF_NOTHROW) != 0, 1);
      bp_pack_value (&bp, (flags & ECF_RETURNS_TWICE) != 0, 1);
      /* Flags that should not appear on indirect calls.  */
      gcc_assert (!(flags & (ECF_LOOPING_CONST_OR_PURE
			     | ECF_MAY_BE_ALLOCA
			     | ECF_SIBCALL
			     | ECF_LEAF
			     | ECF_NOVOPS)));
    }
  streamer_write_bitpack (&bp);
}

/* Return if LIST contain references from other partitions.  */

bool
referenced_from_other_partition_p (struct ipa_ref_list *list, cgraph_node_set set,
				   varpool_node_set vset)
{
  int i;
  struct ipa_ref *ref;
  for (i = 0; ipa_ref_list_referring_iterate (list, i, ref); i++)
    {
      if (symtab_function_p (ref->referring))
	{
	  if (ipa_ref_referring_node (ref)->symbol.in_other_partition
	      || !cgraph_node_in_set_p (ipa_ref_referring_node (ref), set))
	    return true;
	}
      else
	{
	  if (ipa_ref_referring_varpool_node (ref)->symbol.in_other_partition
	      || !varpool_node_in_set_p (ipa_ref_referring_varpool_node (ref),
				         vset))
	    return true;
	}
    }
  return false;
}

/* Return true when node is reachable from other partition.  */

bool
reachable_from_other_partition_p (struct cgraph_node *node, cgraph_node_set set)
{
  struct cgraph_edge *e;
  if (!node->analyzed)
    return false;
  if (node->global.inlined_to)
    return false;
  for (e = node->callers; e; e = e->next_caller)
    if (e->caller->symbol.in_other_partition
	|| !cgraph_node_in_set_p (e->caller, set))
      return true;
  return false;
}

/* Return if LIST contain references from other partitions.  */

bool
referenced_from_this_partition_p (struct ipa_ref_list *list, cgraph_node_set set,
				  varpool_node_set vset)
{
  int i;
  struct ipa_ref *ref;
  for (i = 0; ipa_ref_list_referring_iterate (list, i, ref); i++)
    {
      if (symtab_function_p (ref->referring))
	{
	  if (cgraph_node_in_set_p (ipa_ref_referring_node (ref), set))
	    return true;
	}
      else
	{
	  if (varpool_node_in_set_p (ipa_ref_referring_varpool_node (ref),
				     vset))
	    return true;
	}
    }
  return false;
}

/* Return true when node is reachable from other partition.  */

bool
reachable_from_this_partition_p (struct cgraph_node *node, cgraph_node_set set)
{
  struct cgraph_edge *e;
  for (e = node->callers; e; e = e->next_caller)
    if (cgraph_node_in_set_p (e->caller, set))
      return true;
  return false;
}

/* Output the cgraph NODE to OB.  ENCODER is used to find the
   reference number of NODE->inlined_to.  SET is the set of nodes we
   are writing to the current file.  If NODE is not in SET, then NODE
   is a boundary of a cgraph_node_set and we pretend NODE just has a
   decl and no callees.  WRITTEN_DECLS is the set of FUNCTION_DECLs
   that have had their callgraph node written so far.  This is used to
   determine if NODE is a clone of a previously written node.  */

static void
lto_output_node (struct lto_simple_output_block *ob, struct cgraph_node *node,
		 lto_cgraph_encoder_t encoder, cgraph_node_set set,
		 varpool_node_set vset)
{
  unsigned int tag;
  struct bitpack_d bp;
  bool boundary_p;
  intptr_t ref;
  bool in_other_partition = false;
  struct cgraph_node *clone_of;

  boundary_p = !cgraph_node_in_set_p (node, set);

  if (node->analyzed && !boundary_p)
    tag = LTO_cgraph_analyzed_node;
  else
    tag = LTO_cgraph_unavail_node;

  streamer_write_enum (ob->main_stream, LTO_cgraph_tags, LTO_cgraph_last_tag,
		       tag);
  streamer_write_hwi_stream (ob->main_stream, node->symbol.order);

  /* In WPA mode, we only output part of the call-graph.  Also, we
     fake cgraph node attributes.  There are two cases that we care.

     Boundary nodes: There are nodes that are not part of SET but are
     called from within SET.  We artificially make them look like
     externally visible nodes with no function body.

     Cherry-picked nodes:  These are nodes we pulled from other
     translation units into SET during IPA-inlining.  We make them as
     local static nodes to prevent clashes with other local statics.  */
  if (boundary_p && node->analyzed && !DECL_EXTERNAL (node->symbol.decl))
    {
      /* Inline clones can not be part of boundary.  
         gcc_assert (!node->global.inlined_to);  

	 FIXME: At the moment they can be, when partition contains an inline
	 clone that is clone of inline clone from outside partition.  We can
	 reshape the clone tree and make other tree to be the root, but it
	 needs a bit extra work and will be promplty done by cgraph_remove_node
	 after reading back.  */
      in_other_partition = 1;
    }

  clone_of = node->clone_of;
  while (clone_of
	 && (ref = lto_cgraph_encoder_lookup (encoder, clone_of)) == LCC_NOT_FOUND)
    if (clone_of->prev_sibling_clone)
      clone_of = clone_of->prev_sibling_clone;
    else
      clone_of = clone_of->clone_of;

  if (LTO_cgraph_analyzed_node)
    gcc_assert (clone_of || !node->clone_of);
  if (!clone_of)
    streamer_write_hwi_stream (ob->main_stream, LCC_NOT_FOUND);
  else
    streamer_write_hwi_stream (ob->main_stream, ref);


  lto_output_fn_decl_index (ob->decl_state, ob->main_stream, node->symbol.decl);
  streamer_write_hwi_stream (ob->main_stream, node->count);
  streamer_write_hwi_stream (ob->main_stream, node->count_materialization_scale);

  if (tag == LTO_cgraph_analyzed_node)
    {
      if (node->global.inlined_to)
	{
	  ref = lto_cgraph_encoder_lookup (encoder, node->global.inlined_to);
	  gcc_assert (ref != LCC_NOT_FOUND);
	}
      else
	ref = LCC_NOT_FOUND;

      streamer_write_hwi_stream (ob->main_stream, ref);
    }

  if (node->symbol.same_comdat_group && !boundary_p)
    {
      ref = lto_cgraph_encoder_lookup (encoder,
				       cgraph (node->symbol.same_comdat_group));
      gcc_assert (ref != LCC_NOT_FOUND);
    }
  else
    ref = LCC_NOT_FOUND;
  streamer_write_hwi_stream (ob->main_stream, ref);

  bp = bitpack_create (ob->main_stream);
  bp_pack_value (&bp, node->local.local, 1);
  bp_pack_value (&bp, node->symbol.externally_visible, 1);
  bp_pack_value (&bp, node->local.finalized, 1);
  bp_pack_value (&bp, node->local.versionable, 1);
  bp_pack_value (&bp, node->local.can_change_signature, 1);
  bp_pack_value (&bp, node->local.redefined_extern_inline, 1);
  bp_pack_value (&bp, node->symbol.force_output, 1);
  bp_pack_value (&bp, node->symbol.address_taken, 1);
  bp_pack_value (&bp, node->abstract_and_needed, 1);
  bp_pack_value (&bp, tag == LTO_cgraph_analyzed_node
		 && !DECL_EXTERNAL (node->symbol.decl)
		 && !DECL_COMDAT (node->symbol.decl)
		 && (reachable_from_other_partition_p (node, set)
		     || referenced_from_other_partition_p (&node->symbol.ref_list,
							   set, vset)), 1);
  bp_pack_value (&bp, node->lowered, 1);
  bp_pack_value (&bp, in_other_partition, 1);
  /* Real aliases in a boundary become non-aliases. However we still stream
     alias info on weakrefs. 
     TODO: We lose a bit of information here - when we know that variable is
     defined in other unit, we may use the info on aliases to resolve 
     symbol1 != symbol2 type tests that we can do only for locally defined objects
     otherwise.  */
  bp_pack_value (&bp, node->alias && (!boundary_p || DECL_EXTERNAL (node->symbol.decl)), 1);
  bp_pack_value (&bp, node->frequency, 2);
  bp_pack_value (&bp, node->only_called_at_startup, 1);
  bp_pack_value (&bp, node->only_called_at_exit, 1);
  bp_pack_value (&bp, node->tm_clone, 1);
  bp_pack_value (&bp, node->thunk.thunk_p && !boundary_p, 1);
  bp_pack_enum (&bp, ld_plugin_symbol_resolution,
	        LDPR_NUM_KNOWN, node->symbol.resolution);
  streamer_write_bitpack (&bp);

  if (node->thunk.thunk_p && !boundary_p)
    {
      streamer_write_uhwi_stream
	 (ob->main_stream,
	  1 + (node->thunk.this_adjusting != 0) * 2
	  + (node->thunk.virtual_offset_p != 0) * 4);
      streamer_write_uhwi_stream (ob->main_stream, node->thunk.fixed_offset);
      streamer_write_uhwi_stream (ob->main_stream, node->thunk.virtual_value);
    }
  if ((node->alias || node->thunk.thunk_p)
      && (!boundary_p || (node->alias && DECL_EXTERNAL (node->symbol.decl))))
    {
      streamer_write_hwi_in_range (ob->main_stream, 0, 1,
					node->thunk.alias != NULL);
      if (node->thunk.alias != NULL)
        lto_output_fn_decl_index (ob->decl_state, ob->main_stream,
			          node->thunk.alias);
    }
}

/* Output the varpool NODE to OB. 
   If NODE is not in SET, then NODE is a boundary.  */

static void
lto_output_varpool_node (struct lto_simple_output_block *ob, struct varpool_node *node,
			 lto_varpool_encoder_t varpool_encoder,
		         cgraph_node_set set, varpool_node_set vset)
{
  bool boundary_p = !varpool_node_in_set_p (node, vset) && node->analyzed;
  struct bitpack_d bp;
  int ref;

  streamer_write_hwi_stream (ob->main_stream, node->symbol.order);
  lto_output_var_decl_index (ob->decl_state, ob->main_stream, node->symbol.decl);
  bp = bitpack_create (ob->main_stream);
  bp_pack_value (&bp, node->symbol.externally_visible, 1);
  bp_pack_value (&bp, node->symbol.force_output, 1);
  bp_pack_value (&bp, node->finalized, 1);
  bp_pack_value (&bp, node->alias, 1);
  bp_pack_value (&bp, node->alias_of != NULL, 1);
  gcc_assert (node->finalized || !node->analyzed);
  /* Constant pool initializers can be de-unified into individual ltrans units.
     FIXME: Alternatively at -Os we may want to avoid generating for them the local
     labels and share them across LTRANS partitions.  */
  if (DECL_IN_CONSTANT_POOL (node->symbol.decl)
      && !DECL_EXTERNAL (node->symbol.decl)
      && !DECL_COMDAT (node->symbol.decl))
    {
      bp_pack_value (&bp, 0, 1);  /* used_from_other_parition.  */
      bp_pack_value (&bp, 0, 1);  /* in_other_partition.  */
    }
  else
    {
      bp_pack_value (&bp, node->analyzed
		     && referenced_from_other_partition_p (&node->symbol.ref_list,
							   set, vset), 1);
      bp_pack_value (&bp, boundary_p && !DECL_EXTERNAL (node->symbol.decl), 1);
	  /* in_other_partition.  */
    }
  streamer_write_bitpack (&bp);
  if (node->alias_of)
    lto_output_var_decl_index (ob->decl_state, ob->main_stream, node->alias_of);
  if (node->symbol.same_comdat_group && !boundary_p)
    {
      ref = lto_varpool_encoder_lookup (varpool_encoder,
					varpool (node->symbol.same_comdat_group));
      gcc_assert (ref != LCC_NOT_FOUND);
    }
  else
    ref = LCC_NOT_FOUND;
  streamer_write_hwi_stream (ob->main_stream, ref);
  streamer_write_enum (ob->main_stream, ld_plugin_symbol_resolution,
		       LDPR_NUM_KNOWN, node->symbol.resolution);
}

/* Output the varpool NODE to OB. 
   If NODE is not in SET, then NODE is a boundary.  */

static void
lto_output_ref (struct lto_simple_output_block *ob, struct ipa_ref *ref,
		lto_cgraph_encoder_t encoder,
		lto_varpool_encoder_t varpool_encoder)
{
  struct bitpack_d bp;
  bp = bitpack_create (ob->main_stream);
  bp_pack_value (&bp, symtab_function_p (ref->referred), 1);
  bp_pack_value (&bp, ref->use, 2);
  streamer_write_bitpack (&bp);
  if (symtab_function_p (ref->referred))
    {
      int nref = lto_cgraph_encoder_lookup (encoder, ipa_ref_node (ref));
      gcc_assert (nref != LCC_NOT_FOUND);
      streamer_write_hwi_stream (ob->main_stream, nref);
    }
  else
    {
      int nref = lto_varpool_encoder_lookup (varpool_encoder,
				             ipa_ref_varpool_node (ref));
      gcc_assert (nref != LCC_NOT_FOUND);
      streamer_write_hwi_stream (ob->main_stream, nref);
    }
}

/* Stream out profile_summary to OB.  */

static void
output_profile_summary (struct lto_simple_output_block *ob)
{
  if (profile_info)
    {
      /* We do not output num, sum_all and run_max, they are not used by
	 GCC profile feedback and they are difficult to merge from multiple
	 units.  */
      gcc_assert (profile_info->runs);
      streamer_write_uhwi_stream (ob->main_stream, profile_info->runs);
      streamer_write_uhwi_stream (ob->main_stream, profile_info->sum_max);
    }
  else
    streamer_write_uhwi_stream (ob->main_stream, 0);
}

/* Add NODE into encoder as well as nodes it is cloned from.
   Do it in a way so clones appear first.  */

static void
add_node_to (lto_cgraph_encoder_t encoder, struct cgraph_node *node,
	     bool include_body)
{
  if (node->clone_of)
    add_node_to (encoder, node->clone_of, include_body);
  else if (include_body)
    lto_set_cgraph_encoder_encode_body (encoder, node);
  lto_cgraph_encoder_encode (encoder, node);
}

/* Add all references in LIST to encoders.  */

static void
add_references (lto_cgraph_encoder_t encoder,
		lto_varpool_encoder_t varpool_encoder,
		struct ipa_ref_list *list)
{
  int i;
  struct ipa_ref *ref;
  for (i = 0; ipa_ref_list_reference_iterate (list, i, ref); i++)
    if (symtab_function_p (ref->referred))
      add_node_to (encoder, ipa_ref_node (ref), false);
    else
      {
	struct varpool_node *vnode = ipa_ref_varpool_node (ref);
        lto_varpool_encoder_encode (varpool_encoder, vnode);
      }
}

/* Output all callees or indirect outgoing edges.  EDGE must be the first such
   edge.  */

static void
output_outgoing_cgraph_edges (struct cgraph_edge *edge,
			      struct lto_simple_output_block *ob,
			      lto_cgraph_encoder_t encoder)
{
  if (!edge)
    return;

  /* Output edges in backward direction, so the reconstructed callgraph match
     and it is easy to associate call sites in the IPA pass summaries.  */
  while (edge->next_callee)
    edge = edge->next_callee;
  for (; edge; edge = edge->prev_callee)
    lto_output_edge (ob, edge, encoder);
}

/* Output the part of the cgraph in SET.  */

static void
output_refs (cgraph_node_set set, varpool_node_set vset,
	     lto_cgraph_encoder_t encoder,
	     lto_varpool_encoder_t varpool_encoder)
{
  cgraph_node_set_iterator csi;
  varpool_node_set_iterator vsi;
  struct lto_simple_output_block *ob;
  int count;
  struct ipa_ref *ref;
  int i;

  ob = lto_create_simple_output_block (LTO_section_refs);

  for (csi = csi_start (set); !csi_end_p (csi); csi_next (&csi))
    {
      struct cgraph_node *node = csi_node (csi);

      count = ipa_ref_list_nreferences (&node->symbol.ref_list);
      if (count)
	{
	  streamer_write_uhwi_stream (ob->main_stream, count);
	  streamer_write_uhwi_stream (ob->main_stream,
				     lto_cgraph_encoder_lookup (encoder, node));
	  for (i = 0; ipa_ref_list_reference_iterate (&node->symbol.ref_list,
						      i, ref); i++)
	    lto_output_ref (ob, ref, encoder, varpool_encoder);
	}
    }

  streamer_write_uhwi_stream (ob->main_stream, 0);

  for (vsi = vsi_start (vset); !vsi_end_p (vsi); vsi_next (&vsi))
    {
      struct varpool_node *node = vsi_node (vsi);

      count = ipa_ref_list_nreferences (&node->symbol.ref_list);
      if (count)
	{
	  streamer_write_uhwi_stream (ob->main_stream, count);
	  streamer_write_uhwi_stream (ob->main_stream,
				     lto_varpool_encoder_lookup (varpool_encoder,
								 node));
	  for (i = 0; ipa_ref_list_reference_iterate (&node->symbol.ref_list,
						      i, ref); i++)
	    lto_output_ref (ob, ref, encoder, varpool_encoder);
	}
    }

  streamer_write_uhwi_stream (ob->main_stream, 0);

  lto_destroy_simple_output_block (ob);
}

/* Find out all cgraph and varpool nodes we want to encode in current unit
   and insert them to encoders.  */
void
compute_ltrans_boundary (struct lto_out_decl_state *state,
			 cgraph_node_set set, varpool_node_set vset)
{
  struct cgraph_node *node;
  cgraph_node_set_iterator csi;
  varpool_node_set_iterator vsi;
  struct cgraph_edge *edge;
  int i;
  lto_cgraph_encoder_t encoder;
  lto_varpool_encoder_t varpool_encoder;

  encoder = state->cgraph_node_encoder = lto_cgraph_encoder_new ();
  varpool_encoder = state->varpool_node_encoder = lto_varpool_encoder_new ();

  /* Go over all the nodes in SET and assign references.  */
  for (csi = csi_start (set); !csi_end_p (csi); csi_next (&csi))
    {
      node = csi_node (csi);
      add_node_to (encoder, node, true);
      add_references (encoder, varpool_encoder, &node->symbol.ref_list);
    }
  for (vsi = vsi_start (vset); !vsi_end_p (vsi); vsi_next (&vsi))
    {
      struct varpool_node *vnode = vsi_node (vsi);
      gcc_assert (!vnode->alias || vnode->alias_of);
      lto_varpool_encoder_encode (varpool_encoder, vnode);
      lto_set_varpool_encoder_encode_initializer (varpool_encoder, vnode);
      add_references (encoder, varpool_encoder, &vnode->symbol.ref_list);
    }
  /* Pickle in also the initializer of all referenced readonly variables
     to help folding.  Constant pool variables are not shared, so we must
     pickle those too.  */
  for (i = 0; i < lto_varpool_encoder_size (varpool_encoder); i++)
    {
      struct varpool_node *vnode = lto_varpool_encoder_deref (varpool_encoder, i);
      if (DECL_INITIAL (vnode->symbol.decl)
	  && !lto_varpool_encoder_encode_initializer_p (varpool_encoder,
						        vnode)
	  && const_value_known_p (vnode->symbol.decl))
	{
	  lto_set_varpool_encoder_encode_initializer (varpool_encoder, vnode);
	  add_references (encoder, varpool_encoder, &vnode->symbol.ref_list);
	}
      else if (vnode->alias || vnode->alias_of)
        add_references (encoder, varpool_encoder, &vnode->symbol.ref_list);
    }

  /* Go over all the nodes again to include callees that are not in
     SET.  */
  for (csi = csi_start (set); !csi_end_p (csi); csi_next (&csi))
    {
      node = csi_node (csi);
      for (edge = node->callees; edge; edge = edge->next_callee)
	{
	  struct cgraph_node *callee = edge->callee;
	  if (!cgraph_node_in_set_p (callee, set))
	    {
	      /* We should have moved all the inlines.  */
	      gcc_assert (!callee->global.inlined_to);
	      add_node_to (encoder, callee, false);
	    }
	}
    }
}

/* Output the part of the cgraph in SET.  */

void
output_cgraph (cgraph_node_set set, varpool_node_set vset)
{
  struct cgraph_node *node;
  struct lto_simple_output_block *ob;
  cgraph_node_set_iterator csi;
  int i, n_nodes;
  lto_cgraph_encoder_t encoder;
  lto_varpool_encoder_t varpool_encoder;
  static bool asm_nodes_output = false;

  if (flag_wpa)
    output_cgraph_opt_summary (set);

  ob = lto_create_simple_output_block (LTO_section_cgraph);

  output_profile_summary (ob);

  /* An encoder for cgraph nodes should have been created by
     ipa_write_summaries_1.  */
  gcc_assert (ob->decl_state->cgraph_node_encoder);
  gcc_assert (ob->decl_state->varpool_node_encoder);
  encoder = ob->decl_state->cgraph_node_encoder;
  varpool_encoder = ob->decl_state->varpool_node_encoder;

  /* Write out the nodes.  We must first output a node and then its clones,
     otherwise at a time reading back the node there would be nothing to clone
     from.  */
  n_nodes = lto_cgraph_encoder_size (encoder);
  for (i = 0; i < n_nodes; i++)
    {
      node = lto_cgraph_encoder_deref (encoder, i);
      lto_output_node (ob, node, encoder, set, vset);
    }

  /* Go over the nodes in SET again to write edges.  */
  for (csi = csi_start (set); !csi_end_p (csi); csi_next (&csi))
    {
      node = csi_node (csi);
      output_outgoing_cgraph_edges (node->callees, ob, encoder);
      output_outgoing_cgraph_edges (node->indirect_calls, ob, encoder);
    }

  streamer_write_uhwi_stream (ob->main_stream, 0);

  lto_destroy_simple_output_block (ob);

  /* Emit toplevel asms.
     When doing WPA we must output every asm just once.  Since we do not partition asm
     nodes at all, output them to first output.  This is kind of hack, but should work
     well.  */
  if (!asm_nodes_output)
    {
      asm_nodes_output = true;
      lto_output_toplevel_asms ();
    }

  output_varpool (set, vset);
  output_refs (set, vset, encoder, varpool_encoder);
}

/* Overwrite the information in NODE based on FILE_DATA, TAG, FLAGS,
   STACK_SIZE, SELF_TIME and SELF_SIZE.  This is called either to initialize
   NODE or to replace the values in it, for instance because the first
   time we saw it, the function body was not available but now it
   is.  BP is a bitpack with all the bitflags for NODE read from the
   stream.  */

static void
input_overwrite_node (struct lto_file_decl_data *file_data,
		      struct cgraph_node *node,
		      enum LTO_cgraph_tags tag,
		      struct bitpack_d *bp)
{
  node->symbol.aux = (void *) tag;
  node->symbol.lto_file_data = file_data;

  node->local.local = bp_unpack_value (bp, 1);
  node->symbol.externally_visible = bp_unpack_value (bp, 1);
  node->local.finalized = bp_unpack_value (bp, 1);
  node->local.versionable = bp_unpack_value (bp, 1);
  node->local.can_change_signature = bp_unpack_value (bp, 1);
  node->local.redefined_extern_inline = bp_unpack_value (bp, 1);
  node->symbol.force_output = bp_unpack_value (bp, 1);
  node->symbol.address_taken = bp_unpack_value (bp, 1);
  node->abstract_and_needed = bp_unpack_value (bp, 1);
  node->symbol.used_from_other_partition = bp_unpack_value (bp, 1);
  node->lowered = bp_unpack_value (bp, 1);
  node->analyzed = tag == LTO_cgraph_analyzed_node;
  node->symbol.in_other_partition = bp_unpack_value (bp, 1);
  if (node->symbol.in_other_partition
      /* Avoid updating decl when we are seeing just inline clone.
	 When inlining function that has functions already inlined into it,
	 we produce clones of inline clones.

	 WPA partitioning might put each clone into different unit and
	 we might end up streaming inline clone from other partition
	 to support clone we are interested in. */
      && (!node->clone_of
	  || node->clone_of->symbol.decl != node->symbol.decl))
    {
      DECL_EXTERNAL (node->symbol.decl) = 1;
      TREE_STATIC (node->symbol.decl) = 0;
    }
  node->alias = bp_unpack_value (bp, 1);
  node->frequency = (enum node_frequency)bp_unpack_value (bp, 2);
  node->only_called_at_startup = bp_unpack_value (bp, 1);
  node->only_called_at_exit = bp_unpack_value (bp, 1);
  node->tm_clone = bp_unpack_value (bp, 1);
  node->thunk.thunk_p = bp_unpack_value (bp, 1);
  node->symbol.resolution = bp_unpack_enum (bp, ld_plugin_symbol_resolution,
				     LDPR_NUM_KNOWN);
}

/* Output the part of the cgraph in SET.  */

static void
output_varpool (cgraph_node_set set, varpool_node_set vset)
{
  struct lto_simple_output_block *ob = lto_create_simple_output_block (LTO_section_varpool);
  lto_varpool_encoder_t varpool_encoder = ob->decl_state->varpool_node_encoder;
  int len = lto_varpool_encoder_size (varpool_encoder), i;

  streamer_write_uhwi_stream (ob->main_stream, len);

  /* Write out the nodes.  We must first output a node and then its clones,
     otherwise at a time reading back the node there would be nothing to clone
     from.  */
  for (i = 0; i < len; i++)
    {
      lto_output_varpool_node (ob, lto_varpool_encoder_deref (varpool_encoder, i),
      			       varpool_encoder,
			       set, vset);
    }

  lto_destroy_simple_output_block (ob);
}

/* Read a node from input_block IB.  TAG is the node's tag just read.
   Return the node read or overwriten.  */

static struct cgraph_node *
input_node (struct lto_file_decl_data *file_data,
	    struct lto_input_block *ib,
	    enum LTO_cgraph_tags tag,
	    VEC(cgraph_node_ptr, heap) *nodes)
{
  tree fn_decl;
  struct cgraph_node *node;
  struct bitpack_d bp;
  unsigned decl_index;
  int ref = LCC_NOT_FOUND, ref2 = LCC_NOT_FOUND;
  int clone_ref;
  int order;

  order = streamer_read_hwi (ib) + order_base;
  clone_ref = streamer_read_hwi (ib);

  decl_index = streamer_read_uhwi (ib);
  fn_decl = lto_file_decl_data_get_fn_decl (file_data, decl_index);

  if (clone_ref != LCC_NOT_FOUND)
    {
      node = cgraph_clone_node (VEC_index (cgraph_node_ptr, nodes, clone_ref), fn_decl,
				0, CGRAPH_FREQ_BASE, false, NULL, false);
    }
  else
    node = cgraph_get_create_node (fn_decl);

  node->symbol.order = order;
  if (order >= symtab_order)
    symtab_order = order + 1;

  node->count = streamer_read_hwi (ib);
  node->count_materialization_scale = streamer_read_hwi (ib);

  if (tag == LTO_cgraph_analyzed_node)
    ref = streamer_read_hwi (ib);

  ref2 = streamer_read_hwi (ib);

  /* Make sure that we have not read this node before.  Nodes that
     have already been read will have their tag stored in the 'aux'
     field.  Since built-in functions can be referenced in multiple
     functions, they are expected to be read more than once.  */
  if (node->symbol.aux && !DECL_BUILT_IN (node->symbol.decl))
    internal_error ("bytecode stream: found multiple instances of cgraph "
		    "node %d", node->uid);

  bp = streamer_read_bitpack (ib);
  input_overwrite_node (file_data, node, tag, &bp);

  /* Store a reference for now, and fix up later to be a pointer.  */
  node->global.inlined_to = (cgraph_node_ptr) (intptr_t) ref;

  /* Store a reference for now, and fix up later to be a pointer.  */
  node->symbol.same_comdat_group = (symtab_node) (intptr_t) ref2;

  if (node->thunk.thunk_p)
    {
      int type = streamer_read_uhwi (ib);
      HOST_WIDE_INT fixed_offset = streamer_read_uhwi (ib);
      HOST_WIDE_INT virtual_value = streamer_read_uhwi (ib);

      node->thunk.fixed_offset = fixed_offset;
      node->thunk.this_adjusting = (type & 2);
      node->thunk.virtual_value = virtual_value;
      node->thunk.virtual_offset_p = (type & 4);
    }
  if (node->thunk.thunk_p || node->alias)
    {
      if (streamer_read_hwi_in_range (ib, "alias nonzero flag", 0, 1))
	{
          decl_index = streamer_read_uhwi (ib);
          node->thunk.alias = lto_file_decl_data_get_fn_decl (file_data,
							      decl_index);
	}
    }
  return node;
}

/* Read a node from input_block IB.  TAG is the node's tag just read.
   Return the node read or overwriten.  */

static struct varpool_node *
input_varpool_node (struct lto_file_decl_data *file_data,
		    struct lto_input_block *ib)
{
  int decl_index;
  tree var_decl;
  struct varpool_node *node;
  struct bitpack_d bp;
  int ref = LCC_NOT_FOUND;
  bool non_null_aliasof;
  int order;

  order = streamer_read_hwi (ib) + order_base;
  decl_index = streamer_read_uhwi (ib);
  var_decl = lto_file_decl_data_get_var_decl (file_data, decl_index);
  node = varpool_node (var_decl);
  node->symbol.order = order;
  if (order >= symtab_order)
    symtab_order = order + 1;
  node->symbol.lto_file_data = file_data;

  bp = streamer_read_bitpack (ib);
  node->symbol.externally_visible = bp_unpack_value (&bp, 1);
  node->symbol.force_output = bp_unpack_value (&bp, 1);
  node->finalized = bp_unpack_value (&bp, 1);
  node->alias = bp_unpack_value (&bp, 1);
  non_null_aliasof = bp_unpack_value (&bp, 1);
  node->symbol.used_from_other_partition = bp_unpack_value (&bp, 1);
  node->symbol.in_other_partition = bp_unpack_value (&bp, 1);
  node->analyzed = (node->finalized && (!node->alias || !node->symbol.in_other_partition)); 
  if (node->symbol.in_other_partition)
    {
      DECL_EXTERNAL (node->symbol.decl) = 1;
      TREE_STATIC (node->symbol.decl) = 0;
    }
  if (non_null_aliasof)
    {
      decl_index = streamer_read_uhwi (ib);
      node->alias_of = lto_file_decl_data_get_var_decl (file_data, decl_index);
    }
  ref = streamer_read_hwi (ib);
  /* Store a reference for now, and fix up later to be a pointer.  */
  node->symbol.same_comdat_group = (symtab_node) (intptr_t) ref;
  node->symbol.resolution = streamer_read_enum (ib, ld_plugin_symbol_resolution,
					        LDPR_NUM_KNOWN);

  return node;
}

/* Read a node from input_block IB.  TAG is the node's tag just read.
   Return the node read or overwriten.  */

static void
input_ref (struct lto_input_block *ib,
	   symtab_node referring_node,
	   VEC(cgraph_node_ptr, heap) *nodes,
	   VEC(varpool_node_ptr, heap) *varpool_nodes_vec)
{
  struct cgraph_node *node = NULL;
  struct varpool_node *varpool_node = NULL;
  struct bitpack_d bp;
  int type;
  enum ipa_ref_use use;

  bp = streamer_read_bitpack (ib);
  type = bp_unpack_value (&bp, 1);
  use = (enum ipa_ref_use) bp_unpack_value (&bp, 2);
  if (type)
    node = VEC_index (cgraph_node_ptr, nodes, streamer_read_hwi (ib));
  else
    varpool_node = VEC_index (varpool_node_ptr, varpool_nodes_vec,
			      streamer_read_hwi (ib));
  ipa_record_reference (referring_node,
		        node ? (symtab_node) node : (symtab_node) varpool_node, use, NULL);
}

/* Read an edge from IB.  NODES points to a vector of previously read nodes for
   decoding caller and callee of the edge to be read.  If INDIRECT is true, the
   edge being read is indirect (in the sense that it has
   indirect_unknown_callee set).  */

static void
input_edge (struct lto_input_block *ib, VEC(cgraph_node_ptr, heap) *nodes,
	    bool indirect)
{
  struct cgraph_node *caller, *callee;
  struct cgraph_edge *edge;
  unsigned int stmt_id;
  gcov_type count;
  int freq;
  cgraph_inline_failed_t inline_failed;
  struct bitpack_d bp;
  int ecf_flags = 0;

  caller = VEC_index (cgraph_node_ptr, nodes, streamer_read_hwi (ib));
  if (caller == NULL || caller->symbol.decl == NULL_TREE)
    internal_error ("bytecode stream: no caller found while reading edge");

  if (!indirect)
    {
      callee = VEC_index (cgraph_node_ptr, nodes, streamer_read_hwi (ib));
      if (callee == NULL || callee->symbol.decl == NULL_TREE)
	internal_error ("bytecode stream: no callee found while reading edge");
    }
  else
    callee = NULL;

  count = (gcov_type) streamer_read_hwi (ib);

  bp = streamer_read_bitpack (ib);
  inline_failed = bp_unpack_enum (&bp, cgraph_inline_failed_enum, CIF_N_REASONS);
  stmt_id = bp_unpack_var_len_unsigned (&bp);
  freq = (int) bp_unpack_var_len_unsigned (&bp);

  if (indirect)
    edge = cgraph_create_indirect_edge (caller, NULL, 0, count, freq);
  else
    edge = cgraph_create_edge (caller, callee, NULL, count, freq);

  edge->indirect_inlining_edge = bp_unpack_value (&bp, 1);
  edge->lto_stmt_uid = stmt_id;
  edge->inline_failed = inline_failed;
  edge->call_stmt_cannot_inline_p = bp_unpack_value (&bp, 1);
  edge->can_throw_external = bp_unpack_value (&bp, 1);
  if (indirect)
    {
      if (bp_unpack_value (&bp, 1))
	ecf_flags |= ECF_CONST;
      if (bp_unpack_value (&bp, 1))
	ecf_flags |= ECF_PURE;
      if (bp_unpack_value (&bp, 1))
	ecf_flags |= ECF_NORETURN;
      if (bp_unpack_value (&bp, 1))
	ecf_flags |= ECF_MALLOC;
      if (bp_unpack_value (&bp, 1))
	ecf_flags |= ECF_NOTHROW;
      if (bp_unpack_value (&bp, 1))
	ecf_flags |= ECF_RETURNS_TWICE;
      edge->indirect_info->ecf_flags = ecf_flags;
    }
}


/* Read a cgraph from IB using the info in FILE_DATA.  */

static VEC(cgraph_node_ptr, heap) *
input_cgraph_1 (struct lto_file_decl_data *file_data,
		struct lto_input_block *ib)
{
  enum LTO_cgraph_tags tag;
  VEC(cgraph_node_ptr, heap) *nodes = NULL;
  struct cgraph_node *node;
  unsigned i;

  tag = streamer_read_enum (ib, LTO_cgraph_tags, LTO_cgraph_last_tag);
  order_base = symtab_order;
  while (tag)
    {
      if (tag == LTO_cgraph_edge)
        input_edge (ib, nodes, false);
      else if (tag == LTO_cgraph_indirect_edge)
        input_edge (ib, nodes, true);
      else
	{
	  node = input_node (file_data, ib, tag,nodes);
	  if (node == NULL || node->symbol.decl == NULL_TREE)
	    internal_error ("bytecode stream: found empty cgraph node");
	  VEC_safe_push (cgraph_node_ptr, heap, nodes, node);
	  lto_cgraph_encoder_encode (file_data->cgraph_node_encoder, node);
	}

      tag = streamer_read_enum (ib, LTO_cgraph_tags, LTO_cgraph_last_tag);
    }

  lto_input_toplevel_asms (file_data, order_base);

  /* AUX pointers should be all non-zero for nodes read from the stream.  */
#ifdef ENABLE_CHECKING
  FOR_EACH_VEC_ELT (cgraph_node_ptr, nodes, i, node)
    gcc_assert (node->symbol.aux);
#endif
  FOR_EACH_VEC_ELT (cgraph_node_ptr, nodes, i, node)
    {
      int ref = (int) (intptr_t) node->global.inlined_to;

      /* We share declaration of builtins, so we may read same node twice.  */
      if (!node->symbol.aux)
	continue;
      node->symbol.aux = NULL;

      /* Fixup inlined_to from reference to pointer.  */
      if (ref != LCC_NOT_FOUND)
	node->global.inlined_to = VEC_index (cgraph_node_ptr, nodes, ref);
      else
	node->global.inlined_to = NULL;

      ref = (int) (intptr_t) node->symbol.same_comdat_group;

      /* Fixup same_comdat_group from reference to pointer.  */
      if (ref != LCC_NOT_FOUND)
	node->symbol.same_comdat_group = (symtab_node)VEC_index (cgraph_node_ptr, nodes, ref);
      else
	node->symbol.same_comdat_group = NULL;
    }
  FOR_EACH_VEC_ELT (cgraph_node_ptr, nodes, i, node)
    node->symbol.aux = (void *)1;
  return nodes;
}

/* Read a varpool from IB using the info in FILE_DATA.  */

static VEC(varpool_node_ptr, heap) *
input_varpool_1 (struct lto_file_decl_data *file_data,
		struct lto_input_block *ib)
{
  unsigned HOST_WIDE_INT len;
  VEC(varpool_node_ptr, heap) *varpool = NULL;
  int i;
  struct varpool_node *node;

  len = streamer_read_uhwi (ib);
  while (len)
    {
      VEC_safe_push (varpool_node_ptr, heap, varpool,
		     input_varpool_node (file_data, ib));
      len--;
    }
#ifdef ENABLE_CHECKING
  FOR_EACH_VEC_ELT (varpool_node_ptr, varpool, i, node)
    gcc_assert (!node->symbol.aux);
#endif
  FOR_EACH_VEC_ELT (varpool_node_ptr, varpool, i, node)
    {
      int ref = (int) (intptr_t) node->symbol.same_comdat_group;
      /* We share declaration of builtins, so we may read same node twice.  */
      if (node->symbol.aux)
	continue;
      node->symbol.aux = (void *)1;

      /* Fixup same_comdat_group from reference to pointer.  */
      if (ref != LCC_NOT_FOUND)
	node->symbol.same_comdat_group = (symtab_node)VEC_index (varpool_node_ptr, varpool, ref);
      else
	node->symbol.same_comdat_group = NULL;
    }
  FOR_EACH_VEC_ELT (varpool_node_ptr, varpool, i, node)
    node->symbol.aux = NULL;
  return varpool;
}

/* Input ipa_refs.  */

static void
input_refs (struct lto_input_block *ib,
	    VEC(cgraph_node_ptr, heap) *nodes,
	    VEC(varpool_node_ptr, heap) *varpool)
{
  int count;
  int idx;
  while (true)
    {
      struct cgraph_node *node;
      count = streamer_read_uhwi (ib);
      if (!count)
	break;
      idx = streamer_read_uhwi (ib);
      node = VEC_index (cgraph_node_ptr, nodes, idx);
      while (count)
	{
	  input_ref (ib, (symtab_node) node, nodes, varpool);
	  count--;
	}
    }
  while (true)
    {
      struct varpool_node *node;
      count = streamer_read_uhwi (ib);
      if (!count)
	break;
      node = VEC_index (varpool_node_ptr, varpool,
			streamer_read_uhwi (ib));
      while (count)
	{
	  input_ref (ib, (symtab_node) node, nodes, varpool);
	  count--;
	}
    }
}
	    

static struct gcov_ctr_summary lto_gcov_summary;

/* Input profile_info from IB.  */
static void
input_profile_summary (struct lto_input_block *ib,
		       struct lto_file_decl_data *file_data)
{
  unsigned int runs = streamer_read_uhwi (ib);
  if (runs)
    {
      file_data->profile_info.runs = runs;
      file_data->profile_info.sum_max = streamer_read_uhwi (ib);
    }

}

/* Rescale profile summaries to the same number of runs in the whole unit.  */

static void
merge_profile_summaries (struct lto_file_decl_data **file_data_vec)
{
  struct lto_file_decl_data *file_data;
  unsigned int j;
  gcov_unsigned_t max_runs = 0;
  struct cgraph_node *node;
  struct cgraph_edge *edge;

  /* Find unit with maximal number of runs.  If we ever get serious about
     roundoff errors, we might also consider computing smallest common
     multiply.  */
  for (j = 0; (file_data = file_data_vec[j]) != NULL; j++)
    if (max_runs < file_data->profile_info.runs)
      max_runs = file_data->profile_info.runs;

  if (!max_runs)
    return;

  /* Simple overflow check.  We probably don't need to support that many train
     runs. Such a large value probably imply data corruption anyway.  */
  if (max_runs > INT_MAX / REG_BR_PROB_BASE)
    {
      sorry ("At most %i profile runs is supported. Perhaps corrupted profile?",
	     INT_MAX / REG_BR_PROB_BASE);
      return;
    }

  profile_info = &lto_gcov_summary;
  lto_gcov_summary.runs = max_runs;
  lto_gcov_summary.sum_max = 0;

  /* Rescale all units to the maximal number of runs.
     sum_max can not be easily merged, as we have no idea what files come from
     the same run.  We do not use the info anyway, so leave it 0.  */
  for (j = 0; (file_data = file_data_vec[j]) != NULL; j++)
    if (file_data->profile_info.runs)
      {
	int scale = ((REG_BR_PROB_BASE * max_runs
		      + file_data->profile_info.runs / 2)
		     / file_data->profile_info.runs);
	lto_gcov_summary.sum_max = MAX (lto_gcov_summary.sum_max,
					(file_data->profile_info.sum_max
					 * scale
					 + REG_BR_PROB_BASE / 2)
					/ REG_BR_PROB_BASE);
      }

  /* Watch roundoff errors.  */
  if (lto_gcov_summary.sum_max < max_runs)
    lto_gcov_summary.sum_max = max_runs;

  /* If merging already happent at WPA time, we are done.  */
  if (flag_ltrans)
    return;

  /* Now compute count_materialization_scale of each node.
     During LTRANS we already have values of count_materialization_scale
     computed, so just update them.  */
  FOR_EACH_FUNCTION (node)
    if (node->symbol.lto_file_data
	&& node->symbol.lto_file_data->profile_info.runs)
      {
	int scale;

	scale =
	   ((node->count_materialization_scale * max_runs
	     + node->symbol.lto_file_data->profile_info.runs / 2)
	    / node->symbol.lto_file_data->profile_info.runs);
	node->count_materialization_scale = scale;
	if (scale < 0)
	  fatal_error ("Profile information in %s corrupted",
		       file_data->file_name);

	if (scale == REG_BR_PROB_BASE)
	  continue;
	for (edge = node->callees; edge; edge = edge->next_callee)
	  edge->count = ((edge->count * scale + REG_BR_PROB_BASE / 2)
			 / REG_BR_PROB_BASE);
	node->count = ((node->count * scale + REG_BR_PROB_BASE / 2)
		       / REG_BR_PROB_BASE);
      }
}

/* Input and merge the cgraph from each of the .o files passed to
   lto1.  */

void
input_cgraph (void)
{
  struct lto_file_decl_data **file_data_vec = lto_get_file_decl_data ();
  struct lto_file_decl_data *file_data;
  unsigned int j = 0;
  struct cgraph_node *node;

  cgraph_state = CGRAPH_STATE_IPA_SSA;

  while ((file_data = file_data_vec[j++]))
    {
      const char *data;
      size_t len;
      struct lto_input_block *ib;
      VEC(cgraph_node_ptr, heap) *nodes;
      VEC(varpool_node_ptr, heap) *varpool;

      ib = lto_create_simple_input_block (file_data, LTO_section_cgraph,
					  &data, &len);
      if (!ib) 
	fatal_error ("cannot find LTO cgraph in %s", file_data->file_name);
      input_profile_summary (ib, file_data);
      file_data->cgraph_node_encoder = lto_cgraph_encoder_new ();
      nodes = input_cgraph_1 (file_data, ib);
      lto_destroy_simple_input_block (file_data, LTO_section_cgraph,
				      ib, data, len);

      ib = lto_create_simple_input_block (file_data, LTO_section_varpool,
					  &data, &len);
      if (!ib)
	fatal_error ("cannot find LTO varpool in %s", file_data->file_name);
      varpool = input_varpool_1 (file_data, ib);
      lto_destroy_simple_input_block (file_data, LTO_section_varpool,
				      ib, data, len);

      ib = lto_create_simple_input_block (file_data, LTO_section_refs,
					  &data, &len);
      if (!ib)
	fatal_error("cannot find LTO section refs in %s", file_data->file_name);
      input_refs (ib, nodes, varpool);
      lto_destroy_simple_input_block (file_data, LTO_section_refs,
				      ib, data, len);
      if (flag_ltrans)
	input_cgraph_opt_summary (nodes);
      VEC_free (cgraph_node_ptr, heap, nodes);
      VEC_free (varpool_node_ptr, heap, varpool);
    }

  merge_profile_summaries (file_data_vec);

  /* Clear out the aux field that was used to store enough state to
     tell which nodes should be overwritten.  */
  FOR_EACH_FUNCTION (node)
    {
      /* Some nodes may have been created by cgraph_node.  This
	 happens when the callgraph contains nested functions.  If the
	 node for the parent function was never emitted to the gimple
	 file, cgraph_node will create a node for it when setting the
	 context of the nested function.  */
      if (node->symbol.lto_file_data)
	node->symbol.aux = NULL;
    }
}

/* True when we need optimization summary for NODE.  */

static int
output_cgraph_opt_summary_p (struct cgraph_node *node,
			     cgraph_node_set set ATTRIBUTE_UNUSED)
{
  return (node->clone_of
	  && (node->clone.tree_map
	      || node->clone.args_to_skip
	      || node->clone.combined_args_to_skip));
}

/* Output optimization summary for EDGE to OB.  */
static void
output_edge_opt_summary (struct output_block *ob ATTRIBUTE_UNUSED,
			 struct cgraph_edge *edge ATTRIBUTE_UNUSED)
{
}

/* Output optimization summary for NODE to OB.  */

static void
output_node_opt_summary (struct output_block *ob,
			 struct cgraph_node *node,
			 cgraph_node_set set)
{
  unsigned int index;
  bitmap_iterator bi;
  struct ipa_replace_map *map;
  struct bitpack_d bp;
  int i;
  struct cgraph_edge *e;

  if (node->clone.args_to_skip)
    {
      streamer_write_uhwi (ob, bitmap_count_bits (node->clone.args_to_skip));
      EXECUTE_IF_SET_IN_BITMAP (node->clone.args_to_skip, 0, index, bi)
	streamer_write_uhwi (ob, index);
    }
  else
    streamer_write_uhwi (ob, 0);
  if (node->clone.combined_args_to_skip)
    {
      streamer_write_uhwi (ob, bitmap_count_bits (node->clone.combined_args_to_skip));
      EXECUTE_IF_SET_IN_BITMAP (node->clone.combined_args_to_skip, 0, index, bi)
	streamer_write_uhwi (ob, index);
    }
  else
    streamer_write_uhwi (ob, 0);
  streamer_write_uhwi (ob, VEC_length (ipa_replace_map_p,
			               node->clone.tree_map));
  FOR_EACH_VEC_ELT (ipa_replace_map_p, node->clone.tree_map, i, map)
    {
      int parm_num;
      tree parm;

      for (parm_num = 0, parm = DECL_ARGUMENTS (node->symbol.decl); parm;
	   parm = DECL_CHAIN (parm), parm_num++)
	if (map->old_tree == parm)
	  break;
      /* At the moment we assume all old trees to be PARM_DECLs, because we have no
         mechanism to store function local declarations into summaries.  */
      gcc_assert (parm);
      streamer_write_uhwi (ob, parm_num);
      stream_write_tree (ob, map->new_tree, true);
      bp = bitpack_create (ob->main_stream);
      bp_pack_value (&bp, map->replace_p, 1);
      bp_pack_value (&bp, map->ref_p, 1);
      streamer_write_bitpack (&bp);
    }

  if (cgraph_node_in_set_p (node, set))
    {
      for (e = node->callees; e; e = e->next_callee)
	output_edge_opt_summary (ob, e);
      for (e = node->indirect_calls; e; e = e->next_callee)
	output_edge_opt_summary (ob, e);
    }
}

/* Output optimization summaries stored in callgraph.
   At the moment it is the clone info structure.  */

static void
output_cgraph_opt_summary (cgraph_node_set set)
{
  struct cgraph_node *node;
  int i, n_nodes;
  lto_cgraph_encoder_t encoder;
  struct output_block *ob = create_output_block (LTO_section_cgraph_opt_sum);
  unsigned count = 0;

  ob->cgraph_node = NULL;
  encoder = ob->decl_state->cgraph_node_encoder;
  n_nodes = lto_cgraph_encoder_size (encoder);
  for (i = 0; i < n_nodes; i++)
    if (output_cgraph_opt_summary_p (lto_cgraph_encoder_deref (encoder, i),
				     set))
      count++;
  streamer_write_uhwi (ob, count);
  for (i = 0; i < n_nodes; i++)
    {
      node = lto_cgraph_encoder_deref (encoder, i);
      if (output_cgraph_opt_summary_p (node, set))
	{
	  streamer_write_uhwi (ob, i);
	  output_node_opt_summary (ob, node, set);
	}
    }
  produce_asm (ob, NULL);
  destroy_output_block (ob);
}

/* Input optimisation summary of EDGE.  */

static void
input_edge_opt_summary (struct cgraph_edge *edge ATTRIBUTE_UNUSED,
			struct lto_input_block *ib_main ATTRIBUTE_UNUSED)
{
}

/* Input optimisation summary of NODE.  */

static void
input_node_opt_summary (struct cgraph_node *node,
			struct lto_input_block *ib_main,
			struct data_in *data_in)
{
  int i;
  int count;
  int bit;
  struct bitpack_d bp;
  struct cgraph_edge *e;

  count = streamer_read_uhwi (ib_main);
  if (count)
    node->clone.args_to_skip = BITMAP_GGC_ALLOC ();
  for (i = 0; i < count; i++)
    {
      bit = streamer_read_uhwi (ib_main);
      bitmap_set_bit (node->clone.args_to_skip, bit);
    }
  count = streamer_read_uhwi (ib_main);
  if (count)
    node->clone.combined_args_to_skip = BITMAP_GGC_ALLOC ();
  for (i = 0; i < count; i++)
    {
      bit = streamer_read_uhwi (ib_main);
      bitmap_set_bit (node->clone.combined_args_to_skip, bit);
    }
  count = streamer_read_uhwi (ib_main);
  for (i = 0; i < count; i++)
    {
      int parm_num;
      tree parm;
      struct ipa_replace_map *map = ggc_alloc_ipa_replace_map ();

      VEC_safe_push (ipa_replace_map_p, gc, node->clone.tree_map, map);
      for (parm_num = 0, parm = DECL_ARGUMENTS (node->symbol.decl); parm_num;
	   parm = DECL_CHAIN (parm))
	parm_num --;
      map->parm_num = streamer_read_uhwi (ib_main);
      map->old_tree = NULL;
      map->new_tree = stream_read_tree (ib_main, data_in);
      bp = streamer_read_bitpack (ib_main);
      map->replace_p = bp_unpack_value (&bp, 1);
      map->ref_p = bp_unpack_value (&bp, 1);
    }
  for (e = node->callees; e; e = e->next_callee)
    input_edge_opt_summary (e, ib_main);
  for (e = node->indirect_calls; e; e = e->next_callee)
    input_edge_opt_summary (e, ib_main);
}

/* Read section in file FILE_DATA of length LEN with data DATA.  */

static void
input_cgraph_opt_section (struct lto_file_decl_data *file_data,
			  const char *data, size_t len, VEC (cgraph_node_ptr,
							     heap) * nodes)
{
  const struct lto_function_header *header =
    (const struct lto_function_header *) data;
  const int cfg_offset = sizeof (struct lto_function_header);
  const int main_offset = cfg_offset + header->cfg_size;
  const int string_offset = main_offset + header->main_size;
  struct data_in *data_in;
  struct lto_input_block ib_main;
  unsigned int i;
  unsigned int count;

  LTO_INIT_INPUT_BLOCK (ib_main, (const char *) data + main_offset, 0,
			header->main_size);

  data_in =
    lto_data_in_create (file_data, (const char *) data + string_offset,
			header->string_size, NULL);
  count = streamer_read_uhwi (&ib_main);

  for (i = 0; i < count; i++)
    {
      int ref = streamer_read_uhwi (&ib_main);
      input_node_opt_summary (VEC_index (cgraph_node_ptr, nodes, ref),
			      &ib_main, data_in);
    }
  lto_free_section_data (file_data, LTO_section_cgraph_opt_sum, NULL, data,
			 len);
  lto_data_in_delete (data_in);
}

/* Input optimization summary of cgraph.  */

static void
input_cgraph_opt_summary (VEC (cgraph_node_ptr, heap) * nodes)
{
  struct lto_file_decl_data **file_data_vec = lto_get_file_decl_data ();
  struct lto_file_decl_data *file_data;
  unsigned int j = 0;

  while ((file_data = file_data_vec[j++]))
    {
      size_t len;
      const char *data =
	lto_get_section_data (file_data, LTO_section_cgraph_opt_sum, NULL,
			      &len);

      if (data)
	input_cgraph_opt_section (file_data, data, len, nodes);
    }
}
