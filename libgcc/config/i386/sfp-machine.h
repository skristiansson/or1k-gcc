#ifdef __MINGW32__
  /* Make sure we are using gnu-style bitfield handling.  */
#define _FP_STRUCT_LAYOUT  __attribute__ ((gcc_struct))
#endif

/* The type of the result of a floating point comparison.  This must
   match `__libgcc_cmp_return__' in GCC for the target.  */
typedef int __gcc_CMPtype __attribute__ ((mode (__libgcc_cmp_return__)));
#define CMPtype __gcc_CMPtype

#ifdef __x86_64__
#include "config/i386/64/sfp-machine.h"
#else
#include "config/i386/32/sfp-machine.h"
#endif

#define _FP_KEEPNANFRACP 1

/* Here is something Intel misdesigned: the specs don't define
   the case where we have two NaNs with same mantissas, but
   different sign. Different operations pick up different NaNs.  */
#define _FP_CHOOSENAN(fs, wc, R, X, Y, OP)			\
  do {								\
    if (_FP_FRAC_GT_##wc(X, Y)					\
	|| (_FP_FRAC_EQ_##wc(X,Y) && (OP == '+' || OP == '*')))	\
      {								\
	R##_s = X##_s;						\
        _FP_FRAC_COPY_##wc(R,X);				\
      }								\
    else							\
      {								\
	R##_s = Y##_s;						\
        _FP_FRAC_COPY_##wc(R,Y);				\
      }								\
    R##_c = FP_CLS_NAN;						\
  } while (0)

#define FP_EX_INVALID		0x01
#define FP_EX_DENORM		0x02
#define FP_EX_DIVZERO		0x04
#define FP_EX_OVERFLOW		0x08
#define FP_EX_UNDERFLOW		0x10
#define FP_EX_INEXACT		0x20

#define FP_EX_MASK		0x3f

void __sfp_handle_exceptions (int);

#define FP_HANDLE_EXCEPTIONS			\
  do {						\
    if (_fex & FP_EX_MASK)			\
      __sfp_handle_exceptions (_fex);		\
  } while (0);

#define FP_RND_NEAREST		0
#define FP_RND_ZERO		0xc00
#define FP_RND_PINF		0x800
#define FP_RND_MINF		0x400

#define _FP_DECL_EX \
  unsigned short _fcw __attribute__ ((unused)) = FP_RND_NEAREST

#define FP_INIT_ROUNDMODE			\
  do {						\
    __asm__ ("fnstcw %0" : "=m" (_fcw));	\
  } while (0)

#define FP_ROUNDMODE		(_fcw & 0xc00)

#define	__LITTLE_ENDIAN	1234
#define	__BIG_ENDIAN	4321

#define __BYTE_ORDER __LITTLE_ENDIAN

/* Define ALIASNAME as a strong alias for NAME.  */
#if defined __MACH__
/* Mach-O doesn't support aliasing.  If these functions ever return
   anything but CMPtype we need to revisit this... */
#define strong_alias(name, aliasname) \
  CMPtype aliasname (TFtype a, TFtype b) { return name(a, b); }
#else
# define strong_alias(name, aliasname) _strong_alias(name, aliasname)
# define _strong_alias(name, aliasname) \
  extern __typeof (name) aliasname __attribute__ ((alias (#name)));
#endif
