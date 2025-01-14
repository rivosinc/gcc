/* ACLE support for Arm MVE (function shapes)
   Copyright (C) 2023 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "memmodel.h"
#include "insn-codes.h"
#include "optabs.h"
#include "arm-mve-builtins.h"
#include "arm-mve-builtins-shapes.h"

/* In the comments below, _t0 represents the first type suffix
   (e.g. "_s8") and _t1 represents the second.  T0/T1 represent the
   type full names (e.g. int8x16_t). Square brackets enclose
   characters that are present in only the full name, not the
   overloaded name.  Governing predicate arguments and predicate
   suffixes are not shown, since they depend on the predication type,
   which is a separate piece of information from the shape.  */

namespace arm_mve {

/* If INSTANCE has a predicate, add it to the list of argument types
   in ARGUMENT_TYPES.  RETURN_TYPE is the type returned by the
   function.  */
static void
apply_predication (const function_instance &instance, tree return_type,
		   vec<tree> &argument_types)
{
  if (instance.pred != PRED_none)
    {
      /* When predicate is PRED_m, insert a first argument
	 ("inactive") with the same type as return_type.  */
      if (instance.has_inactive_argument ())
	argument_types.quick_insert (0, return_type);
      argument_types.quick_push (get_mve_pred16_t ());
    }
}

/* Parse and move past an element type in FORMAT and return it as a type
   suffix.  The format is:

   [01]    - the element type in type suffix 0 or 1 of INSTANCE.
   h<elt>  - a half-sized version of <elt>
   s<bits> - a signed type with the given number of bits
   s[01]   - a signed type with the same width as type suffix 0 or 1
   u<bits> - an unsigned type with the given number of bits
   u[01]   - an unsigned type with the same width as type suffix 0 or 1
   w<elt>  - a double-sized version of <elt>
   x<bits> - a type with the given number of bits and same signedness
             as the next argument.

   Future intrinsics will extend this format.  */
static type_suffix_index
parse_element_type (const function_instance &instance, const char *&format)
{
  int ch = *format++;


  if (ch == 's' || ch == 'u')
    {
      type_class_index tclass = (ch == 'f' ? TYPE_float
				 : ch == 's' ? TYPE_signed
				 : TYPE_unsigned);
      char *end;
      unsigned int bits = strtol (format, &end, 10);
      format = end;
      if (bits == 0 || bits == 1)
	bits = instance.type_suffix (bits).element_bits;
      return find_type_suffix (tclass, bits);
    }

  if (ch == 'h')
    {
      type_suffix_index suffix = parse_element_type (instance, format);
      return find_type_suffix (type_suffixes[suffix].tclass,
			       type_suffixes[suffix].element_bits / 2);
    }

   if (ch == 'w')
    {
      type_suffix_index suffix = parse_element_type (instance, format);
      return find_type_suffix (type_suffixes[suffix].tclass,
			       type_suffixes[suffix].element_bits * 2);
    }

  if (ch == 'x')
    {
      const char *next = format;
      next = strstr (format, ",");
      next+=2;
      type_suffix_index suffix = parse_element_type (instance, next);
      type_class_index tclass = type_suffixes[suffix].tclass;
      char *end;
      unsigned int bits = strtol (format, &end, 10);
      format = end;
      return find_type_suffix (tclass, bits);
    }

  if (ch == '0' || ch == '1')
    return instance.type_suffix_ids[ch - '0'];

  gcc_unreachable ();
}

/* Read and return a type from FORMAT for function INSTANCE.  Advance
   FORMAT beyond the type string.  The format is:

   p       - predicates with type mve_pred16_t
   s<elt>  - a scalar type with the given element suffix
   t<elt>  - a vector or tuple type with given element suffix [*1]
   v<elt>  - a vector with the given element suffix

   where <elt> has the format described above parse_element_type.

   Future intrinsics will extend this format.

   [*1] the vectors_per_tuple function indicates whether the type should
        be a tuple, and if so, how many vectors it should contain.  */
static tree
parse_type (const function_instance &instance, const char *&format)
{
  int ch = *format++;

  if (ch == 'p')
    return get_mve_pred16_t ();

  if (ch == 's')
    {
      type_suffix_index suffix = parse_element_type (instance, format);
      return scalar_types[type_suffixes[suffix].vector_type];
    }

  if (ch == 't')
    {
      type_suffix_index suffix = parse_element_type (instance, format);
      vector_type_index vector_type = type_suffixes[suffix].vector_type;
      unsigned int num_vectors = instance.vectors_per_tuple ();
      return acle_vector_types[num_vectors - 1][vector_type];
    }

  if (ch == 'v')
    {
      type_suffix_index suffix = parse_element_type (instance, format);
      return acle_vector_types[0][type_suffixes[suffix].vector_type];
    }

  gcc_unreachable ();
}

/* Read a type signature for INSTANCE from FORMAT.  Add the argument
   types to ARGUMENT_TYPES and return the return type.  Assert there
   are no more than MAX_ARGS arguments.

   The format is a comma-separated list of types (as for parse_type),
   with the first type being the return type and the rest being the
   argument types.  */
static tree
parse_signature (const function_instance &instance, const char *format,
		 vec<tree> &argument_types, unsigned int max_args)
{
  tree return_type = parse_type (instance, format);
  unsigned int args = 0;
  while (format[0] == ',')
    {
      gcc_assert (args < max_args);
      format += 1;
      tree argument_type = parse_type (instance, format);
      argument_types.quick_push (argument_type);
      args += 1;
    }
  gcc_assert (format[0] == 0);
  return return_type;
}

/* Add one function instance for GROUP, using mode suffix MODE_SUFFIX_ID,
   the type suffixes at index TI and the predication suffix at index PI.
   The other arguments are as for build_all.  */
static void
build_one (function_builder &b, const char *signature,
	   const function_group_info &group, mode_suffix_index mode_suffix_id,
	   unsigned int ti, unsigned int pi, bool preserve_user_namespace,
	   bool force_direct_overloads)
{
  /* Current functions take at most five arguments.  Match
     parse_signature parameter below.  */
  auto_vec<tree, 5> argument_types;
  function_instance instance (group.base_name, *group.base, *group.shape,
			      mode_suffix_id, group.types[ti],
			      group.preds[pi]);
  tree return_type = parse_signature (instance, signature, argument_types, 5);
  apply_predication (instance, return_type, argument_types);
  b.add_unique_function (instance, return_type, argument_types,
			 preserve_user_namespace, group.requires_float,
			 force_direct_overloads);
}

/* Add a function instance for every type and predicate combination in
   GROUP, except if requested to use only the predicates listed in
   RESTRICT_TO_PREDS.  Take the function base name from GROUP and the
   mode suffix from MODE_SUFFIX_ID. Use SIGNATURE to construct the
   function signature, then use apply_predication to add in the
   predicate.  */
static void
build_all (function_builder &b, const char *signature,
	   const function_group_info &group, mode_suffix_index mode_suffix_id,
	   bool preserve_user_namespace,
	   bool force_direct_overloads = false,
	   const predication_index *restrict_to_preds = NULL)
{
  for (unsigned int pi = 0; group.preds[pi] != NUM_PREDS; ++pi)
    {
      unsigned int pi2 = 0;

      if (restrict_to_preds)
	for (; restrict_to_preds[pi2] != NUM_PREDS; ++pi2)
	  if (restrict_to_preds[pi2] == group.preds[pi])
	    break;

      if (restrict_to_preds == NULL || restrict_to_preds[pi2] != NUM_PREDS)
	for (unsigned int ti = 0;
	     ti == 0 || group.types[ti][0] != NUM_TYPE_SUFFIXES; ++ti)
	  build_one (b, signature, group, mode_suffix_id, ti, pi,
		     preserve_user_namespace, force_direct_overloads);
    }
}

/* Add a function instance for every type and predicate combination in
   GROUP, except if requested to use only the predicates listed in
   RESTRICT_TO_PREDS, and only for 16-bit and 32-bit integers.  Take
   the function base name from GROUP and the mode suffix from
   MODE_SUFFIX_ID. Use SIGNATURE to construct the function signature,
   then use apply_predication to add in the predicate.  */
static void
build_16_32 (function_builder &b, const char *signature,
	     const function_group_info &group, mode_suffix_index mode_suffix_id,
	     bool preserve_user_namespace,
	     bool force_direct_overloads = false,
	     const predication_index *restrict_to_preds = NULL)
{
  for (unsigned int pi = 0; group.preds[pi] != NUM_PREDS; ++pi)
    {
      unsigned int pi2 = 0;

      if (restrict_to_preds)
	for (; restrict_to_preds[pi2] != NUM_PREDS; ++pi2)
	  if (restrict_to_preds[pi2] == group.preds[pi])
	    break;

      if (restrict_to_preds == NULL || restrict_to_preds[pi2] != NUM_PREDS)
	for (unsigned int ti = 0;
	     ti == 0 || group.types[ti][0] != NUM_TYPE_SUFFIXES; ++ti)
	  {
	    unsigned int element_bits = type_suffixes[group.types[ti][0]].element_bits;
	    type_class_index tclass = type_suffixes[group.types[ti][0]].tclass;
	    if ((tclass == TYPE_signed || tclass == TYPE_unsigned)
		&& (element_bits == 16 || element_bits == 32))
	      build_one (b, signature, group, mode_suffix_id, ti, pi,
			 preserve_user_namespace, force_direct_overloads);
	  }
    }
}

/* Declare the function shape NAME, pointing it to an instance
   of class <NAME>_def.  */
#define SHAPE(NAME) \
  static CONSTEXPR const NAME##_def NAME##_obj; \
  namespace shapes { const function_shape *const NAME = &NAME##_obj; }

/* Base class for functions that are not overloaded.  */
struct nonoverloaded_base : public function_shape
{
  bool
  explicit_type_suffix_p (unsigned int, enum predication_index, enum mode_suffix_index) const override
  {
    return true;
  }

