/* Verify that -mzisslpcfi requires arch extension Zisslpcfi.  */

/* { dg-do compile } */
/* { dg-options "-mzisslpcfi" } */
/* { dg-error "'-mzisslpcfi' requires '-march' to have the 'zisslpcfi' extension" "" { target *-*-* } 0 } */

int func (int arg)
{
  static void *array[] = { &&foo, &&bar };

  goto *array[arg];
foo:
  return arg*111;
bar:
  return arg*777;
}
