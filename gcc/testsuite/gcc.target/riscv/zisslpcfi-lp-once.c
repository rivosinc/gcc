/* Verify that -mzisslpcfi only emits one landing pad.  */
/* There should not be a second landing pad at the label "end".  */

/* { dg-do compile } */
/* { dg-options "-march=rv32g_zisslpcfi0p1 -mabi=ilp32 -mzisslpcfi=+3+check0" } */
/* { dg-final { scan-assembler-times "\tlpcll\t0" 1 } } */

int
test (int* val)
{
  int status = 99;

  if (!val)
    {
      status = 22;
      goto end;
    }

  extern int x;
  *val = x;

  status = 0;
end:
  return status;
}
