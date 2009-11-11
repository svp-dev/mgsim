if ENABLE_COMA
noinst_LIBRARIES = libmemorys.a libmemorys_g.a
endif

AM_CPPFLAGS = -DSIMULATE_DRIVEN_MODE=CONST_DRIVEN_MODE_MIKE -DTIXML_USE_STL
AM_CXXFLAGS = -Wall

libmemorys_g_a_SOURCES = $(ALL_SOURCES)
libmemorys_g_CXXFLAGS = $(AM_CXXFLAGS) -g
libmemorys_g_CPPFLAGS = $(AM_CPPFLAGS) -DMDBLOG
libmemorys_a_SOURCES = $(ALL_SOURCES)
libmemorys_a_CPPFLAGS = $(AM_CPPFLAGS) -DNDEBUG

ALL_SOURCES = \
	busst.cpp \
	busst.h \
	busst_arbiter.h \
	busst_arbiter_if.h \
	busst_if.h \
	busst_master.h \
	busst_slave_if.h \
	cachel2tok.cpp \
	cachel2tok.h \
	cachel2tokim.cpp \
	cachel2tokim.h \
	cachest.cpp \
	cachest.h \
	dcswitch.h \
	ddrmemorysys.cpp \
	ddrmemorysys.h \
	ddrxml.cpp \
	ddrxml.h \
	directoryrttok.cpp \
	directoryrttok.h \
	directoryrttokim.cpp \
	directoryrttokim.h \
	directorytok.cpp \
	directorytok.h \
	directorytokim.cpp \
	directorytokim.h \
	drv_clk.h \
	evicteddirlinebuffer.h \
	fabuffer.h \
	flatmemorydata.h \
	membankst_if.h \
	memory_main.cpp \
	memorybankst.h \
	memorycontrollerst.h \
	memoryst.cpp \
	memoryst.h \
	memschedulepipeline.h \
	mergestorebuffer.cpp \
	mergestorebuffer.h \
	network.h \
	network_if.h \
	network_node.cpp \
	network_node.h \
	network_port.cpp \
	network_port.h \
	networkabove_if.h \
	networkbelow_if.h \
	predef.cpp \
	predef.h \
	processortok.cpp \
	processortok.h \
	requestmergebufferst.h \
	ringnode.h \
	setassociativeprop.cpp \
	setassociativeprop.h \
	simcontrol.cpp \
	simcontrol.h \
	stat_def.h \
	suspendedrequestqueue.cpp \
	suspendedrequestqueue.h \
	topologys.cpp \
	topologys.h \
	tops.h \
	tracefile.cpp \
	tracefile.h \
	traceloader.cpp \
	traceloader.h \
	vmemorydatacontainer.cpp \
	vmemorydatacontainer.h
