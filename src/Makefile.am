AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = $(GLOBAL_CFLAGS) $(libusb_CFLAGS)
AM_LDFLAGS = $(libusb_LIBS) $(LIBIRECOVERYLDFLAGS)

lib_LTLIBRARIES = libirecovery.la
libirecovery_la_CFLAGS = $(AM_CFLAGS)
libirecovery_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LIBIRECOVERY_SO_VERSION) -no-undefined
libirecovery_la_SOURCES = libirecovery.c

if WIN32
libirecovery_la_LDFLAGS += -avoid-version
endif