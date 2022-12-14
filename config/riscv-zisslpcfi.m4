dnl
dnl GCC_RISCV_ZISSLPCFI_FLAGS
dnl    (SHELL-CODE_HANDLER)
dnl
AC_DEFUN([GCC_RISCV_ZISSLPCFI_FLAGS],[dnl
GCC_ENABLE(zisslpcfi, auto, ,[enable RISCV Zisslpcfi in target libraries],
	   permit yes|no|auto|+*)
AC_MSG_CHECKING([for RISCV Zisslpcfi support])

# NB: Avoid nested save_CFLAGS and save_LDFLAGS.
case "$host" in
  riscv*-*-linux*)
    # Check if compiler and assembler support Zisslpcfi insn.
    case "$enable_zisslpcfi" in
      auto)
	zisslpcfi_save_CFLAGS="$CFLAGS"
	_mzisslpcfi="-mzisslpcfi"
	CFLAGS="$CFLAGS $_mzisslpcfi"
	AC_COMPILE_IFELSE(
	 [AC_LANG_PROGRAM(
	  [],
	  [asm ("lpsll 0\n\tlpcll 0\n\tsspush ra\n\tsspop t0\n\tsschkra");])],
	 [enable_zisslpcfi=yes],
	 [enable_zisslpcfi=no])
	CFLAGS="$zisslpcfi_save_CFLAGS"
	;;
      no) ;;
      yes|?*)
	zisslpcfi_save_CFLAGS="$CFLAGS"
	case "$enable_zisslpcfi" in
	yes) _mzisslpcfi="-mzisslpcfi";;
	?*) _mzisslpcfi="-mzisslpcfi=$enable_zisslpcfi";;
	esac
	CFLAGS="$CFLAGS $_mzisslpcfi"
	AC_COMPILE_IFELSE(
	 [AC_LANG_PROGRAM(
	  [],
	  [asm ("lpsll 0\n\tlpcll 0\n\tsspush ra\n\tsspop t0\n\tsschkra");])],
	 [],
	 [AC_MSG_ERROR([assembler with Zisslpcfi support is required for --enable-zisslpcfi])])
	CFLAGS="$zisslpcfi_save_CFLAGS"
	;;
    esac
    ;;
  *)
    enable_zisslpcfi=no
    ;;
esac
if ! test x$enable_zisslpcfi = xno; then
  $1="$_mzisslpcfi"
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
fi
])
