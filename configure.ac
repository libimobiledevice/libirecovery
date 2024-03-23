#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.68)
AC_INIT([libirecovery], [m4_esyscmd(./git-version-gen $RELEASE_VERSION)], [https://github.com/libimobiledevice/libirecovery/issues], [], [https://libimobiledevice.org])
AM_INIT_AUTOMAKE([dist-bzip2 no-dist-gzip check-news])
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
LIBIRECOVERY_SO_VERSION=5:0:0

dnl Minimum package versions
LIBUSB_VERSION=1.0.3
LIMD_GLUE_VERSION=1.2.0

AC_SUBST(LIBIRECOVERY_SO_VERSION)
AC_SUBST(LIMD_GLUE_VERSION)

# Checks for programs.
AC_PROG_CC
#AC_PROG_CXX
AM_PROG_CC_C_O
LT_INIT

# Checks for libraries.
PKG_CHECK_MODULES(limd_glue, libimobiledevice-glue-1.0 >= $LIMD_GLUE_VERSION)

# Checks for header files.
AC_CHECK_HEADERS([stdint.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([strdup strerror strcasecmp strndup malloc realloc calloc])

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
	;;
	mingw32*)
		AC_MSG_RESULT([${host_os}])
		GLOBAL_LDFLAGS+=" -static-libgcc -lkernel32 -lsetupapi"
		win32=true
	;;
	cygwin*)
		AC_MSG_RESULT([${host_os}])
		CC=gcc-3
		CFLAGS+=" -mno-cygwin"
		GLOBAL_LDFLAGS+=" -static-libgcc -lkernel32 -lsetupapi"
		win32=true
	;;
	*)
		AC_MSG_RESULT([${host_os}])
	;;
esac
AM_CONDITIONAL(WIN32, test x$win32 = xtrue)

# Check if the C compiler supports __attribute__((constructor))
AC_CACHE_CHECK([wether the C compiler supports constructor/destructor attributes],
  ac_cv_attribute_constructor, [
  ac_cv_attribute_constructor=no
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
    [[
      static void __attribute__((constructor)) test_constructor(void) {
      }
      static void __attribute__((destructor)) test_destructor(void) {
      }
    ]], [])],
    [ac_cv_attribute_constructor=yes]
  )]
)
if test "$ac_cv_attribute_constructor" = "yes"; then
  AC_DEFINE(HAVE_ATTRIBUTE_CONSTRUCTOR, 1, [Define if the C compiler supports constructor/destructor attributes])
fi

AC_ARG_WITH([tools],
	[AS_HELP_STRING([--with-tools], [Build irecovery tools. (requires readline) [default=yes]])],
	[],
	[with_tools=yes])

AS_IF([test "x$with_tools" = "xyes"], [
	AC_DEFINE(BUILD_TOOLS, 1, [Define if we are building irecovery tools])
	AC_CHECK_HEADERS([readline/readline.h], [],
		[AC_MSG_ERROR([Please install readline development headers])]
	)]
)
AM_CONDITIONAL(BUILD_TOOLS, test "x$with_tools" = "xyes")

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

AS_COMPILER_FLAGS(GLOBAL_CFLAGS, "-Wall -Wextra -Wmissing-declarations -Wredundant-decls -Wshadow -Wpointer-arith -Wwrite-strings -Wswitch-default -Wno-unused-parameter -fvisibility=hidden")

if test "x$enable_static" = "xyes" -a "x$enable_shared" = "xno"; then
	GLOBAL_CFLAGS+=" -DIRECV_STATIC"
fi

AC_SUBST(GLOBAL_CFLAGS)
AC_SUBST(GLOBAL_LDFLAGS)

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

AC_CONFIG_FILES([
Makefile
src/Makefile
src/libirecovery-1.0.pc
udev/39-libirecovery.rules
include/Makefile
tools/Makefile
udev/Makefile
])
AC_OUTPUT

echo "
Configuration for $PACKAGE $VERSION:
-------------------------------------------

  Install prefix: .........: $prefix
  USB backend: ............: $USB_BACKEND

  Now type 'make' to build $PACKAGE $VERSION,
  and then 'make install' for installation.
"
