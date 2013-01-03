MTALPHA_CRT = \
        tests/mtalpha/crt_simple.s

MTALPHA_TEST_SOURCES = \
	tests/mtalpha/fibo/fibo.s \
	tests/mtalpha/sine/sine_mt_o.s \
	tests/mtalpha/sine/sine_mt_u.s \
	tests/mtalpha/sine/sine_seq_o.s \
	tests/mtalpha/sine/sine_seq_u.s \
	tests/mtalpha/livermore/l1_hydro.s \
	tests/mtalpha/livermore/l2_iccg.s \
	tests/mtalpha/livermore/l3_innerprod.s \
	tests/mtalpha/livermore/l3_innerprod_partial.s \
	tests/mtalpha/livermore/l4_bandedlineareq.s \
	tests/mtalpha/livermore/l5_tridiagelim.s \
	tests/mtalpha/livermore/l6_genlinreceq.s \
	tests/mtalpha/livermore/l7_eqofstatefrag.s \
	tests/mtalpha/matmul/matmul0.s \
	tests/mtalpha/matmul/matmul1.s \
	tests/mtalpha/matmul/matmul2.s \
	tests/mtalpha/matmul/matmul3.s \
	tests/mtalpha/regression/inf_pipeline_wait_loop.s \
	tests/mtalpha/regression/continuation.s \
	tests/mtalpha/regression/break.s \
	tests/mtalpha/regression/conc_break.s \
	tests/mtalpha/regression/exclusive_places.s \
	tests/mtalpha/regression/delegation1.s \
	tests/mtalpha/regression/delegation2.s \
	tests/mtalpha/regression/delegation_flood.s \
	tests/mtalpha/regression/self_exclusive_delegate.s \
	tests/mtalpha/regression/sparse_globals.s \
	tests/mtalpha/regression/jsr.s \
	tests/mtalpha/regression/emptyfam.s \
	tests/mtalpha/bundle/ceb_a.s \
	tests/mtalpha/bundle/ceb_as.s \
	tests/mtalpha/bundle/ceb_i.s \
	tests/mtalpha/bundle/ceb_is.s

EXTRA_DIST += \
    $(MTALPHA_TEST_SOURCES) \
    $(MTALPHA_CRT)

MTALPHA_TEST_BINS = $(MTALPHA_TEST_SOURCES:.s=.mtalpha-bin)
MTALPHA_CTEST_BINS = $(COMPILED_TEST_SOURCES:.c=.mtalpha-bin)

if ENABLE_COMPILED_MTALPHA_TESTS
TEST_BINS += $(MTALPHA_CTEST_BINS)
endif

if ENABLE_MTALPHA_TESTS
TEST_BINS += $(MTALPHA_TEST_BINS)
endif

SUFFIXES += .mtalpha-o .mtalpha-bin

.s.mtalpha-o:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(AS_MTALPHA) -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.mtalpha-o.mtalpha-bin:
	$(AM_V_at)$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(LD_MTALPHA) -e main -Ttext 0x10000 -o $@ `test -f "$<" || echo "$(srcdir)"/`$<

.c.mtalpha-bin: $(COMMON_SLC_DEPS)
	$(MKDIR_P) `dirname "$@"`
	$(AM_V_GEN)$(SLC) -b mta_n -o $@ $(srcdir)/tests/mtalpha/crt_simple.s `test -f "$<" || echo "$(srcdir)"/`$< $(COMMON_SLC_OPTIONS)

