OR1K_CRT = \
    tests/or1k/crt_simple.s

COMMON_OR1K_TEST_SOURCES = \
    tests/or1k/simple.s 

EXTRA_DIST += $(COMMON_OR1K_TEST_SOURCES) $(OR1K_CRT)

# OpenRISC test selection
OR1K_TEST_BINS = $(COMMON_OR1K_TEST_SOURCES:.s=.or1k-bin)
OR1K_CTEST_BINS = $(COMPILED_TEST_SOURCES:.c=.or1k-bin)

#if ENABLE_COMPILED_OR1K_TESTS
#TEST_BINS += $(OR1K_CTEST_BINS)
#endif

if ENABLE_OR1K_TESTS
TEST_BINS += $(OR1K_TEST_BINS)
endif

SUFFIXES += .or1k-o .or1k-bin

.s.or1k-o:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(AS_OR1K) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.or1k-o.or1k-bin:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(LD_OR1K) -e main -Ttext 0x10000 -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.c.or1k-bin: $(COMMON_SLC_DEPS)
	$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(SLC) -b mips_s -o $@ $(srcdir)/tests/or1k/crt_simple.s `test -f "$<" || echo "$(srcdir)"/`$< $(COMMON_SLC_OPTIONS)


