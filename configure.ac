#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(libirecovery, 0.1.1, nospam@nowhere.com)
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
LIBIRECOVERY_SO_VERSION=0:0:0

AC_SUBST(LIBIRECOVERY_SO_VERSION)

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O
AC_PROG_LIBTOOL

# Checks for libraries.
ACCFLAGS=
ACLDFLAGS=
ACLIBS=

#echo $host_os
case "$host_os" in
	darwin*)
		ACCFLAGS+="-I\$(top_srcdir)/include -I/usr/local/include -I/opt/local/include"
		ACLDFLAGS+="-L/usr/lib -L/usr/local/lib -L/opt/local/lib"
		ACLIBS+="-lusb-1.0 -framework CoreFoundation -framework IOKit"
	;;
	linux*)
		ACCFLAGS+="-fPIC -O3 -I\$(top_srcdir)/include -I/usr/include -I/usr/local/include"
		ACLDFLAGS+="-L/usr/lib -L/usr/local/lib"
		ACLIBS+="-lusb-1.0"
	;;
	mingw32*)
		ACCFLAGS+="-O3 -DWIN32 -I$(top_srcdir)/include -Ic:/mingw/include"
		ACLDFLAGS+="-static-libgcc -Lc:/mingw/lib -Lc:/mingw/bin"
		ACLIBS+="-lkernel32 -lmsvcrt -lsetupapi"
	;;
	cygwin*)
		CC=gcc-3
		ACCFLAGS+="-O3 -DWIN32 -mno-cygwin -I$(top_srcdir)/include -I/usr/include"
		ACLDFLAGS+="-static-libgcc -L/lib -L/usr/lib -L/bin"
		ACLIBS+="-lkernel32 -lmsvcrt -lsetupapi"
	;;
	*)
		
	;;
esac

IRECOVERY_LIBS=-lreadline
AC_SUBST(IRECOVERY_LIBS)

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

AS_COMPILER_FLAGS(GLOBAL_CFLAGS, "-Wall -Wextra -Wmissing-declarations -Wredundant-decls -Wshadow -Wpointer-arith  -Wwrite-strings -Wswitch-default -Wno-unused-parameter")
AC_SUBST(GLOBAL_CFLAGS)

LIBIRECOVERY_CFLAGS=$ACCFLAGS
LIBIRECOVERY_CFLAGS+=$GLOBAL_CFLAGS
LIBIRECOVERY_LDFLAGS=$ACLDFLAGS
LIBIRECOVERY_LIBS=$ACLIBS

AC_SUBST(LIBIRECOVERY_CFLAGS)
AC_SUBST(LIBIRECOVERY_LDFLAGS)
AC_SUBST(LIBIRECOVERY_LIBS)

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_OUTPUT([
Makefile
src/Makefile
include/Makefile
libirecovery.pc
])

echo "
Configuration for $PACKAGE $VERSION:
-------------------------------------------

  Install prefix: .........: $prefix

  Now type 'make' to build $PACKAGE $VERSION,
  and then 'make install' for installation.
"
