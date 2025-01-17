/* ACLE support for Arm MVE (function_base classes)
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

#ifndef GCC_ARM_MVE_BUILTINS_FUNCTIONS_H
#define GCC_ARM_MVE_BUILTINS_FUNCTIONS_H

namespace arm_mve {

/* Wrap T, which is derived from function_base, and indicate that the
   function never has side effects.  It is only necessary to use this
   wrapper on functions that might have floating-point suffixes, since
   otherwise we assume by default that the function has no side effects.  */
template<typename T>
class quiet : public T
{
public:
  CONSTEXPR quiet () : T () {}

  unsigned int
  call_properties (const function_instance &) const override
  {
    return 0;
  }
};

/* An incomplete function_base for functions that have an associated
   rtx_code for signed integers, unsigned integers and floating-point
   values for the non-predicated, non-suffixed intrinsic, and unspec
   codes, with separate codes for signed integers, unsigned integers
   and floating-point values.  The class simply records information
   about the mapping for derived classes to use.  */
class unspec_based_mve_function_base : public function_base
{
public:
  CONSTEXPR unspec_based_mve_function_base (rtx_code code_for_sint,
					    rtx_code code_for_uint,
					    rtx_code code_for_fp,
					    int unspec_for_n_sint,
					    int unspec_for_n_uint,
					    int unspec_for_n_fp,
					    int unspec_for_m_sint,
					    int unspec_for_m_uint,
					    int unspec_for_m_fp,
					    int unspec_for_m_n_sint,
					    int unspec_for_m_n_uint,
					    int unspec_for_m_n_fp)
    : m_code_for_sint (code_for_sint),
      m_code_for_uint (code_for_uint),
      m_code_for_fp (code_for_fp),
      m_unspec_for_n_sint (unspec_for_n_sint),
      m_unspec_for_n_uint (unspec_for_n_uint),
      m_unspec_for_n_fp (unspec_for_n_fp),
      m_unspec_for_m_sint (unspec_for_m_sint),
      m_unspec_for_m_uint (unspec_for_m_uint),
      m_unspec_for_m_fp (unspec_for_m_fp),
      m_unspec_for_m_n_sint (unspec_for_m_n_sint),
      m_unspec_for_m_n_uint (unspec_for_m_n_uint),
      m_unspec_for_m_n_fp (unspec_for_m_n_fp)
  {}

  /* The rtx code to use for signed, unsigned integers and
     floating-point values respectively.  */
  rtx_code m_code_for_sint;
  rtx_code m_code_for_uint;
  rtx_code m_code_for_fp;

  /* The unspec code associated with signed-integer, unsigned-integer
     and floating-point operations respectively.  It covers the cases
     with the _n suffix, and/or the _m predicate.  */
  int m_unspec_for_n_sint;
  int m_unspec_for_n_uint;
  int m_unspec_for_n_fp;
  int m_unspec_for_m_sint;
  int m_unspec_for_m_uint;
  int m_unspec_for_m_fp;
  int m_unspec_for_m_n_sint;
  int m_unspec_for_m_n_uint;
  int m_unspec_for_m_n_fp;
};

/* Map the function directly to CODE (UNSPEC, M) where M is the vector
   mode associated with type suffix 0, except when there is no
   predicate and no _n suffix, in which case we use the appropriate
   rtx_code.  This is useful when the basic operation is mapped to a
   standard RTX code and all other versions use different unspecs.  */
class unspec_based_mve_function_exact_insn : public unspec_based_mve_function_base
{
public:
  CONSTEXPR unspec_based_mve_function_exact_insn (rtx_code code_for_sint,
						  rtx_code code_for_uint,
						  rtx_code code_for_fp,
						  int unspec_for_n_sint,
						  int unspec_for_n_uint,
						  int unspec_for_n_fp,
						  int unspec_for_m_sint,
						  int unspec_for_m_uint,
						  int unspec_for_m_fp,
						  int unspec_for_m_n_sint,
						  int unspec_for_m_n_uint,
						  int unspec_for_m_n_fp)
    : unspec_based_mve_function_base (code_for_sint,
				      code_for_uint,
				      code_for_fp,
				      unspec_for_n_sint,
				      unspec_for_n_uint,
				      unspec_for_n_fp,
				      unspec_for_m_sint,
				      unspec_for_m_uint,
				      unspec_for_m_fp,
				      unspec_for_m_n_sint,
				      unspec_for_m_n_uint,
				      unspec_for_m_n_fp)
  {}

