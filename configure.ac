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
AC_CHECK_FUNCS([strdup strerror strndup])

# Checks for libraries.
AC_CHECK_HEADERS([readline/readline.h], [],
	[AC_MSG_ERROR([Please install readline development headers])]
)

# Check additional platform flags
AC_MSG_CHECKING([for platform-specific build settings])
case ${host_os} in
	darwin*)
		AC_MSG_RESULT([${host_os}])
		AC_CHECK_HEADER(CoreFoundation/CoreFoundation.h, [
			AC_CHECK_HEADER(IOKit/usb/IOUSBLib.h, [
				GLOBAL_LDFLAGS+=" -framework IOKit -framework CoreFoundation"
				have_iokit=yes
			], [])
		], [])
		AX_PTHREAD([], [AC_MSG_ERROR([pthread is required to build $PACKAGE])])
	;;
	mingw32*)
		AC_MSG_RESULT([${host_os}])
		GLOBAL_LDFLAGS+=" -static-libgcc -lkernel32 -lmsvcrt -lsetupapi"
		win32=true
	;;
	cygwin*)
		AC_MSG_RESULT([${host_os}])
		CC=gcc-3
		CFLAGS+=" -mno-cygwin"
		GLOBAL_LDFLAGS+=" -static-libgcc -lkernel32 -lmsvcrt -lsetupapi"
		win32=true
	;;
	*)
		AC_MSG_RESULT([${host_os}])
		AX_PTHREAD([], [AC_MSG_ERROR([pthread is required to build $PACKAGE])])
	;;
esac
AM_CONDITIONAL(WIN32, test x$win32 = xtrue)

AC_ARG_WITH([dummy],
	[AS_HELP_STRING([--with-dummy], [Use no USB driver at all [default=no]. This is only useful if you just want to query the device list by product type or hardware model. All other operations are no-ops or will return IRECV_E_UNSUPPORTED.])],
	[],
	[with_dummy=no])

AS_IF([test "x$have_iokit" = "xyes"], [
	AC_ARG_WITH([iokit],
		[AS_HELP_STRING([--with-iokit], [Use IOKit instead of libusb on OS X [default=yes]])],
		[],
		[with_iokit=yes])
	]
)

AS_IF([test "x$with_dummy" = "xyes"], [
	AC_DEFINE(USE_DUMMY, 1, [Define if we are using dummy USB driver])
	USB_BACKEND="dummy"
], [
	AS_IF([test "x$with_iokit" = "xyes" && test "x$have_iokit" = "xyes"], [
		AC_DEFINE(HAVE_IOKIT, 1, [Define if we have IOKit])
		USB_BACKEND="IOKit"
	], [
		AS_IF([test "x$win32" = "xtrue"], [
			USB_BACKEND="win32 native (setupapi)"
		], [
			PKG_CHECK_MODULES(libusb, libusb-1.0 >= $LIBUSB_VERSION)
			USB_BACKEND="libusb `$PKG_CONFIG --modversion libusb-1.0`"
			LIBUSB_REQUIRED="libusb-1.0 >= $LIBUSB_VERSION"
			AC_SUBST(LIBUSB_REQUIRED)
		])
	])
])

AS_COMPILER_FLAGS(GLOBAL_CFLAGS, "-Wall -Wextra -Wmissing-declarations -Wredundant-decls -Wshadow -Wpointer-arith -Wwrite-strings -Wswitch-default -Wno-unused-parameter -fvisibility=hidden $PTHREAD_CFLAGS")
GLOBAL_LDFLAGS+=" $PTHREAD_LIBS"
AC_SUBST(GLOBAL_CFLAGS)
AC_SUBST(GLOBAL_LDFLAGS)

case "$GLOBAL_CFLAGS" in
	*-fvisibility=hidden*)
		AC_DEFINE([HAVE_FVISIBILITY], [1], [Define if compiled with -fvisibility=hidden])
esac

# check for large file support
AC_SYS_LARGEFILE

AC_ARG_WITH([udev],
	AS_HELP_STRING([--with-udev],
	[Configure and install udev rules file for DFU/Recovery mode devices]),
	[],
	[if $($PKG_CONFIG --exists udev); then with_udev=yes; else with_udev=no; fi])

AC_ARG_WITH([udevrulesdir],
	AS_HELP_STRING([--with-udevrulesdir=DIR],
	[Directory for udev rules (implies --with-udev)]),
	[with_udev=yes],
	[with_udevrulesdir=auto])

AC_ARG_WITH([udevrule],
	AS_HELP_STRING([--with-udevrule="RULE"],
	[udev activation rule (implies --with-udev)]),
	[with_udev=yes],
	[with_udevrule=auto])

if test "x$with_udev" = "xyes"; then
	if test "x$with_udevrule" = "xauto"; then
		for I in plugdev storage disk staff; do
			if grep $I /etc/group >/dev/null; then
				USEGROUP=$I
				break
			fi
		done
		if test "x$USEGROUP" != "x"; then
			if ! groups |grep $USEGROUP >/dev/null; then
				AC_MSG_WARN([The group '$USEGROUP' was determined to be used for the udev rule, but the current user is not member of this group.])
			fi
		else
			AC_MSG_ERROR([Could not determine an appropriate user group for the udev activation rule.
    Please manually specify a udev activation rule using --with-udevrule=<RULE>
    Example: --with-udevrule="OWNER=\\"root\\", GROUP=\\"myusergroup\\", MODE=\\"0660\\""])
		fi
		with_udevrule="OWNER=\"root\", GROUP=\"$USEGROUP\", MODE=\"0660\""
	fi

	if test "x$with_udevrulesdir" = "xauto"; then
		udevdir=$($PKG_CONFIG --silence-errors --variable=udevdir udev)
		if test "x$udevdir" != "x"; then
			with_udevrulesdir=$udevdir"/rules.d"
		else
			with_udevrulesdir="\${prefix}/lib/udev/rules.d"
			AC_MSG_WARN([Could not determine default udev rules directory. Using $with_udevrulesdir.])
		fi
	fi

	AC_SUBST([udev_activation_rule], [$with_udevrule])
	AC_SUBST([udevrulesdir], [$with_udevrulesdir])
fi
AM_CONDITIONAL(WITH_UDEV, test "x$with_udev" = "xyes")

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_OUTPUT([
Makefile
src/Makefile
include/Makefile
tools/Makefile
libirecovery.pc
udev/Makefile
])

echo "
Configuration for $PACKAGE $VERSION:
-------------------------------------------

  Install prefix: .........: $prefix
  USB backend: ............: $USB_BACKEND

  Now type 'make' to build $PACKAGE $VERSION,
  and then 'make install' for installation.
"
