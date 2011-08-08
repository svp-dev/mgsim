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
    cacti/io.cc \
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
    cacti/technology.h \
    cacti/technology.cc \
    cacti/uca.h \
    cacti/uca.cc \
    cacti/Ucache.h \
    cacti/Ucache.cc \
    cacti/wire.h \
    cacti/wire.cc

CACTI_EXTRA_CXXFLAGS = -DNTHREADS=8
