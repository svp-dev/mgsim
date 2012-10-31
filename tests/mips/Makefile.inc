COMMON_MIPS_TEST_SOURCES = \
    tests/mips/simple.s \
    tests/mips/addsub.s \
    tests/mips/arith_branches.s \
    tests/mips/memory.s \
    tests/mips/hilo.s

EXTRA_DIST += $(COMMON_MIPS_TEST_SOURCES) 

# MIPS (BE) test selection
MIPS_TEST_BINS = $(COMMON_MIPS_TEST_SOURCES:.s=.mips-bin)
MIPS_CTEST_BINS = $(COMPILED_TEST_SOURCES:.c=.mips-bin)

#if ENABLE_COMPILED_MIPS_TESTS
#TEST_BINS += $(MIPS_CTEST_BINS)
#endif

if ENABLE_MIPS_TESTS
TEST_BINS += $(MIPS_TEST_BINS)
endif

SUFFIXES += .mips-o .mips-bin

.s.mips-o:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(AS_MIPS) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.mips-o.mips-bin:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(LD_MIPS) -e main -Ttext 0x10000 -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.c.mips-bin:
	$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(SLC) -b mips_s -o $@ $(srcdir)/tests/mips/crt_simple.s `test -f "$<" || echo "$(srcdir)"/`$< $(COMMON_SLC_OPTIONS)


# MIPSel (LE) test selection
MIPSEL_TEST_BINS = $(COMMON_MIPS_TEST_SOURCES:.s=.mipsel-bin)
MIPSEL_CTEST_BINS = $(COMPILED_TEST_SOURCES:.c=.mipsel-bin)


if ENABLE_COMPILED_MIPSEL_TESTS
TEST_BINS += $(MIPSEL_CTEST_BINS)
endif

if ENABLE_MIPSEL_TESTS
TEST_BINS += $(MIPSEL_TEST_BINS)
endif

SUFFIXES += .mipsel-o .mipsel-bin

.s.mipsel-o:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(AS_MIPSEL) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.mipsel-o.mipsel-bin:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(LD_MIPSEL) -e main -Ttext 0x10000 -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.c.mipsel-bin:
	$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(SLC) -b mipsel_s -o $@ $(srcdir)/tests/mips/crt_simple.s `test -f "$<" || echo "$(srcdir)"/`$< $(COMMON_SLC_OPTIONS)