  bool
  explicit_mode_suffix_p (enum predication_index, enum mode_suffix_index) const override
  {
    return true;
  }

  bool
  skip_overload_p (enum predication_index, enum mode_suffix_index) const override
  {
    return false;
  }

  tree
  resolve (function_resolver &) const override
  {
    gcc_unreachable ();
  }
};

/* Base class for overloaded functions.  Bit N of EXPLICIT_MASK is true
   if type suffix N appears in the overloaded name.  */
template<unsigned int EXPLICIT_MASK>
struct overloaded_base : public function_shape
{
  bool
  explicit_type_suffix_p (unsigned int i, enum predication_index, enum mode_suffix_index) const override
  {
    return (EXPLICIT_MASK >> i) & 1;
  }

  bool
  explicit_mode_suffix_p (enum predication_index, enum mode_suffix_index) const override
  {
    return false;
  }

  bool
  skip_overload_p (enum predication_index, enum mode_suffix_index) const override
  {
    return false;
  }
};

/* <T0>_t vfoo[_t0](<T0>_t, <T0>_t)

   i.e. the standard shape for binary operations that operate on
   uniform types.

   Example: vandq.
   int8x16_t [__arm_]vandq[_s8](int8x16_t a, int8x16_t b)
   int8x16_t [__arm_]vandq_m[_s8](int8x16_t inactive, int8x16_t a, int8x16_t b, mve_pred16_t p)
   int8x16_t [__arm_]vandq_x[_s8](int8x16_t a, int8x16_t b, mve_pred16_t p)  */
struct binary_def : public overloaded_base<0>
{
  void
  build (function_builder &b, const function_group_info &group,
	 bool preserve_user_namespace) const override
  {
    b.add_overloaded_functions (group, MODE_none, preserve_user_namespace);
    build_all (b, "v0,v0,v0", group, MODE_none, preserve_user_namespace);
  }

