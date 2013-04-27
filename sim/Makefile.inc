SIM_SOURCES = \
	sim/breakpoints.cpp \
	sim/breakpoints.h \
        sim/configparser.cpp \
        sim/configparser.h \
	sim/config.cpp \
	sim/config.h \
        sim/ctz.h \
	sim/delegate.h \
	sim/except.h \
	sim/except.cpp \
	sim/inspect.h \
	sim/inspect.cpp \
	sim/kernel.h \
	sim/kernel.cpp \
	sim/log2.h \
	sim/ports.h \
	sim/ports.cpp \
	sim/range.h \
        sim/readfile.h \
        sim/readfile.cpp \
        sim/rusage.h \
        sim/rusage.cpp \
	sim/sampling.h \
	sim/sampling.cpp \
	sim/storage.h \
	sim/storagetrace.h \
        sim/storagetrace.cpp \
	sim/types.h \
        sim/unreachable.h

if ENABLE_MONITOR
SIM_SOURCES += \
	sim/monitor.h \
	sim/monitor.cpp
endif



