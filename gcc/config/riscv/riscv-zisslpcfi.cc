/* Branch Target Identification for RISCV architecture.
   Copyright (C) 2019-2022 Free Software Foundation, Inc.
   Contributed by Arm Ltd.

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

#define IN_TARGET_CODE 1

#include "config.h"
#define INCLUDE_STRING
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "memmodel.h"
#include "gimple.h"
#include "tm_p.h"
#include "stringpool.h"
#include "attribs.h"
#include "emit-rtl.h"
#include "gimplify.h"
#include "gimple-iterator.h"
#include "dumpfile.h"
#include "rtl-iter.h"
#include "cfgrtl.h"
#include "tree-pass.h"
#include "cgraph.h"
#include "output.h"

/* This pass implements forward-CFI landing pad checks for RISCV. This is
   a security feature similar to BTI (branch target identification) in
   AArch64 and IBT (indirect branch tracking)in X86. A LPCLL (landing-pad
   check lower label) instruction is used to guard against the execution of
   instructions which are not the intended target of an indirect branch.

   When forward-CFI is disabled or unimplemented in the CPU, the
   landing-pad check label instructions behave as NOP. When implemented in
   the CPU, and enabled, the destination of an indirect branch must be
   LPCLL insn. Otherwise, the CPU reaises an exception.

   In order to enable this mechanism, this pass iterates through the
   control flow of the code and adds appropriate LPCLL instructions at the
   beginning of any function that can be called indirectly, and for targets
   of indirect jumps, i.e., jump table targets, non-local goto targets, and
   labels that might be referenced by variables, constant pools, etc
   (NOTE_INSN_DELETED_LABEL).

   This pass is triggered by the command line option -mzisslpcfi=+[123],
   which indicates the landing-pad label width of 1/2/3 byte-ish
   components, or 9/17/25 bits, respectively.  Since all the Zisslpcfi
   instructions are in the Zimops encoding space, this pass does not
   require any minimum architecture version. */

namespace {

const pass_data pass_data_insert_landing_pad =
{
  RTL_PASS, /* type. */
  "zisslpcfi", /* name. */
  OPTGROUP_NONE, /* optinfo_flags. */
  TV_MACH_DEP, /* tv_id. */
  0, /* properties_required. */
  0, /* properties_provided. */
  0, /* properties_destroyed. */
  0, /* todo_flags_start. */
  0, /* todo_flags_finish. */
};

static bool
landing_pad_insn_p (rtx_insn *insn, enum unspecv unspecv_lpxll)
{
  return (insn && INSN_P (insn)
	  && GET_CODE (PATTERN (insn)) == UNSPEC_VOLATILE
	  && XINT (PATTERN (insn), 1) == unspecv_lpxll);
}

static void
emit_aligned_lpcll (rtx_insn *code_label, rtx lp_label_rtx = NULL_RTX)
{
  if (lp_label_rtx == NULL_RTX)
    {
      uint32_t lp_label = riscv_get_landing_pad_label ();
      lp_label_rtx = GEN_INT (ZISSLPCFI_LP_LABEL_LOWER (lp_label));
    }
  emit_insn_before (gen_lp_align (), code_label);
  emit_insn_after (gen_lpcll (lp_label_rtx), code_label);
}

/* Emit landing-pad set instructions at the top of a function.  Also emit a
   pseudo-op to inform the assembler that this function has a landing-pad,
   which will be noted in the ELF symbol table entry as a bit in the
   machine-specific ST_OTHER. */

static void
emit_lpcxl_insns (rtx_insn *insn)
{
  uint32_t lp_label = riscv_get_landing_pad_label ();
  insn = emit_insn_before
    (gen_lpcll (GEN_INT (ZISSLPCFI_LP_LABEL_LOWER (lp_label))), insn);
  if (ZISSLPCFI_LP_WIDTH (riscv_zisslpcfi) >= 2)
    insn = emit_insn_after
      (gen_lpcml (GEN_INT (ZISSLPCFI_LP_LABEL_MIDDLE (lp_label))), insn);
  if (ZISSLPCFI_LP_WIDTH (riscv_zisslpcfi) == 3)
    insn = emit_insn_after
      (gen_lpcul (GEN_INT (ZISSLPCFI_LP_LABEL_UPPER (lp_label))), insn);

  fputs ("\t.zisslpcfi_lp ", asm_out_file);
  assemble_name (asm_out_file,
		 IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (cfun->decl)));
  putc ('\n', asm_out_file);
}