  tree
  resolve (function_resolver &r) const override
  {
    return r.resolve_uniform (2);
  }
};
SHAPE (binary)

/* <T0>_t vfoo[_t0](<T0>_t, <T0>_t)
   <T0>_t vfoo[_n_t0](<T0>_t, <S0>_t)

   i.e. the standard shape for binary operations that operate on
   uniform types.

   Example: vaddq.
   int8x16_t [__arm_]vaddq[_s8](int8x16_t a, int8x16_t b)
   int8x16_t [__arm_]vaddq[_n_s8](int8x16_t a, int8_t b)
   int8x16_t [__arm_]vaddq_m[_s8](int8x16_t inactive, int8x16_t a, int8x16_t b, mve_pred16_t p)
   int8x16_t [__arm_]vaddq_m[_n_s8](int8x16_t inactive, int8x16_t a, int8_t b, mve_pred16_t p)
   int8x16_t [__arm_]vaddq_x[_s8](int8x16_t a, int8x16_t b, mve_pred16_t p)
   int8x16_t [__arm_]vaddq_x[_n_s8](int8x16_t a, int8_t b, mve_pred16_t p)  */
struct binary_opt_n_def : public overloaded_base<0>
{
  void
  build (function_builder &b, const function_group_info &group,
	 bool preserve_user_namespace) const override
  {
    b.add_overloaded_functions (group, MODE_none, preserve_user_namespace);
    build_all (b, "v0,v0,v0", group, MODE_none, preserve_user_namespace);
    build_all (b, "v0,v0,s0", group, MODE_n, preserve_user_namespace);
  }

