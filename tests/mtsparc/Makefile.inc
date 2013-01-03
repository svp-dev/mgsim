MTSPARC_CRT = \
    tests/mtsparc/crt_simple.s

MTSPARC_TEST_SOURCES = \
    tests/mtsparc/fibo/fibo.s \
    tests/mtsparc/sine/sine_mt_o.s \
    tests/mtsparc/livermore/l1_hydro.s \
    tests/mtsparc/livermore/l2_iccg.s \
    tests/mtsparc/livermore/l3_innerprod.s \
    tests/mtsparc/livermore/l4_bandedlineareq.s \
    tests/mtsparc/livermore/l5_tridiagelim.s \
    tests/mtsparc/livermore/l6_genlinreceq.s \
    tests/mtsparc/livermore/l7_eqofstatefrag.s \
    tests/mtsparc/matmul/matmul0.s \
    tests/mtsparc/matmul/matmul1.s \
    tests/mtsparc/matmul/matmul2.s \
    tests/mtsparc/matmul/matmul3.s \
    tests/mtsparc/regression/inf_pipeline_wait_loop.s \
    tests/mtsparc/regression/continuation.s \
    tests/mtsparc/regression/break.s \
    tests/mtsparc/regression/conc_break.s \
    tests/mtsparc/regression/exclusive_places.s \
    tests/mtsparc/regression/delegation1.s \
    tests/mtsparc/regression/delegation2.s \
    tests/mtsparc/regression/delegation_flood.s \
    tests/mtsparc/regression/self_exclusive_delegate.s \
    tests/mtsparc/regression/sparse_globals.s \
    tests/mtsparc/regression/multi_shareds.s \
    tests/mtsparc/bundle/ceb_a.s \
    tests/mtsparc/bundle/ceb_as.s \
    tests/mtsparc/bundle/ceb_i.s \
    tests/mtsparc/bundle/ceb_is.s

EXTRA_DIST += \
    $(MTSPARC_TEST_SOURCES) \
    $(MTSPARC_CRT)

MTSPARC_TEST_BINS = $(MTSPARC_TEST_SOURCES:.s=.mtsparc-bin)
MTSPARC_CTEST_BINS = $(COMPILED_TEST_SOURCES:.c=.mtsparc-bin)

# FIXME: slc -b mtsn_n   is missing gcc lib support for __builtin_ffs
#if ENABLE_COMPILED_MTSPARC_TESTS
#TEST_BINS += $(MTSPARC_CTEST_BINS)
#endif

if ENABLE_MTSPARC_TESTS
TEST_BINS += $(MTSPARC_TEST_BINS)
endif

SUFFIXES += .mtalpha-o .mtalpha-bin

.s.mtsparc-o:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(AS_MTSPARC) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.mtsparc-o.mtsparc-bin:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(LD_MTSPARC) -e main -Ttext 0x10000 -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.c.mtsparc-bin: $(COMMON_SLC_DEPS)
	$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(SLC) -b mtsn_n -o $@ $(srcdir)/tests/mtsparc/crt_simple.s `test -f "$<" || echo "$(srcdir)"/`$< $(COMMON_SLC_OPTIONS)
