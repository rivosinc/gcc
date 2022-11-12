/* Verify that -mzisslpcfi emits section .riscv.attributes.  */

/* { dg-do compile { target *-*-linux* } } */
/* { dg-options "-march=rv64g_zisslpcfi -mzisslpcfi" } */
/* { dg-final { scan-assembler ".riscv.attributes" } } */

extern void foo (void);

void
bar (void)
{
  foo ();
}
