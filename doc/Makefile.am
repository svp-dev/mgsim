dist_doc_DATA = \
	README \
	INSTALL \
	AUTHORS \
        NEWS \
	mgsimdoc.rst \
	dev-arom.txt \
	dev-rtc.txt \
	dev-lcd.txt \
	dev-gfx.txt \
	dev-uart.txt \
	processor-architecture-20110430.pdf \
	system-model-20110430.pdf

dist_man7_MANS = \
	mgsimdoc.7

.rst.7:
	$(AM_V_GEN)$(SED) -e "s/PACKAGE_BUGREPORT/$(PACKAGE_BUGREPORT)/g;s/PACKAGE_VERSION/$(PACKAGE_VERSION)/g" <$< | $(RST2MAN) - $@.tmp || { rm -f $@.tmp; exit 1; }
	$(AM_V_at)mv -f $@.tmp $@

MAINTAINERCLEANFILES = $(dist_man7_MANS)
