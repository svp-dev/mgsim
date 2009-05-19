TEST_SOURCES = \
	fibo/fibo.s \
	sine/sine_mt_o.s \
	sine/sine_mt_u.s \
	sine/sine_seq_o.s \
	sine/sine_seq_u.s \
	livermore/l1_hydro.s \
	livermore/l2_iccq.s \
	livermore/l3_innerprod.s \
	livermore/l3_innerprod_partial.s \
	livermore/l4_bandedlineareq.s \
	livermore/l5_tridiagelim.s \
	livermore/l6_genlinreceq.s \
	livermore/l7_eqofstatefrag.s \
	matmul/matmul0.s \
	matmul/matmul1.s \
	matmul/matmul2.s \
	matmul/matmul3.s \
	sine/sine_mt_o.s \
	sine/sine_mt_u.s \
	sine/sine_seq_o.s \
	sine/sine_seq_u.s \
	regression/inf_pipeline_wait_loop.s \
	regression/exclusive_places.s \
	regression/delegation1.s \
	regression/delegation2.s \
	regression/sparse_globals.s

if ENABLE_MTALPHA_TESTS
TESTS = $(TEST_SOURCES:.s=.bin)
endif

ARCH = alpha
include $(srcdir)/../tests.mk