  tree
  resolve (function_resolver &r) const override
  {
    return r.resolve_uniform_opt_n (2);
  }
};
SHAPE (binary_opt_n)

/* <T0>_t vfoo[t0](<T0>_t, <T0>_t)
   <T0>_t vfoo[_n_t0](<T0>_t, <S0>_t)

   Where the _n form only supports s16/s32/u16/u32 types as for vorrq.

   Example: vorrq.
   int16x8_t [__arm_]vorrq[_s16](int16x8_t a, int16x8_t b)
   int16x8_t [__arm_]vorrq_m[_s16](int16x8_t inactive, int16x8_t a, int16x8_t b, mve_pred16_t p)
   int16x8_t [__arm_]vorrq_x[_s16](int16x8_t a, int16x8_t b, mve_pred16_t p)
   int16x8_t [__arm_]vorrq[_n_s16](int16x8_t a, const int16_t imm)
   int16x8_t [__arm_]vorrq_m_n[_s16](int16x8_t a, const int16_t imm, mve_pred16_t p)  */
struct binary_orrq_def : public overloaded_base<0>
{
  bool
  explicit_mode_suffix_p (enum predication_index pred, enum mode_suffix_index mode) const override
  {
    return (mode == MODE_n
	    && pred == PRED_m);
  }

  bool
  skip_overload_p (enum predication_index pred, enum mode_suffix_index mode) const override
  {
    switch (mode)
      {
      case MODE_none:
	return false;

	/* For MODE_n, share the overloaded instance with MODE_none, except for PRED_m.  */
      case MODE_n:
	return pred != PRED_m;

      default:
	gcc_unreachable ();
      }
  }

  void
  build (function_builder &b, const function_group_info &group,
	 bool preserve_user_namespace) const override
  {
    b.add_overloaded_functions (group, MODE_none, preserve_user_namespace);
    b.add_overloaded_functions (group, MODE_n, preserve_user_namespace);
    build_all (b, "v0,v0,v0", group, MODE_none, preserve_user_namespace);
    build_16_32 (b, "v0,v0,s0", group, MODE_n, preserve_user_namespace, false, preds_m_or_none);
  }

  tree
  resolve (function_resolver &r) const override
  {
    unsigned int i, nargs;
    type_suffix_index type;
    if (!r.check_gp_argument (2, i, nargs)
	|| (type = r.infer_vector_type (0)) == NUM_TYPE_SUFFIXES)
      return error_mark_node;

    return r.finish_opt_n_resolution (i, 0, type);
  }
};
SHAPE (binary_orrq)

/* <T0>xN_t vfoo[_t0](uint64_t, uint64_t)

   where there are N arguments in total.
   Example: vcreateq.
   int16x8_t [__arm_]vcreateq_s16(uint64_t a, uint64_t b)  */
struct create_def : public nonoverloaded_base
{
  void
  build (function_builder &b, const function_group_info &group,
	 bool preserve_user_namespace) const override
  {
    build_all (b, "v0,su64,su64", group, MODE_none, preserve_user_namespace);
  }

  tree
  resolve (function_resolver &r) const override
  {
    return r.resolve_uniform (0, 2);
  }
};
SHAPE (create)

/* <T0>[xN]_t vfoo_t0().

   Example: vuninitializedq.
   int8x16_t [__arm_]vuninitializedq_s8(void)
   int8x16_t [__arm_]vuninitializedq(int8x16_t t)  */
struct inherent_def : public nonoverloaded_base
{
  void
  build (function_builder &b, const function_group_info &group,
	 bool preserve_user_namespace) const override
  {
    build_all (b, "t0", group, MODE_none, preserve_user_namespace);
  }
};
SHAPE (inherent)

/* <T0>_t foo_t0[_t1](<T1>_t)

   where the target type <t0> must be specified explicitly but the source
   type <t1> can be inferred.

   Example: vreinterpretq.
   int16x8_t [__arm_]vreinterpretq_s16[_s8](int8x16_t a)
   int32x4_t [__arm_]vreinterpretq_s32[_s8](int8x16_t a)
   int8x16_t [__arm_]vreinterpretq_s8[_s16](int16x8_t a)
   int8x16_t [__arm_]vreinterpretq_s8[_s32](int32x4_t a)  */
struct unary_convert_def : public overloaded_base<1>
{
  void
  build (function_builder &b, const function_group_info &group,
	 bool preserve_user_namespace) const override
  {
    b.add_overloaded_functions (group, MODE_none, preserve_user_namespace);
    build_all (b, "v0,v1", group, MODE_none, preserve_user_namespace);
  }

  tree
  resolve (function_resolver &r) const override
  {
    return r.resolve_unary ();
  }
};
SHAPE (unary_convert)

} /* end namespace arm_mve */

#undef SHAPE
