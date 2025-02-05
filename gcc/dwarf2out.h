/* dwarf2out.h - Various declarations for functions found in dwarf2out.c
   Copyright (C) 1998, 1999, 2000, 2003, 2007, 2010, 2011
   Free Software Foundation, Inc.

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

#ifndef GCC_DWARF2OUT_H
#define GCC_DWARF2OUT_H 1

#include "dwarf2.h"	/* ??? Remove this once only used by dwarf2foo.c.  */

typedef struct die_struct *dw_die_ref;
typedef const struct die_struct *const_dw_die_ref;

typedef struct dw_val_struct *dw_val_ref;
typedef struct dw_cfi_struct *dw_cfi_ref;
typedef struct dw_loc_descr_struct *dw_loc_descr_ref;
typedef struct dw_loc_list_struct *dw_loc_list_ref;


/* Call frames are described using a sequence of Call Frame
   Information instructions.  The register number, offset
   and address fields are provided as possible operands;
   their use is selected by the opcode field.  */

enum dw_cfi_oprnd_type {
  dw_cfi_oprnd_unused,
  dw_cfi_oprnd_reg_num,
  dw_cfi_oprnd_offset,
  dw_cfi_oprnd_addr,
  dw_cfi_oprnd_loc
};

typedef union GTY(()) dw_cfi_oprnd_struct {
  unsigned int GTY ((tag ("dw_cfi_oprnd_reg_num"))) dw_cfi_reg_num;
  HOST_WIDE_INT GTY ((tag ("dw_cfi_oprnd_offset"))) dw_cfi_offset;
  const char * GTY ((tag ("dw_cfi_oprnd_addr"))) dw_cfi_addr;
  struct dw_loc_descr_struct * GTY ((tag ("dw_cfi_oprnd_loc"))) dw_cfi_loc;
}
dw_cfi_oprnd;

typedef struct GTY(()) dw_cfi_struct {
  enum dwarf_call_frame_info dw_cfi_opc;
  dw_cfi_oprnd GTY ((desc ("dw_cfi_oprnd1_desc (%1.dw_cfi_opc)")))
    dw_cfi_oprnd1;
  dw_cfi_oprnd GTY ((desc ("dw_cfi_oprnd2_desc (%1.dw_cfi_opc)")))
    dw_cfi_oprnd2;
}
dw_cfi_node;

DEF_VEC_P (dw_cfi_ref);
DEF_VEC_ALLOC_P (dw_cfi_ref, heap);
DEF_VEC_ALLOC_P (dw_cfi_ref, gc);

typedef VEC(dw_cfi_ref, gc) *cfi_vec;

typedef struct dw_fde_struct *dw_fde_ref;

/* All call frame descriptions (FDE's) in the GCC generated DWARF
   refer to a single Common Information Entry (CIE), defined at
   the beginning of the .debug_frame section.  This use of a single
   CIE obviates the need to keep track of multiple CIE's
   in the DWARF generation routines below.  */

typedef struct GTY(()) dw_fde_struct {
  tree decl;
  const char *dw_fde_begin;
  const char *dw_fde_current_label;
  const char *dw_fde_end;
  const char *dw_fde_vms_end_prologue;
  const char *dw_fde_vms_begin_epilogue;
  const char *dw_fde_second_begin;
  const char *dw_fde_second_end;
  cfi_vec dw_fde_cfi;
  int dw_fde_switch_cfi_index; /* Last CFI before switching sections.  */
  HOST_WIDE_INT stack_realignment;

  unsigned funcdef_number;
  unsigned fde_index;

  /* Dynamic realign argument pointer register.  */
  unsigned int drap_reg;
  /* Virtual dynamic realign argument pointer register.  */
  unsigned int vdrap_reg;
  /* These 3 flags are copied from rtl_data in function.h.  */
  unsigned all_throwers_are_sibcalls : 1;
  unsigned uses_eh_lsda : 1;
  unsigned nothrow : 1;
  /* Whether we did stack realign in this call frame.  */
  unsigned stack_realign : 1;
  /* Whether dynamic realign argument pointer register has been saved.  */
  unsigned drap_reg_saved: 1;
  /* True iff dw_fde_begin label is in text_section or cold_text_section.  */
  unsigned in_std_section : 1;
  /* True iff dw_fde_second_begin label is in text_section or
     cold_text_section.  */
  unsigned second_in_std_section : 1;
}
dw_fde_node;


/* This is how we define the location of the CFA. We use to handle it
   as REG + OFFSET all the time,  but now it can be more complex.
   It can now be either REG + CFA_OFFSET or *(REG + BASE_OFFSET) + CFA_OFFSET.
   Instead of passing around REG and OFFSET, we pass a copy
   of this structure.  */
typedef struct GTY(()) cfa_loc {
  HOST_WIDE_INT offset;
  HOST_WIDE_INT base_offset;
  /* REG is in DWARF_FRAME_REGNUM space, *not* normal REGNO space.  */
  unsigned int reg;
  BOOL_BITFIELD indirect : 1;  /* 1 if CFA is accessed via a dereference.  */
  BOOL_BITFIELD in_use : 1;    /* 1 if a saved cfa is stored here.  */
} dw_cfa_location;


/* Each DIE may have a series of attribute/value pairs.  Values
   can take on several forms.  The forms that are used in this
   implementation are listed below.  */

enum dw_val_class
{
  dw_val_class_none,
  dw_val_class_addr,
  dw_val_class_offset,
  dw_val_class_loc,
  dw_val_class_loc_list,
  dw_val_class_range_list,
  dw_val_class_const,
  dw_val_class_unsigned_const,
  dw_val_class_const_double,
  dw_val_class_vec,
  dw_val_class_flag,
  dw_val_class_die_ref,
  dw_val_class_fde_ref,
  dw_val_class_lbl_id,
  dw_val_class_lineptr,
  dw_val_class_str,
  dw_val_class_macptr,
  dw_val_class_file,
  dw_val_class_data8,
  dw_val_class_decl_ref,
  dw_val_class_vms_delta
};

