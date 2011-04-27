SIM_SOURCES = \
	sim/breakpoints.cpp \
	sim/breakpoints.h \
	sim/config.cpp \
	sim/config.h \
	sim/delegate.h \
	sim/display.h \
	sim/display.cpp \
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
	sim/sampling.h \
	sim/sampling.cpp \
	sim/storage.h \
	sim/types.h

FEATURE_FLAGS = 

if ENABLE_MONITOR
SIM_SOURCES += \
	sim/monitor.h \
	sim/monitor.cpp
FEATURE_FLAGS += $(PTHREAD_CFLAGS) -DENABLE_MONITOR=1
endif

if ENABLE_SDL
FEATURE_FLAGS += $(SDL_CFLAGS) -DENABLE_SDL=1 
endif

SIM_EXTRA_CXXFLAGS = $(FEATURE_FLAGS) 
SIM_EXTRA_CPPFLAGS = -I$(srcdir)/sim

