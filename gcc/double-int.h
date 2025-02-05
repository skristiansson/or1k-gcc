/* Operations with long integers.
   Copyright (C) 2006, 2007, 2008, 2010, 2012 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

GCC is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef DOUBLE_INT_H
#define DOUBLE_INT_H

#ifndef GENERATOR_FILE
#include <gmp.h>
#endif
#include "coretypes.h"

/* A large integer is currently represented as a pair of HOST_WIDE_INTs.
   It therefore represents a number with precision of
   2 * HOST_BITS_PER_WIDE_INT bits (it is however possible that the
   internal representation will change, if numbers with greater precision
   are needed, so the users should not rely on it).  The representation does
   not contain any information about signedness of the represented value, so
   it can be used to represent both signed and unsigned numbers.  For
   operations where the results depend on signedness (division, comparisons),
   it must be specified separately.  For each such operation, there are three
   versions of the function -- double_int_op, that takes an extra UNS argument
   giving the signedness of the values, and double_int_sop and double_int_uop
   that stand for its specializations for signed and unsigned values.

   You may also represent with numbers in smaller precision using double_int.
   You however need to use double_int_ext (that fills in the bits of the
   number over the prescribed precision with zeros or with the sign bit) before
   operations that do not perform arithmetics modulo 2^precision (comparisons,
   division), and possibly before storing the results, if you want to keep
   them in some canonical form).  In general, the signedness of double_int_ext
   should match the signedness of the operation.

   ??? The components of double_int differ in signedness mostly for
   historical reasons (they replace an older structure used to represent
   numbers with precision higher than HOST_WIDE_INT).  It might be less
   confusing to have them both signed or both unsigned.  */

typedef struct
{
  unsigned HOST_WIDE_INT low;
  HOST_WIDE_INT high;
} double_int;

#define HOST_BITS_PER_DOUBLE_INT (2 * HOST_BITS_PER_WIDE_INT)

/* Constructors and conversions.  */

/* Constructs double_int from integer CST.  The bits over the precision of
   HOST_WIDE_INT are filled with the sign bit.  */

static inline double_int
shwi_to_double_int (HOST_WIDE_INT cst)
{
  double_int r;

  r.low = (unsigned HOST_WIDE_INT) cst;
  r.high = cst < 0 ? -1 : 0;

  return r;
}

/* Some useful constants.  */

#define double_int_minus_one (shwi_to_double_int (-1))
#define double_int_zero (shwi_to_double_int (0))
#define double_int_one (shwi_to_double_int (1))
#define double_int_two (shwi_to_double_int (2))
#define double_int_ten (shwi_to_double_int (10))

/* Constructs double_int from unsigned integer CST.  The bits over the
   precision of HOST_WIDE_INT are filled with zeros.  */

static inline double_int
uhwi_to_double_int (unsigned HOST_WIDE_INT cst)
{
  double_int r;

  r.low = cst;
  r.high = 0;

  return r;
}

/* Returns value of CST as a signed number.  CST must satisfy
   double_int_fits_in_shwi_p.  */

static inline HOST_WIDE_INT
double_int_to_shwi (double_int cst)
{
  return (HOST_WIDE_INT) cst.low;
}

/* Returns value of CST as an unsigned number.  CST must satisfy
   double_int_fits_in_uhwi_p.  */

static inline unsigned HOST_WIDE_INT
double_int_to_uhwi (double_int cst)
{
  return cst.low;
}

bool double_int_fits_in_hwi_p (double_int, bool);
bool double_int_fits_in_shwi_p (double_int);

/* Returns true if CST fits in unsigned HOST_WIDE_INT.  */

static inline bool
double_int_fits_in_uhwi_p (double_int cst)
{
  return cst.high == 0;
}

/* The following operations perform arithmetics modulo 2^precision,
   so you do not need to call double_int_ext between them, even if
   you are representing numbers with precision less than
   HOST_BITS_PER_DOUBLE_INT bits.  */

double_int double_int_mul (double_int, double_int);
double_int double_int_mul_with_sign (double_int, double_int, bool, int *);
double_int double_int_add (double_int, double_int);
double_int double_int_sub (double_int, double_int);
double_int double_int_neg (double_int);

/* You must ensure that double_int_ext is called on the operands
   of the following operations, if the precision of the numbers
   is less than HOST_BITS_PER_DOUBLE_INT bits.  */
double_int double_int_div (double_int, double_int, bool, unsigned);
double_int double_int_sdiv (double_int, double_int, unsigned);
double_int double_int_udiv (double_int, double_int, unsigned);
double_int double_int_mod (double_int, double_int, bool, unsigned);
double_int double_int_smod (double_int, double_int, unsigned);
double_int double_int_umod (double_int, double_int, unsigned);
double_int double_int_divmod (double_int, double_int, bool, unsigned, double_int *);
double_int double_int_sdivmod (double_int, double_int, unsigned, double_int *);
double_int double_int_udivmod (double_int, double_int, unsigned, double_int *);

double_int double_int_setbit (double_int, unsigned);
int double_int_ctz (double_int);

/* Logical operations.  */

/* Returns ~A.  */

