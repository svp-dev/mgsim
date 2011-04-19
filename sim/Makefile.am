noinst_LIBRARIES = libsim.a libsim_g.a

FLAGS_OPT = -O3 -DNDEBUG
FLAGS_DEBUG = -g -DDEBUG 

FLAGS_COMMON = -W -Wall -Wwrite-strings -Wno-unused \
       -DMGSIM_CONFIG_PATH=\"$(pkgdatadir)/config.ini\"

SOURCES = \
	Archures.h \
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
	simtypes.cpp \
	simtypes.h \
	storage.h \
	symtable.h \
	symtable.cpp \
	types.h

if ENABLE_MONITOR
SOURCES += monitor.h monitor.cpp
FLAGS_COMMON += $(PTHREAD_CFLAGS) -DENABLE_MONITOR=1
endif

if ENABLE_SDL
FLAGS_COMMON += $(SDL_CFLAGS) -DENABLE_SDL=1 
endif

libsim_a_SOURCES = $(SOURCES)
libsim_a_CXXFLAGS = $(FLAGS_COMMON) $(FLAGS_OPT) $(FLAGS_SPECIAL)

libsim_g_a_SOURCES = $(SOURCES)
libsim_g_a_CXXFLAGS = $(FLAGS_COMMON) $(FLAGS_DEBUG) $(FLAGS_SPECIAL)
