if BUILD_TOOLS
AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = $(GLOBAL_CFLAGS) $(libusb_CFLAGS)
AM_LDFLAGS = $(libusb_LIBS)

bin_PROGRAMS = irecovery

irecovery_SOURCES = irecovery.c
irecovery_CFLAGS = $(AM_CFLAGS)
irecovery_LDFLAGS = $(AM_LDFLAGS)
if HAVE_READLINE
irecovery_LDFLAGS += -lreadline
endif
irecovery_LDADD = $(top_builddir)/src/libirecovery-1.0.la
endif
