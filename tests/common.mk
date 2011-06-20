COMMON_TEST_SOURCES = \
	common/mgcfg.c \
	common/rtc.c

if ENABLE_C_TESTS
TEST_BINS += $(COMMON_TEST_SOURCES:.c=.bin)
endif

EXTRA_DIST += $(COMMON_TEST_SOURCES)