static inline double_int
double_int_not (double_int a)
{
  a.low = ~a.low;
  a.high = ~a.high;
  return a;
}

/* Returns A | B.  */

static inline double_int
double_int_ior (double_int a, double_int b)
{
  a.low |= b.low;
  a.high |= b.high;
  return a;
}

/* Returns A & B.  */

static inline double_int
double_int_and (double_int a, double_int b)
{
  a.low &= b.low;
  a.high &= b.high;
  return a;
}

/* Returns A & ~B.  */

static inline double_int
double_int_and_not (double_int a, double_int b)
{
  a.low &= ~b.low;
  a.high &= ~b.high;
  return a;
}

/* Returns A ^ B.  */

static inline double_int
double_int_xor (double_int a, double_int b)
{
  a.low ^= b.low;
  a.high ^= b.high;
  return a;
}


/* Shift operations.  */
double_int double_int_lshift (double_int, HOST_WIDE_INT, unsigned int, bool);
double_int double_int_rshift (double_int, HOST_WIDE_INT, unsigned int, bool);
double_int double_int_lrotate (double_int, HOST_WIDE_INT, unsigned int);
double_int double_int_rrotate (double_int, HOST_WIDE_INT, unsigned int);

/* Returns true if CST is negative.  Of course, CST is considered to
   be signed.  */

static inline bool
double_int_negative_p (double_int cst)
{
  return cst.high < 0;
}

int double_int_cmp (double_int, double_int, bool);
int double_int_scmp (double_int, double_int);
int double_int_ucmp (double_int, double_int);

double_int double_int_max (double_int, double_int, bool);
double_int double_int_smax (double_int, double_int);
double_int double_int_umax (double_int, double_int);

double_int double_int_min (double_int, double_int, bool);
double_int double_int_smin (double_int, double_int);
double_int double_int_umin (double_int, double_int);

void dump_double_int (FILE *, double_int, bool);

/* Zero and sign extension of numbers in smaller precisions.  */

double_int double_int_ext (double_int, unsigned, bool);
double_int double_int_sext (double_int, unsigned);
double_int double_int_zext (double_int, unsigned);
double_int double_int_mask (unsigned);

double_int double_int_max_value (unsigned int, bool);
double_int double_int_min_value (unsigned int, bool);

#define ALL_ONES (~((unsigned HOST_WIDE_INT) 0))

/* The operands of the following comparison functions must be processed
   with double_int_ext, if their precision is less than
   HOST_BITS_PER_DOUBLE_INT bits.  */

/* Returns true if CST is zero.  */

static inline bool
double_int_zero_p (double_int cst)
{
  return cst.low == 0 && cst.high == 0;
}

/* Returns true if CST is one.  */

static inline bool
double_int_one_p (double_int cst)
{
  return cst.low == 1 && cst.high == 0;
}

/* Returns true if CST is minus one.  */

static inline bool
double_int_minus_one_p (double_int cst)
{
  return (cst.low == ALL_ONES && cst.high == -1);
}

/* Returns true if CST1 == CST2.  */

static inline bool
double_int_equal_p (double_int cst1, double_int cst2)
{
  return cst1.low == cst2.low && cst1.high == cst2.high;
}


/* Legacy interface with decomposed high/low parts.  */

extern int add_double_with_sign (unsigned HOST_WIDE_INT, HOST_WIDE_INT,
				 unsigned HOST_WIDE_INT, HOST_WIDE_INT,
				 unsigned HOST_WIDE_INT *, HOST_WIDE_INT *,
				 bool);
#define add_double(l1,h1,l2,h2,lv,hv) \
  add_double_with_sign (l1, h1, l2, h2, lv, hv, false)
extern int neg_double (unsigned HOST_WIDE_INT, HOST_WIDE_INT,
		       unsigned HOST_WIDE_INT *, HOST_WIDE_INT *);
extern int mul_double_with_sign (unsigned HOST_WIDE_INT, HOST_WIDE_INT,
				 unsigned HOST_WIDE_INT, HOST_WIDE_INT,
				 unsigned HOST_WIDE_INT *, HOST_WIDE_INT *,
				 bool);
#define mul_double(l1,h1,l2,h2,lv,hv) \
  mul_double_with_sign (l1, h1, l2, h2, lv, hv, false)
extern void lshift_double (unsigned HOST_WIDE_INT, HOST_WIDE_INT,
			   HOST_WIDE_INT, unsigned int,
			   unsigned HOST_WIDE_INT *, HOST_WIDE_INT *, bool);
extern int div_and_round_double (unsigned, int, unsigned HOST_WIDE_INT,
				 HOST_WIDE_INT, unsigned HOST_WIDE_INT,
				 HOST_WIDE_INT, unsigned HOST_WIDE_INT *,
				 HOST_WIDE_INT *, unsigned HOST_WIDE_INT *,
				 HOST_WIDE_INT *);


#ifndef GENERATOR_FILE
/* Conversion to and from GMP integer representations.  */

void mpz_set_double_int (mpz_t, double_int, bool);
double_int mpz_get_double_int (const_tree, mpz_t, bool);
#endif

#endif /* DOUBLE_INT_H */
