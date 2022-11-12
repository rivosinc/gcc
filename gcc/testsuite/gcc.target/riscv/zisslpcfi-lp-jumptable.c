/* Verify that -mzisslpcfi emits landing pads for a jump table.  */

/* { dg-do compile } */
/* { dg-options "-march=rv64g_zisslpcfi -mzisslpcfi=+3+check0" } */
/* { dg-final { scan-assembler-times "\tlpcll\t0" 12 } } */

void func2 (int);

int func1 (int arg)
{
  switch (arg)
  {
    case 1: func2 (arg*100);
    case 2: func2 (arg*300);
    case 5: func2 (arg*500);
    case 8: func2 (arg*700);
    case 7: func2 (arg*900);
    case -1: func2 (arg*-100);
    case -2: func2 (arg*-300);
    case -5: func2 (arg*-500);
    case -7: func2 (arg*-700);
    case -9: func2 (arg*-900);
  }
  return 0;
}
