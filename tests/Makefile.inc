TEST_BINS =
SUFFIXES = .c .s .test .log
include tests/common/Makefile.inc
include tests/mtalpha/Makefile.inc
include tests/mtsparc/Makefile.inc
include tests/mips/Makefile.inc

# The "slc" command is used by the various target test suites.
# We need to sandwich -lc between two uses of -lmgos since they
# are mutually dependent and the linker may be single-pass.
COMMON_SLC_OPTIONS = $(top_srcdir)/programs/mtconf.c -nostartfiles -lmgos -lc -lmgos -I$(top_srcdir)/programs
COMMON_SLC_DEPS = $(top_srcdir)/programs/mtconf.c $(top_srcdir)/programs/mgsim.h

# Command used to run tests by Automake
RUNTEST_SH = \
   $(SHELL) $(srcdir)/tools/runtest.sh \
	$(builddir)/mgsim \
	$(srcdir)/tools/timeout \
	$(srcdir)/programs/config.ini \
	`test -f programs/nobounds.ini || echo '$(srcdir)/'`programs/nobounds.ini

MEMORIES = 
if ENABLE_MEM_SERIAL
MEMORIES += serial
endif
if ENABLE_MEM_PARALLEL
MEMORIES += parallel
endif
if ENABLE_MEM_BANKED
MEMORIES += banked randombanked
endif
if ENABLE_MEM_COMA
MEMORIES += coma
endif
if ENABLE_MEM_DDR
MEMORIES += ddr randomddr
endif
if ENABLE_MEM_ZLCOMA
MEMORIES += zlcoma
endif
PSIZES = 1 2 4 16
TEST_LIST = $(foreach P,$(PSIZES),$(foreach M,$(MEMORIES),$(foreach T,$(TEST_BINS),$(T).$(M).$(P).test)))

check_DATA = $(TEST_BINS)
TESTS = @GET_TEST_LIST@ # ugly hack to prevent Automake from trying to understand foreach above.

.PHONY: smoketest check_% recheck_%

smoketest: $(TEST_BINS)
	$(MAKE) check TESTS="$(foreach P,$(firstword $(PSIZES)),$(foreach M,$(firstword $(MEMORIES)),$(foreach T,$(TEST_BINS),$(T).$(M).$(P).test)))"

check_%: $(TEST_BINS)
	$(MAKE) check TESTS="$(foreach P,$(PSIZES),$(foreach T,$(TEST_BINS),$(T).$*.$(P).test))"

recheck_%: $(TEST_BINS)
	$(MAKE) recheck TESTS="$(foreach P,$(PSIZES),$(foreach T,$(TEST_BINS),$(T).$*.$(P).test))"

CLEANFILES += $(TESTS) $(TEST_BINS) *.out
MAINTAINERCLEANFILES += $(TEST_BINS)

.PRECIOUS: %.test

.SECONDEXPANSION:
%.test: $$(basename $$(basename $$(basename $$@)))
	$(AM_V_at)$(MKDIR_P) `dirname $@`
	$(AM_V_GEN)ncores="$(suffix $(basename $@))"; ncores=$${ncores#.}; \
	  mem="$(suffix $(basename $(basename $@)))"; mem=$${mem#.}; \
	  testbin=`test -f "$^" || echo '$(srcdir)/'`"$^"; \
	  pinfo=`strings <"$$testbin"|$(GREP) 'PLACES:'|head -n1|cut -d: -f2`; \
	  selected=yes; \
	  if test -n "$$pinfo"; then \
	    selected=no; \
	    for p in $$pinfo; do if test $$p = $$ncores; then selected=yes; break; fi; done; \
	  fi; \
	  if test $$selected = yes; then \
	   echo $(RUNTEST_SH) $$ncores $$mem $$testbin >"$@"; \
	  else \
	   echo exit 77 >"$@"; \
	  fi
	$(AM_V_at)chmod +x "$@"