/* Describe a floating point constant value, or a vector constant value.  */

typedef struct GTY(()) dw_vec_struct {
  unsigned char * GTY((length ("%h.length"))) array;
  unsigned length;
  unsigned elt_size;
}
dw_vec_const;

/* The dw_val_node describes an attribute's value, as it is
   represented internally.  */

typedef struct GTY(()) dw_val_struct {
  enum dw_val_class val_class;
  union dw_val_struct_union
    {
      rtx GTY ((tag ("dw_val_class_addr"))) val_addr;
      unsigned HOST_WIDE_INT GTY ((tag ("dw_val_class_offset"))) val_offset;
      dw_loc_list_ref GTY ((tag ("dw_val_class_loc_list"))) val_loc_list;
      dw_loc_descr_ref GTY ((tag ("dw_val_class_loc"))) val_loc;
      HOST_WIDE_INT GTY ((default)) val_int;
      unsigned HOST_WIDE_INT GTY ((tag ("dw_val_class_unsigned_const"))) val_unsigned;
      double_int GTY ((tag ("dw_val_class_const_double"))) val_double;
      dw_vec_const GTY ((tag ("dw_val_class_vec"))) val_vec;
      struct dw_val_die_union
	{
	  dw_die_ref die;
	  int external;
	} GTY ((tag ("dw_val_class_die_ref"))) val_die_ref;
      unsigned GTY ((tag ("dw_val_class_fde_ref"))) val_fde_index;
      struct indirect_string_node * GTY ((tag ("dw_val_class_str"))) val_str;
      char * GTY ((tag ("dw_val_class_lbl_id"))) val_lbl_id;
      unsigned char GTY ((tag ("dw_val_class_flag"))) val_flag;
      struct dwarf_file_data * GTY ((tag ("dw_val_class_file"))) val_file;
      unsigned char GTY ((tag ("dw_val_class_data8"))) val_data8[8];
      tree GTY ((tag ("dw_val_class_decl_ref"))) val_decl_ref;
      struct dw_val_vms_delta_union
	{
	  char * lbl1;
	  char * lbl2;
	} GTY ((tag ("dw_val_class_vms_delta"))) val_vms_delta;
    }
  GTY ((desc ("%1.val_class"))) v;
}
dw_val_node;

/* Locations in memory are described using a sequence of stack machine
   operations.  */

typedef struct GTY(()) dw_loc_descr_struct {
  dw_loc_descr_ref dw_loc_next;
  ENUM_BITFIELD (dwarf_location_atom) dw_loc_opc : 8;
  /* Used to distinguish DW_OP_addr with a direct symbol relocation
     from DW_OP_addr with a dtp-relative symbol relocation.  */
  unsigned int dtprel : 1;
  int dw_loc_addr;
  dw_val_node dw_loc_oprnd1;
  dw_val_node dw_loc_oprnd2;
}
dw_loc_descr_node;


/* Interface from dwarf2out.c to dwarf2cfi.c.  */
extern struct dw_loc_descr_struct *build_cfa_loc
  (dw_cfa_location *, HOST_WIDE_INT);
extern struct dw_loc_descr_struct *build_cfa_aligned_loc
  (dw_cfa_location *, HOST_WIDE_INT offset, HOST_WIDE_INT alignment);
extern struct dw_loc_descr_struct *mem_loc_descriptor
  (rtx, enum machine_mode mode, enum machine_mode mem_mode,
   enum var_init_status);
extern bool loc_descr_equal_p (dw_loc_descr_ref, dw_loc_descr_ref);
extern dw_fde_ref dwarf2out_alloc_current_fde (void);

extern unsigned long size_of_locs (dw_loc_descr_ref);
extern void output_loc_sequence (dw_loc_descr_ref, int);
extern void output_loc_sequence_raw (dw_loc_descr_ref);

/* Interface from dwarf2cfi.c to dwarf2out.c.  */
extern void lookup_cfa_1 (dw_cfi_ref cfi, dw_cfa_location *loc,
			  dw_cfa_location *remember);
extern bool cfa_equal_p (const dw_cfa_location *, const dw_cfa_location *);

extern void output_cfi (dw_cfi_ref, dw_fde_ref, int);

extern GTY(()) cfi_vec cie_cfi_vec;

/* Interface from dwarf2*.c to the rest of the compiler.  */
extern enum dw_cfi_oprnd_type dw_cfi_oprnd1_desc
  (enum dwarf_call_frame_info cfi);
extern enum dw_cfi_oprnd_type dw_cfi_oprnd2_desc
  (enum dwarf_call_frame_info cfi);

extern void output_cfi_directive (FILE *f, struct dw_cfi_struct *cfi);

extern void dwarf2out_decl (tree);
extern void dwarf2out_emit_cfi (dw_cfi_ref cfi);

extern void debug_dwarf (void);
struct die_struct;
extern void debug_dwarf_die (struct die_struct *);
extern void dwarf2out_set_demangle_name_func (const char *(*) (const char *));
#ifdef VMS_DEBUGGING_INFO
extern void dwarf2out_vms_debug_main_pointer (void);
#endif

struct array_descr_info
{
  int ndimensions;
  tree element_type;
  tree base_decl;
  tree data_location;
  tree allocated;
  tree associated;
  struct array_descr_dimen
    {
      tree lower_bound;
      tree upper_bound;
      tree stride;
    } dimen[10];
};

#endif /* GCC_DWARF2OUT_H */
