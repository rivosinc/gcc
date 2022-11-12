/*===------ riscv-cfi.h -Control-flow Integrity feature --------------------===
 * Add RISCV feature with landing pads (forward) and/or shadow
 * stack (backward) CFI bits to ELF program property if they are
 * enabled. Otherwise, contents in this header file are unused. This file is
 * mainly design for assembly source code which want to enable CFI.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __RISCV_ZISSLPCFI_H
#define __RISCV_ZISSLPCFI_H

#define ZISSLPCFI_LP_KIND_CHECK0 0 /* check 0 only. lplr=0 once at startup.  */
#define ZISSLPCFI_LP_KIND_SET0 1   /* set 0 & check 0 for each indirect call.  */
#define ZISSLPCFI_LP_KIND_TYPE 2
#define ZISSLPCFI_LP_KIND_CFG 3

#define ZISSLPCFI_LP_WIDTH(N) ((N) & 3)
#define ZISSLPCFI_LP_KIND(N) (((N) >> 2) & 3)
#define ZISSLPCFI_SS(N) (((N) >> 4) & 1)
#define ZISSLPCFI_ENCODE_ATTRIBUTE(lp_width, lp_kind, ss) \
  ((lp_width) | ((lp_kind) << 2) | ((ss) << 4))

#define ZISSLPCFI_LP_LABEL_LOWER(n) ((n) & 0x1ff)
#define ZISSLPCFI_LP_LABEL_MIDDLE(n) (((n) >> 9) & 0xff)
#define ZISSLPCFI_LP_LABEL_UPPER(n) (((n) >> 17) & 0xff)
#define ZISSLPCFI_LP_ENCODE_LABEL_FIELDS(lower, middle, upper) \
  ((lower) | ((middle) << 9) | ((upper) << 17))
#define ZISSLPCFI_LP_ENCODE_LABEL_BYTES(b0, b1, b2, b3) \
  ((b0) | ((b1) << 8) | ((b2) << 16) | ((b3) << 24))

#define ZISSLPCFI_LP_LABEL_MASK ((1 << 25) - 1)

#ifdef __ASSEMBLER__
# if __RISCV_ZISSLPCFI__

.attribute zisslpcfi, __RISCV_ZISSLPCFI__

#  define ZISSLPCFI_LP(f) .zisslpcfi_lp f

#  if ZISSLPCFI_LP_KIND (__RISCV_ZISSLPCFI__) <= ZISSLPCFI_LP_KIND_SET0
#   define _ZISSLPCFI_LP_LABEL_OR_0(n) 0
#  else
#   define _ZISSLPCFI_LP_LABEL_OR_0(n) (n)
#  endif

#  if ZISSLPCFI_LP_WIDTH (__RISCV_ZISSLPCFI__) == 3
#   if ZISSLPCFI_LP_KIND (__RISCV_ZISSLPCFI__) >= ZISSLPCFI_LP_KIND_SET0
#    define _ZISSLPCFI_LPSUL(label) \
	    lpsul ZISSLPCFI_LP_LABEL_UPPER (_ZISSLPCFI_LP_LABEL_OR_0 (label))
#   endif
#   define _ZISSLPCFI_LPCUL(label) \
	   lpcul ZISSLPCFI_LP_LABEL_UPPER (_ZISSLPCFI_LP_LABEL_OR_0 (label))
#  endif

#  if ZISSLPCFI_LP_WIDTH (__RISCV_ZISSLPCFI__) >= 2
#   if ZISSLPCFI_LP_KIND (__RISCV_ZISSLPCFI__) >= ZISSLPCFI_LP_KIND_SET0
#    define _ZISSLPCFI_LPSML(label) \
	    lpsml ZISSLPCFI_LP_LABEL_MIDDLE (_ZISSLPCFI_LP_LABEL_OR_0 (label))
#   endif
#   define _ZISSLPCFI_LPCML(label) \
	   lpcml ZISSLPCFI_LP_LABEL_MIDDLE (_ZISSLPCFI_LP_LABEL_OR_0 (label))
#  endif

#  if ZISSLPCFI_LP_WIDTH (__RISCV_ZISSLPCFI__) >= 1
#   if ZISSLPCFI_LP_KIND (__RISCV_ZISSLPCFI__) >= ZISSLPCFI_LP_KIND_SET0
#    define _ZISSLPCFI_LPSLL(label) \
	    lpsll ZISSLPCFI_LP_LABEL_LOWER (_ZISSLPCFI_LP_LABEL_OR_0 (label))
#   endif
#   define _ZISSLPCFI_LPCLL(label) .p2align 2; \
	   lpcll ZISSLPCFI_LP_LABEL_LOWER (_ZISSLPCFI_LP_LABEL_OR_0 (label))
#  endif

#  if ZISSLPCFI_SS (__RISCV_ZISSLPCFI__)
#   define ZISSLPCFI_SS_CHECK sschkra
#   define ZISSLPCFI_SS_PUSH(reg) sspush reg
#   define ZISSLPCFI_SS_POP(reg) sspop reg
#  endif

# endif // __RISCV_ZISSLPCFI__

