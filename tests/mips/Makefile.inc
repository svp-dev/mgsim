COMMON_MIPS32_TEST_SOURCES = \
    tests/mips/simple.s \
    tests/mips/addsub.s \
    tests/mips/arith_branches.s \
    tests/mips/memory.s \
    tests/mips/hilo.s

EXTRA_DIST += $(COMMON_MIPS32_TEST_SOURCES) 

# MIPS (BE) test selection
MIPS32_TEST_BINS = $(COMMON_MIPS32_TEST_SOURCES:.s=.mips-bin)
MIPS32_CTEST_BINS = $(COMPILED_TEST_SOURCES:.c=.mips-bin)

#if ENABLE_COMPILED_MIPS32_TESTS
#TEST_BINS += $(MIPS32_CTEST_BINS)
#endif

if ENABLE_MIPS32_TESTS
TEST_BINS += $(MIPS32_TEST_BINS)
endif

SUFFIXES += .mips-o .mips-bin

.s.mips-o:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(AS_MIPS32) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.mips-o.mips-bin:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(LD_MIPS32) -e main -Ttext 0x10000 -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.c.mips-bin: $(COMMON_SLC_DEPS)
	$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(SLC) -b mips_s -o $@ $(srcdir)/tests/mips/crt_simple.s `test -f "$<" || echo "$(srcdir)"/`$< $(COMMON_SLC_OPTIONS)


# MIPS32el (LE) test selection
MIPS32EL_TEST_BINS = $(COMMON_MIPS32_TEST_SOURCES:.s=.mipsel-bin)
MIPS32EL_CTEST_BINS = $(COMPILED_TEST_SOURCES:.c=.mipsel-bin)


if ENABLE_COMPILED_MIPS32EL_TESTS
TEST_BINS += $(MIPS32EL_CTEST_BINS)
endif

if ENABLE_MIPS32EL_TESTS
TEST_BINS += $(MIPS32EL_TEST_BINS)
endif

SUFFIXES += .mipsel-o .mipsel-bin

.s.mipsel-o:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(AS_MIPS32EL) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.mipsel-o.mipsel-bin:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(LD_MIPS32EL) -e main -Ttext 0x10000 -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.c.mipsel-bin: $(COMMON_SLC_DEPS)
	$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(SLC) -b mipsel_s -o $@ $(srcdir)/tests/mips/crt_simple.s `test -f "$<" || echo "$(srcdir)"/`$< $(COMMON_SLC_OPTIONS)

