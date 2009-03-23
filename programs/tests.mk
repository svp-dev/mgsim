EXTRA_DIST = $(TEST_SOURCES)

TESTS_ENVIRONMENT = $(top_builddir)/src/mgsim-$(ARCH) -c $(top_srcdir)/programs/config.ini -t
ASLINK = $(SHELL) $(top_builddir)/programs/aslink.sh $(ARCH)
SUFFIXES = .s .bin

.s.bin:
	$(MKDIR_P) `dirname "$@"`
	$(ASLINK) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

