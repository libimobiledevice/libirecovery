AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src include tools udev

EXTRA_DIST = \
	README.md \
	git-version-gen

dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

DISTCHECK_CONFIGURE_FLAGS = \
	--with-udevrulesdir=$$dc_install_base/$(udevrulesdir)
