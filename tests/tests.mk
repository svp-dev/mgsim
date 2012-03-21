LOG_COMPILER = \
   MGSIM=$(top_builddir)/mgsim $(SHELL) $(srcdir)/runtest.sh \
	$(top_srcdir)/tools/timeout \
	$(top_srcdir)/programs/config.ini \
	`test -f ../programs/nobounds.ini || echo '$(srcdir)/'`../programs/nobounds.ini

ASLINK = $(SHELL) $(top_builddir)/tools/aslink $(TEST_ARCH)
COMPILE = $(SHELL) $(top_builddir)/tools/compile $(TEST_ARCH) \
	   $(srcdir)/$(TEST_ARCH)/crt_simple.s $(top_srcdir)/programs/mtconf.c -DMGSIM_TEST_SUITE \
	   -I$(top_srcdir)/programs -I$(top_builddir)/programs


SUFFIXES = .c .s .bin .coma .zlcoma .serial .parallel .banked .randombanked .ddr .randomddr

.s.bin:
	$(MKDIR_P) `dirname "$@"`
	$(ASLINK) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.c.bin:
	$(MKDIR_P) `dirname "$@"`
	$(COMPILE) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

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
.bin.ddr:
	echo "$<" >"$@"
.bin.randomddr:
	echo "$<" >"$@"

check_DATA = $(TEST_BINS)
TESTS = \
	$(TEST_BINS:.bin=.serial) \
	$(TEST_BINS:.bin=.parallel) \
	$(TEST_BINS:.bin=.banked) \
	$(TEST_BINS:.bin=.randombanked) \
	$(TEST_BINS:.bin=.ddr) \
	$(TEST_BINS:.bin=.randomddr) \
	$(TEST_BINS:.bin=.coma) \
    $(TEST_BINS:.bin=.zlcoma)

CLEANFILES = $(TESTS) *.out
