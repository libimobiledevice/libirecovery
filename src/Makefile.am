AM_CPPFLAGS = -I$(top_srcdir)/include -DIRECV_DATADIR=\"$(pkgdatadir)\"

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
libirecovery_1_0_la_SOURCES = libirecovery.c devicedb.c devicedb.h devices.c

# The built-in device table is generated from data/devices.txt
BUILT_SOURCES = devices.c
CLEANFILES = devices.c

devices.c: $(top_srcdir)/data/devices.txt $(top_srcdir)/data/gen-devices.sh
	$(AM_V_GEN)$(SHELL) $(top_srcdir)/data/gen-devices.sh $(top_srcdir)/data/devices.txt > $@.tmp && mv $@.tmp $@

if WIN32
libirecovery_1_0_la_LDFLAGS += -avoid-version
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libirecovery-1.0.pc
