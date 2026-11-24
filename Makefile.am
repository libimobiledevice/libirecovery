AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src include tools udev

pkgdata_DATA = data/devices.txt

EXTRA_DIST = \
	README.md \
	org.libimobiledevice.libirecovery.metainfo.xml \
	data/devices.txt \
	data/gen-devices.sh \
	git-version-gen

dist-hook:
	@if ! git diff --quiet; then echo "Uncommitted changes present; not releasing"; exit 1; fi
	echo $(VERSION) > $(distdir)/.tarball-version

DISTCHECK_CONFIGURE_FLAGS = \
	--with-udevrulesdir=$$dc_install_base/$(udevrulesdir)

dist_metainfo_DATA = org.libimobiledevice.libirecovery.metainfo.xml
metainfodir = $(datarootdir)/metainfo
