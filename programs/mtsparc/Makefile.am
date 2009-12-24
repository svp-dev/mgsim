TEST_SOURCES = \
    fibo/fibo.s \
    sine/sine_mt_o.s \
    livermore/l1_hydro.s \
    livermore/l2_iccg.s \
    livermore/l3_innerprod.s \
    livermore/l4_bandedlineareq.s \
    livermore/l5_tridiagelim.s \
    livermore/l6_genlinreceq.s \
    livermore/l7_eqofstatefrag.s \
    matmul/matmul0.s \
    matmul/matmul1.s \
    matmul/matmul2.s \
    matmul/matmul3.s \
    regression/inf_pipeline_wait_loop.s \
    regression/exclusive_places.s \
    regression/delegation1.s \
    regression/delegation2.s \
    regression/delegation_flood.s \
    regression/self_exclusive_delegate.s \
    regression/sparse_globals.s \
    regression/multi_shareds.s

if ENABLE_MTSPARC_TESTS
TESTS = $(TEST_SOURCES:.s=.bin)
CLEANFILES = $(TESTS)
endif

ARCH = sparc
include $(srcdir)/../tests.mk
