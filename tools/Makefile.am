AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = $(GLOBAL_CFLAGS) $(libusb_CFLAGS)
AM_LDFLAGS = $(libusb_LIBS) -lreadline

bin_PROGRAMS = irecovery irecovery_info

irecovery_SOURCES = irecovery.c
irecovery_CFLAGS = $(AM_CFLAGS)
irecovery_LDFLAGS = $(AM_LDFLAGS)
irecovery_LDADD = $(top_builddir)/src/libirecovery.la

irecovery_info_SOURCES = irecovery_info.c
irecovery_info_CFLAGS = $(AM_CFLAGS)
irecovery_info_LDFLAGS = $(AM_LDFLAGS)
irecovery_info_LDADD = $(top_builddir)/src/libirecovery.la