/* Insert landing-pad check instructions.  This is a late RTL pass that runs
   before branch shortening. */
static unsigned int
rest_of_insert_landing_pad (void)
{
  timevar_push (TV_MACH_DEP);
  rtx_insn *insn;
  basic_block bb = 0;
  FOR_EACH_BB_FN (bb, cfun)
    for (insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb));
	 insn = NEXT_INSN (insn))
      {
	/* If a label is marked to be preserved, it must be
	   protected with a landing pad. */
	if (LABEL_P (insn) && (LABEL_PRESERVE_P (insn)
			       || bb->flags & BB_NON_LOCAL_GOTO_TARGET))
	  emit_aligned_lpcll (insn);

	/* There could still be more labels that are valid targets of a
	   LPCLL instuction. To find them we start looking through the
	   JUMP_INSN. If it jumps to a jump table, then we find all labels
	   of the jump table to protect with a LPCLL insn. */
	rtx_jump_table_data *table;
	if (JUMP_P (insn) && tablejump_p (insn, NULL, &table))
	  {
	    rtvec vec = table->get_labels ();
	    for (int i = GET_NUM_ELEM (vec) - 1; i >= 0; --i)
	      {
		rtx_insn *code_label
		  = as_a <rtx_insn *> (XEXP (RTVEC_ELT (vec, i), 0));
		rtx_insn *prev = prev_nonnote_nondebug_insn (code_label);
		rtx_insn *next = next_nonnote_nondebug_insn (code_label);
		if (! landing_pad_insn_p (next, UNSPECV_LPCLL))
		  emit_aligned_lpcll (code_label);
		if (! BARRIER_P (prev) && (ZISSLPCFI_LP_KIND (riscv_zisslpcfi)
					   >= ZISSLPCFI_LP_KIND_SET0))
		  {
		    /* No barrier before this point means that the previous
		       block could fall-through and hit LPCLL. It is more
		       expedient to set LPLR than to branch around LPCLL.  */
		    prev = riscv_prev_ebb_head (prev);
		    if (! landing_pad_insn_p (prev, UNSPECV_LPSLL))
		      emit_insn_before
			(gen_lpsll (GEN_INT
				    (ZISSLPCFI_LP_LABEL_LOWER
				     (riscv_get_landing_pad_label ()))), prev);
		  }
	      }
	  }

	/* setjmp/longjmp use lp_label=~0.  We must place a landing pad
	   immediately after the call to setjmp, which is where longjmp will
	   indirectly jump, and set LPLR=~0 prior to normal RET.  */
	if (CALL_P (insn) && find_reg_note (insn, REG_SETJMP, NULL))
	  emit_aligned_lpcll
	    (insn, GEN_INT ((ZISSLPCFI_LP_KIND (riscv_zisslpcfi)
			     <= ZISSLPCFI_LP_KIND_SET0) ? 0 : 0x1ff));
      }

  /* Since an exception due to a missing landing pad can only be triggered
     by an indirect call, we exempt function that are only called directly.
     Otherwise, insert a landing pad at the beginning of the function. */
  if (!cgraph_node::get (cfun->decl)->only_called_directly_p ())
    {
      bb = ENTRY_BLOCK_PTR_FOR_FN (cfun)->next_bb;
      emit_lpcxl_insns (BB_HEAD (bb));
    }

  timevar_pop (TV_MACH_DEP);
  return 0;
}

class pass_insert_landing_pad : public rtl_opt_pass
{
public:
  pass_insert_landing_pad (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_insert_landing_pad, ctxt)
  {}

  /* opt_pass methods: */
  virtual bool gate (function *)
    {
      return TARGET_ZISSLPCFI && ZISSLPCFI_LP_WIDTH (riscv_zisslpcfi) > 0;
    }

  virtual unsigned int execute (function *)
    {
      return rest_of_insert_landing_pad ();
    }

}; // class pass_insert_landing_pad

} // anon namespace

rtl_opt_pass *
make_pass_insert_landing_pad (gcc::context *ctxt)
{
  return new pass_insert_landing_pad (ctxt);
}