/* ZISSLPCFI_LP_SET (label) and ZISSLPCFI_LP_CHECK (label) accept 25 bits of
   label and expand to 1/2/3 label-setting and -checking instructions requested
   by the user according to the +1, +2, +3 label-width suboptions of
   -mzisslpcfi=+...  */

# ifdef _ZISSLPCFI_LPSUL
#  define ZISSLPCFI_LP_SET_BYTES(b0, b1, b2, b3) \
	  _ZISSLPCFI_LPSLL (ZISSLPCFI_LP_ENCODE_LABEL_BYTES (b0, b1, b2, b3)); \
	  _ZISSLPCFI_LPSML (ZISSLPCFI_LP_ENCODE_LABEL_BYTES (b0, b1, b2, b3)); \
	  _ZISSLPCFI_LPSUL (ZISSLPCFI_LP_ENCODE_LABEL_BYTES (b0, b1, b2, b3))
#  define ZISSLPCFI_LP_SET_FIELDS(lower, middle, upper) \
	  _ZISSLPCFI_LPSLL (lower); \
	  _ZISSLPCFI_LPSML (middle); \
	  _ZISSLPCFI_LPSUL (upper)
# elif defined _ZISSLPCFI_LPSML
#  define ZISSLPCFI_LP_SET_BYTES(b0, b1, b2, b3) \
	  _ZISSLPCFI_LPSLL (ZISSLPCFI_LP_ENCODE_LABEL_BYTES (b0, b1, b2, b3)); \
	  _ZISSLPCFI_LPSML (ZISSLPCFI_LP_ENCODE_LABEL_BYTES (b0, b1, b2, b3))
#  define ZISSLPCFI_LP_SET_FIELDS(lower, middle, upper) \
	  _ZISSLPCFI_LPSLL (lower); \
	  _ZISSLPCFI_LPSML (middle)
# elif defined _ZISSLPCFI_LPSLL
#  define ZISSLPCFI_LP_SET_BYTES(b0, b1, b2, b3) \
	  _ZISSLPCFI_LPSLL (ZISSLPCFI_LP_ENCODE_LABEL_BYTES (b0, b1, b2, b3))
#  define ZISSLPCFI_LP_SET_FIELDS(lower, middle, upper) \
	  _ZISSLPCFI_LPSLL (lower)
# else
#  define ZISSLPCFI_LP_SET_BYTES(b0, b1, b2, b3)
#  define ZISSLPCFI_LP_SET_FIELDS(lower, middle, upper)
# endif

# ifdef _ZISSLPCFI_LPCUL
#  define ZISSLPCFI_LP_CHECK_BYTES(b0, b1, b2, b3) \
	  _ZISSLPCFI_LPCLL (ZISSLPCFI_LP_ENCODE_LABEL_BYTES (b0, b1, b2, b3)); \
	  _ZISSLPCFI_LPCML (ZISSLPCFI_LP_ENCODE_LABEL_BYTES (b0, b1, b2, b3)); \
	  _ZISSLPCFI_LPCUL (ZISSLPCFI_LP_ENCODE_LABEL_BYTES (b0, b1, b2, b3))
#  define ZISSLPCFI_LP_CHECK_FIELDS(lower, middle, upper) \
	  _ZISSLPCFI_LPCLL (lower); \
	  _ZISSLPCFI_LPCML (middle); \
	  _ZISSLPCFI_LPCUL (upper)

# elif defined _ZISSLPCFI_LPCML
#  define ZISSLPCFI_LP_CHECK_BYTES(b0, b1, b2, b3) \
	  _ZISSLPCFI_LPCLL (ZISSLPCFI_LP_ENCODE_LABEL_BYTES (b0, b1, b2, b3)); \
	  _ZISSLPCFI_LPCML (ZISSLPCFI_LP_ENCODE_LABEL_BYTES (b0, b1, b2, b3))
#  define ZISSLPCFI_LP_CHECK_FIELDS(lower, middle, upper) \
	  _ZISSLPCFI_LPCLL (lower); \
	  _ZISSLPCFI_LPCML (middle)

# elif defined _ZISSLPCFI_LPCLL
#  define ZISSLPCFI_LP_CHECK_BYTES(b0, b1, b2, b3) \
	  _ZISSLPCFI_LPCLL (ZISSLPCFI_LP_ENCODE_LABEL_BYTES (b0, b1, b2, b3))
#  define ZISSLPCFI_LP_CHECK_FIELDS(lower, middle, upper) \
	  _ZISSLPCFI_LPCLL (lower)

# else
#  define ZISSLPCFI_LP_CHECK_BYTES(b0, b1, b2, b3)
#  define ZISSLPCFI_LP_CHECK_FIELDS(lower, middle, upper)
# endif

# ifndef ZISSLPCFI_SS_CHECK
#  define ZISSLPCFI_SS_CHECK
# endif
# ifndef ZISSLPCFI_SS_PUSH
#  define ZISSLPCFI_SS_PUSH(reg)
# endif
# ifndef ZISSLPCFI_SS_POP
#  define ZISSLPCFI_SS_POP(reg)
# endif
# ifndef ZISSLPCFI_LP
#  define ZISSLPCFI_LP(f)
# endif

#endif // __ASSEMBLER__
#endif // __RISCV_ZISSLPCFI_H
