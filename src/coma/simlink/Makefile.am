if ENABLE_COMA
noinst_LIBRARIES = libsimlink.a libsimlink_g.a
endif

AM_CPPFLAGS = -DSIMULATE_DRIVEN_MODE=CONST_DRIVEN_MODE_MIKE -DTIXML_USE_STL
AM_CXXFLAGS = -Wall

libsimlink_g_a_SOURCES = $(ALL_SOURCES)
libsimlink_g_CXXFLAGS = $(AM_CXXFLAGS) -g
libsimlink_g_CPPFLAGS = $(AM_CPPFLAGS) -DMDBLOG
libsimlink_a_SOURCES = $(ALL_SOURCES)
libsimlink_a_CPPFLAGS = $(AM_CPPFLAGS) -DNDEBUG

ALL_SOURCES = \
	linkmgs.h \
	linkprocs.cpp \
	linkrealization.h \
	memorydatacontainer.cpp \
	memorydatacontainer.h \
	memstat.h \
	mgs_main.h \
	smdatatype.h \
	th.h
