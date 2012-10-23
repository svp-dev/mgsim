COMPILED_TEST_SOURCES = \
    tests/common/io.c \
    tests/common/mgcfg.c \
    tests/common/rtc.c

CLEANFILES += testinput # created by io.c

EXTRA_DIST += $(COMPILED_TEST_SOURCES)
