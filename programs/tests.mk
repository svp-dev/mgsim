EXTRA_DIST = $(TEST_SOURCES)


LOG_COMPILER = \
   MGSIM="$(top_builddir)/src/mgsim-$(ARCH).dbg -c $(top_srcdir)/programs/config.ini -t" \
   $(SHELL) $(top_srcdir)/programs/runtest.sh $(ARCH) $(top_srcdir)/programs/timeout

ASLINK = $(SHELL) $(top_builddir)/programs/aslink.sh $(ARCH)
SUFFIXES = .s .bin

.s.bin:
	$(MKDIR_P) `dirname "$@"`
	$(ASLINK) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

