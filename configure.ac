#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.64)
AC_INIT([libirecovery], [0.2.0], [https://github.com/libimobiledevice/libirecovery/issues],, [http://libimobiledevice.org])
AM_INIT_AUTOMAKE([dist-bzip2 no-dist-gzip])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

dnl libtool versioning
# +1 : 0 : +1  == adds new functions to the interface
# +1 : 0 : 0   == changes or removes functions (changes include both
#                 changes to the signature and the semantic)
#  ? :+1 : ?   == just internal changes
# CURRENT : REVISION : AGE
LIBIRECOVERY_SO_VERSION=2:1:0

dnl Minimum package versions
LIBUSB_VERSION=1.0.3

AC_SUBST(LIBIRECOVERY_SO_VERSION)

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O
AC_PROG_LIBTOOL

# Checks for libraries.
AC_CHECK_HEADERS([readline/readline.h], [],
	[AC_MSG_ERROR([Please install readline development headers])]
)

# Check additional platform flags
case "$host_os" in
	darwin*)
		AC_CHECK_HEADER(CoreFoundation/CoreFoundation.h, [
			AC_CHECK_HEADER(IOKit/usb/IOUSBLib.h, [
				LIBIRECOVERYLDFLAGS="-framework IOKit -framework CoreFoundation"
				have_iokit=yes
			], [])
		], [])
	;;
	mingw32*)
		LDFLAGS+=" -static-libgcc"
		LIBIRECOVERYLDFLAGS=" -lkernel32 -lmsvcrt -lsetupapi"
	;;
	cygwin*)
		CC=gcc-3
		CFLAGS+=" -mno-cygwin"
		LDFLAGS+=" -static-libgcc"
		LIBIRECOVERYLDFLAGS=" -lkernel32 -lmsvcrt -lsetupapi"
	;;
	*)
		LIBIRECOVERYLDFLAGS=
	;;
esac
AC_SUBST(LIBIRECOVERYLDFLAGS)

AS_IF([test "x$have_iokit" = "xyes"], [
	AC_ARG_WITH([iokit],
		[AS_HELP_STRING([--with-iokit], [Use IOKit instead of libusb on OS X [default=yes]])],
		[],
		[with_iokit=yes])
	], []
)

AS_IF([test "x$with_iokit" = "xyes" && test "x$have_iokit" = "xyes"] , [
		AC_DEFINE(HAVE_IOKIT, 1, [Define if we have IOKit])
		USB_BACKEND="IOKit"
	],
	[
		PKG_CHECK_MODULES(libusb, libusb-1.0 >= $LIBUSB_VERSION)
		USB_BACKEND="libusb `$PKG_CONFIG --modversion libusb-1.0`"
		LIBUSB_REQUIRED="libusb-1.0 >= $LIBUSB_VERSION"
		AC_SUBST(LIBUSB_REQUIRED)
	]
)

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdint.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([strcasecmp strdup strerror strndup])

# Check for operating system
AC_MSG_CHECKING([whether to enable WIN32 build settings])
case ${host_os} in
	*mingw32*|*cygwin*)
		win32=true
		AC_MSG_RESULT([yes])
		AC_CHECK_TOOL([WINDRES], [windres], AC_MSG_ERROR([windres not found]))
		AC_SUBST(WINDRES)
	;;
	*)
		win32=false
		AC_MSG_RESULT([no])
	;;
esac
AM_CONDITIONAL(WIN32, test x$win32 = xtrue)

AS_COMPILER_FLAGS(GLOBAL_CFLAGS, "-Wall -Wextra -Wmissing-declarations -Wredundant-decls -Wshadow -Wpointer-arith -Wwrite-strings -Wswitch-default -Wno-unused-parameter -fvisibility=hidden")
AC_SUBST(GLOBAL_CFLAGS)

case "$GLOBAL_CFLAGS" in
	*-fvisibility=hidden*)
		AC_DEFINE([HAVE_FVISIBILITY], [1], [Define if compiled with -fvisibility=hidden])
esac

# check for large file support
AC_SYS_LARGEFILE
LFS_CFLAGS=''
if test "$enable_largefile" != no; then
	if test "$ac_cv_sys_file_offset_bits" != 'no'; then
		LFS_CFLAGS="$LFS_CFLAGS -D_FILE_OFFSET_BITS=$ac_cv_sys_file_offset_bits"
	else
		AC_MSG_CHECKING(for native large file support)
		AC_RUN_IFELSE([AC_LANG_SOURCE([#include <unistd.h>
		  int main (int argc, char **argv)
		  {
		      exit(!(sizeof(off_t) == 8));
		  }])],
		[ac_cv_sys_file_offset_bits=64; AC_DEFINE(_FILE_OFFSET_BITS,64)
		 AC_MSG_RESULT(yes)],
		[AC_MSG_RESULT(no)])
	fi
	if test "$ac_cv_sys_large_files" != 'no'; then
		LFS_CFLAGS="$LFS_CFLAGS -D_LARGE_FILES=1"
	fi
	AC_FUNC_FSEEKO
	if test "$ac_cv_sys_largefile_source" != 'no'; then
		LFS_CFLAGS="$LFS_CFLAGS -D_LARGEFILE_SOURCE=1"
	fi
fi
AC_SUBST(LFS_CFLAGS)

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_OUTPUT([
Makefile
src/Makefile
include/Makefile
tools/Makefile
libirecovery.pc
])

echo "
Configuration for $PACKAGE $VERSION:
-------------------------------------------

  Install prefix: .........: $prefix
  USB backend: ............: $USB_BACKEND

  Now type 'make' to build $PACKAGE $VERSION,
  and then 'make install' for installation.
"
