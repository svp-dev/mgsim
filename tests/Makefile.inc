TEST_BINS =

include tests/mtalpha/Makefile.inc
include tests/mtsparc/Makefile.inc
include tests/mips/Makefile.inc
include tests/common/Makefile.inc

LOG_COMPILER = \
   $(SHELL) $(srcdir)/tools/runtest.sh \
	$(builddir)/mgsim \
	$(srcdir)/tools/timeout \
	$(srcdir)/programs/config.ini \
	`test -f programs/nobounds.ini || echo '$(srcdir)/'`programs/nobounds.ini

ASLINK = $(SHELL) tools/aslink $(TEST_ARCH)
COMPILE = $(SHELL) tools/compile $(TEST_ARCH) \
	   $(srcdir)/tests/$(TEST_ARCH)/crt_simple.s $(srcdir)/programs/mtconf.c -DMGSIM_TEST_SUITE \
	   -I$(srcdir)/programs -Iprograms


SUFFIXES = .c .s .bin .coma .zlcoma .serial .parallel .banked .randombanked .ddr .randomddr

.s.bin:
	$(MKDIR_P) `dirname "$@"`
	$(ASLINK) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.c.bin:
	$(MKDIR_P) `dirname "$@"`
	$(COMPILE) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.bin.flatcoma:
	echo "$<" >"$@"
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
	$(TEST_BINS:.bin=.flatcoma) \
	$(TEST_BINS:.bin=.coma) \
    $(TEST_BINS:.bin=.zlcoma)

check_%: $(TEST_BINS)
	$(MAKE) check TESTS="$(TEST_BINS:.bin=.$*)"

recheck_%: $(TEST_BINS)
	$(MAKE) recheck TESTS="$(TEST_BINS:.bin=.$*)"

CLEANFILES = $(TESTS) *.out
DISTCLEANFILES = $(TEST_BINS)

