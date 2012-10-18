MIPS_TEST_SOURCES = \
    tests/mips/simple.s \
    tests/mips/addsub.s \
    tests/mips/arith_branches.s \
    tests/mips/memory.s \
    tests/mips/hilo.s

if ENABLE_MIPS_TESTS
TEST_ARCH       = mips
TEST_BINS       += $(MIPS_TEST_SOURCES:.s=.bin)
endif

if ENABLE_MIPSEL_TESTS
TEST_ARCH       = mipsel
TEST_BINS       += $(MIPS_TEST_SOURCES:.s=.bin)
endif

EXTRA_DIST += $(MIPS_TEST_SOURCES)