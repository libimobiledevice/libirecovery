AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src include tools udev

EXTRA_DIST = \
	README.md \
	git-version-gen

dist-hook:
	@if ! git diff --quiet; then echo "Uncommitted changes present; not releasing"; exit 1; fi
	echo $(VERSION) > $(distdir)/.tarball-version

DISTCHECK_CONFIGURE_FLAGS = \
	--with-udevrulesdir=$$dc_install_base/$(udevrulesdir)
