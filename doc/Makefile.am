dist_doc_DATA = \
	README \
	INSTALL \
	AUTHORS \
        NEWS \
	mgsimdoc.rst \
	mgsimdev-arom.rst \
	mgsimdev-gfx.rst \
	mgsimdev-lcd.rst \
	mgsimdev-uart.rst \
	mgsimdev-rtc.rst \
	mgsimdev-smc.rst \
	mgsimdev-rpc.rst \
	mgsim-memranges.rst \
	mgsim-perfcounters.rst \
	mgsim-control.rst \
	processor-architecture-20110430.pdf \
	system-model-20110430.pdf

dist_man7_MANS = \
	mgsimdev-arom.7 \
	mgsimdev-gfx.7 \
	mgsimdev-lcd.7 \
	mgsimdev-uart.7 \
	mgsimdev-rtc.7 \
	mgsimdev-smc.7 \
	mgsimdev-rpc.7 \
	mgsim-memranges.7 \
	mgsim-perfcounters.7 \
	mgsim-control.7 \
	mgsimdoc.7

.rst.7:
	$(AM_V_GEN)$(SED) -e "s/PACKAGE_BUGREPORT/$(PACKAGE_BUGREPORT)/g;s/PACKAGE_VERSION/$(PACKAGE_VERSION)/g" <$< | $(RST2MAN) - $@.tmp || { rm -f $@.tmp; exit 1; }
	$(AM_V_at)mv -f $@.tmp $@

# extra rules for manual generation of .pdf manuals
.7.pdf:
	$(AM_V_GEN)$(TBL) <$< | $(GROFF) -Tps -mandoc | $(PS2PDF) - - >$@

pdf: $(dist_man7_MANS:.7=.pdf)

MAINTAINERCLEANFILES = $(dist_man7_MANS)
