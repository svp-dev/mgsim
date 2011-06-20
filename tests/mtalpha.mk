MTALPHA_TEST_SOURCES = \
	mtalpha/fibo/fibo.s \
	mtalpha/sine/sine_mt_o.s \
	mtalpha/sine/sine_mt_u.s \
	mtalpha/sine/sine_seq_o.s \
	mtalpha/sine/sine_seq_u.s \
	mtalpha/livermore/l1_hydro.s \
	mtalpha/livermore/l2_iccg.s \
	mtalpha/livermore/l3_innerprod.s \
	mtalpha/livermore/l3_innerprod_partial.s \
	mtalpha/livermore/l4_bandedlineareq.s \
	mtalpha/livermore/l5_tridiagelim.s \
	mtalpha/livermore/l6_genlinreceq.s \
	mtalpha/livermore/l7_eqofstatefrag.s \
	mtalpha/matmul/matmul0.s \
	mtalpha/matmul/matmul1.s \
	mtalpha/matmul/matmul2.s \
	mtalpha/matmul/matmul3.s \
	mtalpha/regression/inf_pipeline_wait_loop.s \
	mtalpha/regression/continuation.s \
	mtalpha/regression/break.s \
	mtalpha/regression/conc_break.s \
	mtalpha/regression/exclusive_places.s \
	mtalpha/regression/delegation1.s \
	mtalpha/regression/delegation2.s \
	mtalpha/regression/delegation_flood.s \
	mtalpha/regression/self_exclusive_delegate.s \
	mtalpha/regression/sparse_globals.s

if ENABLE_MTALPHA_TESTS
TEST_ARCH = mtalpha
TEST_BINS += $(MTALPHA_TEST_SOURCES:.s=.bin)
endif

EXTRA_DIST += $(MTALPHA_TEST_SOURCES)



