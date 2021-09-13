AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = \
	$(GLOBAL_CFLAGS) \
	$(LFS_CFLAGS) \
	$(limd_glue_CFLAGS) \
	$(libusb_CFLAGS)

AM_LDFLAGS = \
	$(GLOBAL_LDFLAGS) \
	$(limd_glue_LIBS) \
	$(libusb_LIBS)

lib_LTLIBRARIES = libirecovery-1.0.la
libirecovery_1_0_la_CFLAGS = $(AM_CFLAGS)
libirecovery_1_0_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LIBIRECOVERY_SO_VERSION) -no-undefined
libirecovery_1_0_la_SOURCES = libirecovery.c

if WIN32
libirecovery_1_0_la_LDFLAGS += -avoid-version
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libirecovery-1.0.pc
