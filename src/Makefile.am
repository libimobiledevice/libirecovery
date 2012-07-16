AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = $(LIBIRECOVERY_CFLAGS)
AM_LDFLAGS = $(LIBIRECOVERY_LDFLAGS)
AM_LIBS = $(LIBIRECOVERY_LIBS)

lib_LTLIBRARIES = libirecovery.la
libirecovery_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LIBIRECOVERY_SO_VERSION) -no-undefined $(AM_LIBS)
libirecovery_la_SOURCES = \
		libirecovery.c

bin_PROGRAMS = irecovery

irecovery_SOURCES = irecovery.c
irecovery_CFLAGS = $(AM_CFLAGS)
irecovery_LDFLAGS = $(AM_LDFLAGS) $(IRECOVERY_LIBS)
irecovery_LDADD = ./libirecovery.la