  rtx
  expand (function_expander &e) const override
  {
    /* No suffix, no predicate, use the right RTX code.  */
    if ((e.mode_suffix_id != MODE_n)
	&& (e.pred == PRED_none))
      return e.map_to_rtx_codes (m_code_for_sint, m_code_for_uint,
				 m_code_for_fp);

    insn_code code;
    switch (e.pred)
      {
      case PRED_none:
	if (e.mode_suffix_id == MODE_n)
	  /* No predicate, _n suffix.  */
	  {
	    if (e.type_suffix (0).integer_p)
	      if (e.type_suffix (0).unsigned_p)
		code = code_for_mve_q_n (m_unspec_for_n_uint, m_unspec_for_n_uint, e.vector_mode (0));
	      else
		code = code_for_mve_q_n (m_unspec_for_n_sint, m_unspec_for_n_sint, e.vector_mode (0));
	    else
	      code = code_for_mve_q_n_f (m_unspec_for_n_fp, e.vector_mode (0));

	    return e.use_exact_insn (code);
	  }
	gcc_unreachable ();
	break;

      case PRED_m:
	switch (e.mode_suffix_id)
	  {
	  case MODE_none:
	    /* No suffix, "m" predicate.  */
	    if (e.type_suffix (0).integer_p)
	      if (e.type_suffix (0).unsigned_p)
		code = code_for_mve_q_m (m_unspec_for_m_uint, m_unspec_for_m_uint, e.vector_mode (0));
	      else
		code = code_for_mve_q_m (m_unspec_for_m_sint, m_unspec_for_m_sint, e.vector_mode (0));
	    else
	      code = code_for_mve_q_m_f (m_unspec_for_m_fp, e.vector_mode (0));
	    break;

	  case MODE_n:
	    /* _n suffix, "m" predicate.  */
	    if (e.type_suffix (0).integer_p)
	      if (e.type_suffix (0).unsigned_p)
		code = code_for_mve_q_m_n (m_unspec_for_m_n_uint, m_unspec_for_m_n_uint, e.vector_mode (0));
	      else
		code = code_for_mve_q_m_n (m_unspec_for_m_n_sint, m_unspec_for_m_n_sint, e.vector_mode (0));
	    else
	      code = code_for_mve_q_m_n_f (m_unspec_for_m_n_fp, e.vector_mode (0));
	    break;

	  default:
	    gcc_unreachable ();
	  }
	return e.use_cond_insn (code, 0);

      case PRED_x:
	switch (e.mode_suffix_id)
	  {
	  case MODE_none:
	    /* No suffix, "x" predicate.  */
	    if (e.type_suffix (0).integer_p)
	      if (e.type_suffix (0).unsigned_p)
		code = code_for_mve_q_m (m_unspec_for_m_uint, m_unspec_for_m_uint, e.vector_mode (0));
	      else
		code = code_for_mve_q_m (m_unspec_for_m_sint, m_unspec_for_m_sint, e.vector_mode (0));
	    else
	      code = code_for_mve_q_m_f (m_unspec_for_m_fp, e.vector_mode (0));
	    break;

	  case MODE_n:
	    /* _n suffix, "x" predicate.  */
	    if (e.type_suffix (0).integer_p)
	      if (e.type_suffix (0).unsigned_p)
		code = code_for_mve_q_m_n (m_unspec_for_m_n_uint, m_unspec_for_m_n_uint, e.vector_mode (0));
	      else
		code = code_for_mve_q_m_n (m_unspec_for_m_n_sint, m_unspec_for_m_n_sint, e.vector_mode (0));
	    else
	      code = code_for_mve_q_m_n_f (m_unspec_for_m_n_fp, e.vector_mode (0));
	    break;

	  default:
	    gcc_unreachable ();
	  }
	return e.use_pred_x_insn (code);

      default:
	gcc_unreachable ();
      }

    gcc_unreachable ();
  }
};

/* Map the function directly to CODE (UNSPEC, M) where M is the vector
   mode associated with type suffix 0.  */
class unspec_mve_function_exact_insn : public function_base
{
public:
  CONSTEXPR unspec_mve_function_exact_insn (int unspec_for_sint,
					    int unspec_for_uint,
					    int unspec_for_fp,
					    int unspec_for_n_sint,
					    int unspec_for_n_uint,
					    int unspec_for_n_fp,
					    int unspec_for_m_sint,
					    int unspec_for_m_uint,
					    int unspec_for_m_fp,
					    int unspec_for_m_n_sint,
					    int unspec_for_m_n_uint,
					    int unspec_for_m_n_fp)
    : m_unspec_for_sint (unspec_for_sint),
      m_unspec_for_uint (unspec_for_uint),
      m_unspec_for_fp (unspec_for_fp),
      m_unspec_for_n_sint (unspec_for_n_sint),
      m_unspec_for_n_uint (unspec_for_n_uint),
      m_unspec_for_n_fp (unspec_for_n_fp),
      m_unspec_for_m_sint (unspec_for_m_sint),
      m_unspec_for_m_uint (unspec_for_m_uint),
      m_unspec_for_m_fp (unspec_for_m_fp),
      m_unspec_for_m_n_sint (unspec_for_m_n_sint),
      m_unspec_for_m_n_uint (unspec_for_m_n_uint),
      m_unspec_for_m_n_fp (unspec_for_m_n_fp)
  {}

