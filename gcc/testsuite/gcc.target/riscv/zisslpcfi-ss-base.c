/* Verify that -mzisslpcfi emits section .note.gnu.property.  */

/* { dg-do compile { target *-*-linux* } } */
/* { dg-options "-march=rv64g_zisslpcfi -mzisslpcfi=+ss" } */
/* { dg-final { scan-assembler-times "\tsspush" 3 } } */
/* { dg-final { scan-assembler-times "\tsspop" 3 } } */
/* { dg-final { scan-assembler-times "\tsschkra" 3 } } */

extern int a (int);
extern int b (int, int, int);
extern int c (int, int, int, int, int, int, int, int,
              int, int, int, int, int, int, int);
extern int d (int, int, int, int, int);

int
a (int i)
{
  return b (i + 1, i * 2, i / 3);
}

extern int i1, i2, i3, i4, i5, i6, i7, i8,
           i9, i10, i11, i12, i13, i14, i15;

int
b (int i, int j, int k)
{
  return c (i1, i2, i3, i4, i5, i6, i7, i8,
            i9, i10, i11, i12, i13, i14, i15);
}

int
c (int a1, int a2, int a3, int a4, int a5,
   int a6, int a7, int a8, int a9, int a10,
   int a11, int a12, int a13, int a14, int a15)
{
  return (d (a1, a2, a3, a4, a5)
          + d (a6, a7, a8, a9, a10)
          + d (a11, a12, a13, a14, a15));
}
