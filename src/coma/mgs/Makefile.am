noinst_LIBRARIES = libmgs.a libmgs_g.a

AM_CPPFLAGS = -DSIMULATE_DRIVEN_MODE=CONST_DRIVEN_MODE_MIKE -DTIXML_USE_STL
AM_CXXFLAGS = -Wall

libmgs_g_a_SOURCES = $(ALL_SOURCES)
libmgs_g_CXXFLAGS = $(AM_CXXFLAGS) -g
libmgs_g_CPPFLAGS = $(AM_CPPFLAGS) -DMDBLOG
libmgs_a_SOURCES = $(ALL_SOURCES)
libmgs_a_CPPFLAGS = $(AM_CPPFLAGS) -DNDEBUG

ALL_SOURCES = \
	CMLink.cpp \
	CMLink.h \
	memsstoresequence.cpp \
	memsstoresequence.h \
	memstrace.cpp \
	memstrace.h \
	profile.cpp \
	profile.h \
	simtime.cpp \
	simtime.h
