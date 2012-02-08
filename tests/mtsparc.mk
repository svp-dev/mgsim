MTSPARC_TEST_SOURCES = \
    mtsparc/fibo/fibo.s \
    mtsparc/sine/sine_mt_o.s \
    mtsparc/livermore/l1_hydro.s \
    mtsparc/livermore/l2_iccg.s \
    mtsparc/livermore/l3_innerprod.s \
    mtsparc/livermore/l4_bandedlineareq.s \
    mtsparc/livermore/l5_tridiagelim.s \
    mtsparc/livermore/l6_genlinreceq.s \
    mtsparc/livermore/l7_eqofstatefrag.s \
    mtsparc/matmul/matmul0.s \
    mtsparc/matmul/matmul1.s \
    mtsparc/matmul/matmul2.s \
    mtsparc/matmul/matmul3.s \
    mtsparc/regression/inf_pipeline_wait_loop.s \
    mtsparc/regression/continuation.s \
    mtsparc/regression/break.s \
    mtsparc/regression/conc_break.s \
    mtsparc/regression/exclusive_places.s \
    mtsparc/regression/delegation1.s \
    mtsparc/regression/delegation2.s \
    mtsparc/regression/delegation_flood.s \
    mtsparc/regression/self_exclusive_delegate.s \
    mtsparc/regression/sparse_globals.s \
    mtsparc/regression/multi_shareds.s \
    mtsparc/bundle/ceb_a.s \
    mtsparc/bundle/ceb_as.s \
    mtsparc/bundle/ceb_i.s \
    mtsparc/bundle/ceb_is.s


if ENABLE_MTSPARC_TESTS
TEST_ARCH       = mtsparc
TEST_BINS       += $(MTSPARC_TEST_SOURCES:.s=.bin)
endif

EXTRA_DIST      += $(MTSPARC_TEST_SOURCES) \
	mtsparc/crt_simple.s
