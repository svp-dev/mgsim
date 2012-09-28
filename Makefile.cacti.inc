EXTRA_DIST += build-aux/cacti-io-cc.diff

CACTI_SOURCES = \
    cacti/arbiter.h \
    cacti/arbiter.cc \
    cacti/area.h \
    cacti/area.cc \
    cacti/bank.h \
    cacti/bank.cc \
    cacti/basic_circuit.h \
    cacti/basic_circuit.cc \
    cacti/cacti_interface.h \
    cacti/cacti_interface.cc \
    cacti/const.h \
    cacti/component.h \
    cacti/component.cc \
    cacti/crossbar.h \
    cacti/crossbar.cc \
    cacti/decoder.h \
    cacti/decoder.cc \
    cacti/htree2.h \
    cacti/htree2.cc \
    cacti/io.h \
    cacti/io_patched.cc \
    cacti/mat.h \
    cacti/mat.cc \
    cacti/nuca.h \
    cacti/nuca.cc \
    cacti/parameter.h \
    cacti/parameter.cc \
    cacti/router.h \
    cacti/router.cc \
    cacti/subarray.h \
    cacti/subarray.cc \
    cacti/technology.cc \
    cacti/uca.h \
    cacti/uca.cc \
    cacti/Ucache.h \
    cacti/Ucache.cc \
    cacti/wire.h \
    cacti/wire.cc

cacti/io_patched.cc: $(srcdir)/cacti/io.cc
	$(MKDIR_P) cacti
	(cp -f $(srcdir)/cacti/io.cc cacti/io_patched.cc && patch cacti/io_patched.cc $(srcdir)/build-aux/cacti-io-cc.diff) || rm -f cacti/io_patched.cc

CACTI_EXTRA_CPPFLAGS = -DNTHREADS=8 -I$(srcdir)/cacti
