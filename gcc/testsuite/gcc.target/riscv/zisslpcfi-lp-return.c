/* Verify that -mzisslpcfi does not emit landing pad for interior label.  */

/* { dg-do compile } */
/* { dg-options "-march=rv64g_zisslpcfi -mzisslpcfi=+3+check0  -Wno-return-local-addr" } */
/* { dg-final { scan-assembler-times "\tlpcll\t0" 1 } } */

void *
func (void)
{
  return &&bar;
bar:
  return 0;
}
