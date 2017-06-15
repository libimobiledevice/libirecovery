if WITH_UDEV
edit = \
	$(SED) -r \
	-e 's|@udev_activation_rule[@]|$(udev_activation_rule)|g' \
	< $< > $@ || rm $@

udevrules_DATA = \
	39-libirecovery.rules

39-libirecovery.rules: 39-libirecovery.rules.in
	$(edit)

EXTRA_DIST = \
	39-libirecovery.rules.in

MAINTAINERCLEANFILES = \
	39-libirecovery.rules

CLEANFILES = \
	39-libirecovery.rules
endif
