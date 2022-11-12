/* Verify that -mzisslpcfi emits landing pads for func and jump targets.  */

/* { dg-do compile } */
/* { dg-options "-march=rv64g_zisslpcfi -mzisslpcfi=+3+check0" } */
/* { dg-final { scan-assembler-times "\tlpcll\t0" 3 } } */

int func (int arg)
{
  static void *array[] = { &&foo, &&bar };

  goto *array[arg];
foo:
  return arg*111;
bar:
  return arg*777;
}
