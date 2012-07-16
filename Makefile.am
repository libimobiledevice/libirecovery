AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src include

DISTCHECK_CONFIGURE_FLAGS =

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libirecovery.pc

indent:
	indent -kr -ut -ts4 -l120 src/*.c src/*.h

