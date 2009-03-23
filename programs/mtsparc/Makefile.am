TEST_SOURCES = \
	fibo/fibo.s \
	sine/sine_mt_o.s

if ENABLE_MTSPARC_TESTS
TESTS = $(TEST_SOURCES:.s=.bin)
endif

ARCH = sparc
include $(srcdir)/../tests.mk
