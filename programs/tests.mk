EXTRA_DIST = $(TEST_SOURCES)

LOG_COMPILER = \
   MGSIM="$(top_builddir)/src/mgsim-$(ARCH).dbg -c $(top_srcdir)/programs/config.ini -t" \
   $(SHELL) $(top_srcdir)/programs/runtest.sh $(ARCH) $(top_srcdir)/programs/timeout

ASLINK = $(SHELL) $(top_builddir)/programs/aslink.sh $(ARCH)
SUFFIXES = .s .bin .coma .zlcoma .serial .parallel .banked .randombanked

.s.bin:
	$(MKDIR_P) `dirname "$@"`
	$(ASLINK) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.bin.coma:
	echo "$<" >"$@"
.bin.zlcoma:
	echo "$<" >"$@"
.bin.serial:
	echo "$<" >"$@"
.bin.parallel:
	echo "$<" >"$@"
.bin.banked:
	echo "$<" >"$@"
.bin.randombanked:
	echo "$<" >"$@"

TESTS = \
	$(TEST_BINS:.bin=.serial) \
	$(TEST_BINS:.bin=.parallel) \
	$(TEST_BINS:.bin=.banked) \
	$(TEST_BINS:.bin=.randombanked) \
	$(TEST_BINS:.bin=.coma) \
    $(TEST_BINS:.bin=.zlcoma)

CLEANFILES = $(TESTS) *.out
