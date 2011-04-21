noinst_LIBRARIES = libsim.a

SOURCES = \
	breakpoints.cpp \
	breakpoints.h \
	config.cpp \
	config.h \
	delegate.h \
	display.h \
	display.cpp \
	except.h \
	except.cpp \
	kernel.h \
	kernel.cpp \
	log2.h \
	ports.h \
	ports.cpp \
	range.h \
	sampling.h \
	sampling.cpp \
	storage.h \
	types.h

FEATURE_FLAGS=

if ENABLE_MONITOR
SOURCES += monitor.h monitor.cpp
FEATURE_FLAGS += $(PTHREAD_CFLAGS) -DENABLE_MONITOR=1
endif

if ENABLE_SDL
FEATURE_FLAGS += $(SDL_CFLAGS) -DENABLE_SDL=1 
endif

libsim_a_SOURCES = $(SOURCES)
libsim_a_CXXFLAGS = $(AM_CXXFLAGS) $(FEATURE_FLAGS)
