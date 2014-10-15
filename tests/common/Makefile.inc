COMPILED_TEST_SOURCES = \
    tests/common/io.c \
    tests/common/mgcfg.c \
    tests/common/rtc.c \
    tests/common/gfx.c \
    tests/common/breakshared.c \
    tests/common/regression/negindex.c \
    tests/common/regression/negindex2.c \
    tests/common/regression/breaknegindex.c

CLEANFILES += testinput # created by io.c

EXTRA_DIST += $(COMPILED_TEST_SOURCES)