  /* The unspec code associated with signed-integer, unsigned-integer
     and floating-point operations respectively.  It covers the cases
     with the _n suffix, and/or the _m predicate.  */
  int m_unspec_for_sint;
  int m_unspec_for_uint;
  int m_unspec_for_fp;
  int m_unspec_for_n_sint;
  int m_unspec_for_n_uint;
  int m_unspec_for_n_fp;
  int m_unspec_for_m_sint;
  int m_unspec_for_m_uint;
  int m_unspec_for_m_fp;
  int m_unspec_for_m_n_sint;
  int m_unspec_for_m_n_uint;
  int m_unspec_for_m_n_fp;

  rtx
  expand (function_expander &e) const override
  {
    insn_code code;
    switch (e.pred)
      {
      case PRED_none:
	switch (e.mode_suffix_id)
	  {
	  case MODE_none:
	    /* No predicate, no suffix.  */
	    if (e.type_suffix (0).integer_p)
	      if (e.type_suffix (0).unsigned_p)
		code = code_for_mve_q (m_unspec_for_uint, m_unspec_for_uint, e.vector_mode (0));
	      else
		code = code_for_mve_q (m_unspec_for_sint, m_unspec_for_sint, e.vector_mode (0));
	    else
	      code = code_for_mve_q_f (m_unspec_for_fp, e.vector_mode (0));
	    break;

	  case MODE_n:
	    /* No predicate, _n suffix.  */
	    if (e.type_suffix (0).integer_p)
	      if (e.type_suffix (0).unsigned_p)
		code = code_for_mve_q_n (m_unspec_for_n_uint, m_unspec_for_n_uint, e.vector_mode (0));
	      else
		code = code_for_mve_q_n (m_unspec_for_n_sint, m_unspec_for_n_sint, e.vector_mode (0));
	    else
	      code = code_for_mve_q_n_f (m_unspec_for_n_fp, e.vector_mode (0));
	    break;

	  default:
	    gcc_unreachable ();
	  }
	return e.use_exact_insn (code);

      case PRED_m:
	switch (e.mode_suffix_id)
	  {
	  case MODE_none:
	    /* No suffix, "m" predicate.  */
	    if (e.type_suffix (0).integer_p)
	      if (e.type_suffix (0).unsigned_p)
		code = code_for_mve_q_m (m_unspec_for_m_uint, m_unspec_for_m_uint, e.vector_mode (0));
	      else
		code = code_for_mve_q_m (m_unspec_for_m_sint, m_unspec_for_m_sint, e.vector_mode (0));
	    else
	      code = code_for_mve_q_m_f (m_unspec_for_m_fp, e.vector_mode (0));
	    break;

	  case MODE_n:
	    /* _n suffix, "m" predicate.  */
	    if (e.type_suffix (0).integer_p)
	      if (e.type_suffix (0).unsigned_p)
		code = code_for_mve_q_m_n (m_unspec_for_m_n_uint, m_unspec_for_m_n_uint, e.vector_mode (0));
	      else
		code = code_for_mve_q_m_n (m_unspec_for_m_n_sint, m_unspec_for_m_n_sint, e.vector_mode (0));
	    else
	      code = code_for_mve_q_m_n_f (m_unspec_for_m_n_fp, e.vector_mode (0));
	    break;

	  default:
	    gcc_unreachable ();
	  }
	return e.use_cond_insn (code, 0);

      case PRED_x:
	switch (e.mode_suffix_id)
	  {
	  case MODE_none:
	    /* No suffix, "x" predicate.  */
	    if (e.type_suffix (0).integer_p)
	      if (e.type_suffix (0).unsigned_p)
		code = code_for_mve_q_m (m_unspec_for_m_uint, m_unspec_for_m_uint, e.vector_mode (0));
	      else
		code = code_for_mve_q_m (m_unspec_for_m_sint, m_unspec_for_m_sint, e.vector_mode (0));
	    else
	      code = code_for_mve_q_m_f (m_unspec_for_m_fp, e.vector_mode (0));
	    break;

	  case MODE_n:
	    /* _n suffix, "x" predicate.  */
	    if (e.type_suffix (0).integer_p)
	      if (e.type_suffix (0).unsigned_p)
		code = code_for_mve_q_m_n (m_unspec_for_m_n_uint, m_unspec_for_m_n_uint, e.vector_mode (0));
	      else
		code = code_for_mve_q_m_n (m_unspec_for_m_n_sint, m_unspec_for_m_n_sint, e.vector_mode (0));
	    else
	      code = code_for_mve_q_m_n_f (m_unspec_for_m_n_fp, e.vector_mode (0));
	    break;

	  default:
	    gcc_unreachable ();
	  }
	return e.use_pred_x_insn (code);

      default:
	gcc_unreachable ();
      }

    gcc_unreachable ();
  }
};

} /* end namespace arm_mve */

/* Declare the global function base NAME, creating it from an instance
   of class CLASS with constructor arguments ARGS.  */
#define FUNCTION(NAME, CLASS, ARGS) \
  namespace { static CONSTEXPR const CLASS NAME##_obj ARGS; } \
  namespace functions { const function_base *const NAME = &NAME##_obj; }

#endif
