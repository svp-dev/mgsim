# PROGRAMS is redefined below.
bin_PROGRAMS =
dist_bin_SCRIPTS = simx-cleanup-sems.sh
noinst_PROGRAMS = 
noinst_LIBRARIES =
dist_man1_MANS =

FLAGS_OPT = -O3 -DNDEBUG 
FLAGS_DEBUG = -g -DDEBUG 

FLAGS_COMMON = -W -Wall -Wwrite-strings -Wno-unused \
       -DMGSIM_CONFIG_PATH=\"$(pkgdatadir)/config.ini\"
#       -Wshorten-64-to-32 -Wextra
#       -Weffc++ -Woverloaded-virtual 

FLAGS_COMA = -DENABLE_COMA=1 -DTARGET_ARCH=ARCH_ALPHA


COMA_LDADD = $(SC_LIBS)
COMA_LDADD_G = $(SC_LIBS)
COMA_LIBS = libmemorys.a libsimlink.a  
COMA_LIBS_G = libmemorys_g.a libsimlink_g.a 
COMA_SRC = \
	CMLink.cpp \
	CMLink.h 

MEMORYS_SRC = \
	coma/memorys/busswitch.h \
	coma/memorys/busswitch.cpp \
	coma/memorys/busst.cpp \
	coma/memorys/busst.h \
	coma/memorys/busst_if.h \
	coma/memorys/busst_master.h \
	coma/memorys/busst_slave_if.h \
	coma/memorys/cachel2tok.cpp \
	coma/memorys/cachel2tok.h \
	coma/memorys/cachel2tokim.cpp \
	coma/memorys/cachel2tokim.h \
	coma/memorys/cachest.cpp \
	coma/memorys/cachest.h \
	coma/memorys/ddrmemorysys.cpp \
	coma/memorys/ddrmemorysys.h \
	coma/memorys/directoryrttok.cpp \
	coma/memorys/directoryrttok.h \
	coma/memorys/directorytok.cpp \
	coma/memorys/directorytok.h \
	coma/memorys/drv_clk.h \
	coma/memorys/evicteddirlinebuffer.h \
	coma/memorys/fabuffer.h \
	coma/memorys/memory_main.cpp \
	coma/memorys/memschedulepipeline.h \
	coma/memorys/mergestorebuffer.cpp \
	coma/memorys/mergestorebuffer.h \
	coma/memorys/network.h \
	coma/memorys/network_if.h \
	coma/memorys/network_node.h \
	coma/memorys/networkabove_if.h \
	coma/memorys/networkbelow_if.h \
	coma/memorys/predef.cpp \
	coma/memorys/predef.h \
	coma/memorys/processortok.cpp \
	coma/memorys/processortok.h \
	coma/memorys/requestmergebufferst.h \
	coma/memorys/simcontrol.h \
	coma/memorys/suspendedrequestqueue.cpp \
	coma/memorys/suspendedrequestqueue.h \
	coma/memorys/topologys.cpp \
	coma/memorys/topologys.h

SIMLINK_SRC = \
	coma/simlink/linkmgs.h \
	coma/simlink/linkprocs.cpp \
	coma/simlink/linkrealization.h \
	coma/simlink/memstat.h \
	coma/simlink/smdatatype.h \
	coma/simlink/th.h


COMMON_SRC = \
	Allocator.cpp \
	Allocator.h \
	Archures.h \
	BankedMemory.cpp \
	BankedMemory.h \
	DCache.cpp \
	DCache.h \
	DecodeStage.cpp \
	DummyStage.cpp \
	FPU.cpp \
	FPU.h \
	FamilyTable.cpp \
	FamilyTable.h \
	FetchStage.cpp \
	ICache.cpp \
	ICache.h \
	Memory.h \
	MemoryStage.cpp \
	Network.cpp \
	Network.h \
	ParallelMemory.cpp \
	ParallelMemory.h \
	Pipeline.cpp \
	Pipeline.h \
	Processor.cpp \
	Processor.h \
	RandomBankedMemory.cpp \
	RandomBankedMemory.h \
	RAUnit.cpp \
	RAUnit.h \
	ReadStage.cpp \
	RegisterFile.cpp \
	RegisterFile.h \
	SerialMemory.cpp \
	SerialMemory.h \
	ThreadTable.cpp \
	ThreadTable.h \
	VirtualMemory.cpp \
	VirtualMemory.h \
	WritebackStage.cpp \
	config.cpp \
	config.h \
	delegate.h \
	elf.h \
	except.h \
	except.cpp \
	kernel.h \
	loader.cpp \
	loader.h \
	ports.h \
	ports.cpp \
	range.h \
	simtypes.cpp \
	simtypes.h \
	symtable.h \
	symtable.cpp \
	storage.h \
	types.h \
	coma/COMA.h \
	coma/COMA.cpp \
	coma/Cache.h \
	coma/Cache.cpp \
	coma/Directory.h \
	coma/Directory.cpp \
	coma/Node.h \
	coma/Node.cpp \
	coma/RootDirectory.h \
	coma/RootDirectory.cpp \
	coma/DDR.h \
	coma/DDR.cpp

SPECIAL_SRC = \
	ExecuteStage.cpp \
	kernel.cpp \
	MGSystem.h \
	MGSystem.cpp \
	commands.h \
	commands.cpp \
	simreadline.h \
	simreadline.cpp \
	main.cpp \
	display.h \
	display.cpp

FLAGS_SPECIAL = -DGRAPHICS_SENSITIVE_COMPILE

ALPHA_SRC = $(COMMON_SRC) \
	ISA.alpha.h \
	ISA.alpha.cpp

SPARC_SRC = $(COMMON_SRC) \
	ISA.sparc.h \
	ISA.sparc.cpp

FLAGS_ALPHA = -DTARGET_ARCH=ARCH_ALPHA -DCORE_ISA_NAME=\"DEC\ Alpha\"
FLAGS_SPARC = -DTARGET_ARCH=ARCH_SPARC -DCORE_ISA_NAME=\"SPARC\ v8\"

if ENABLE_USE_SDL
GFX_FLAGS = $(SDL_CFLAGS) -DUSE_SDL=1 
endif

if ENABLE_COMA
noinst_LIBRARIES += libsimlink.a libsimlink_g.a

libsimlink_g_a_SOURCES = $(SIMLINK_SRC)
libsimlink_g_a_CXXFLAGS = $(FLAGS_COMMON) $(FLAGS_DEBUG) $(FLAGS_COMA)

libsimlink_a_SOURCES = $(SIMLINK_SRC)
libsimlink_a_CXXFLAGS = $(FLAGS_COMMON) $(FLAGS_OPT) $(FLAGS_COMA)

noinst_LIBRARIES += libmemorys.a libmemorys_g.a

libmemorys_g_a_SOURCES = $(MEMORYS_SRC)
libmemorys_g_a_CXXFLAGS = $(FLAGS_COMMON) $(FLAGS_DEBUG) $(FLAGS_COMA)

libmemorys_a_SOURCES = $(MEMORYS_SRC)
libmemorys_a_CXXFLAGS = $(FLAGS_COMMON) $(FLAGS_OPT) $(FLAGS_COMA)

endif


if ENABLE_MTALPHA

bin_PROGRAMS += mgsim-alpha
noinst_PROGRAMS += mgsim-alpha.dbg 
dist_man1_MANS += mgsim-alpha.1

noinst_LIBRARIES += libalpha.a libalpha_g.a 

libalpha_a_SOURCES = $(COMMON_SRC) $(ALPHA_SRC)
libalpha_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON)

libalpha_g_a_SOURCES = $(COMMON_SRC) $(ALPHA_SRC)
libalpha_g_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)

mgsim_alpha_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON) $(FLAGS_SPECIAL)
mgsim_alpha_LDADD = libalpha.a

mgsim_alpha_dbg_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_dbg_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(FLAGS_SPECIAL)
mgsim_alpha_dbg_LDADD = libalpha_g.a

if ENABLE_COMA
bin_PROGRAMS += simx-alpha simx-alpha.dbg
dist_man1_MANS += simx-alpha.1

simx_alpha_SOURCES = $(SPECIAL_SRC) $(COMA_SRC)
simx_alpha_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON) $(FLAGS_COMA) $(FLAGS_SPECIAL)
simx_alpha_LDADD = $(COMA_LIBS) libalpha.a $(COMA_LDADD)
simx_alpha_LDFLAGS = -pthread

simx_alpha_dbg_SOURCES = $(SPECIAL_SRC) $(COMA_SRC)
simx_alpha_dbg_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(FLAGS_COMA) $(FLAGS_SPECIAL)
simx_alpha_dbg_LDADD = $(COMA_LIBS_G) libalpha_g.a $(COMA_LDADD_G)
simx_alpha_dbg_LDFLAGS = -pthread
endif

if ENABLE_USE_SDL
bin_PROGRAMS += mgsim-alpha.gfx
noinst_PROGRAMS += mgsim-alpha.dbg.gfx 

mgsim_alpha_gfx_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_gfx_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON) $(GFX_FLAGS) $(FLAGS_SPECIAL)
mgsim_alpha_gfx_LDADD = libalpha.a $(SDL_LIBS) 

mgsim_alpha_dbg_gfx_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_dbg_gfx_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(GFX_FLAGS) $(FLAGS_SPECIAL)
mgsim_alpha_dbg_gfx_LDADD = libalpha_g.a $(SDL_LIBS)

endif

endif

if ENABLE_MTSPARC

bin_PROGRAMS += mgsim-sparc
noinst_PROGRAMS += mgsim-sparc.dbg
dist_man1_MANS += mgsim-sparc.1

noinst_LIBRARIES += libsparc.a libsparc_g.a

libsparc_a_SOURCES = $(COMMON_SRC) $(SPARC_SRC)
libsparc_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON)

libsparc_g_a_SOURCES = $(COMMON_SRC) $(SPARC_SRC)
libsparc_g_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)

mgsim_sparc_SOURCES = $(SPECIAL_SRC)
mgsim_sparc_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON) $(FLAGS_SPECIAL)
mgsim_sparc_LDADD = libsparc.a

mgsim_sparc_dbg_SOURCES = $(SPECIAL_SRC)
mgsim_sparc_dbg_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(FLAGS_SPECIAL)
mgsim_sparc_dbg_LDADD = libsparc_g.a

# if ENABLE_COMA
# bin_PROGRAMS += simx-sparc simx-sparc.dbg
# dist_man1_MANS += simx-sparc.1

# simx_sparc_SOURCES = $(SPECIAL_SRC) $(COMA_SRC)
# simx_sparc_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON) $(FLAGS_COMA)
# simx_sparc_LDADD = $(COMA_LIBS) libsparc.a $(COMA_LDADD)

# simx_sparc_dbg_SOURCES = $(SPECIAL_SRC) $(COMA_SRC)
# simx_sparc_dbg_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(FLAGS_COMA)
# simx_sparc_dbg_LDADD = $(COMA_LIBS_G) libsparc_g.a $(COMA_LDADD)
# endif

if ENABLE_USE_SDL
bin_PROGRAMS += mgsim-sparc.gfx
noinst_PROGRAMS += mgsim-sparc.dbg.gfx

mgsim_sparc_gfx_SOURCES = $(SPECIAL_SRC)
mgsim_sparc_gfx_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON) $(GFX_FLAGS) $(FLAGS_SPECIAL)
mgsim_sparc_gfx_LDADD = libsparc.a $(SDL_LIBS) 

mgsim_sparc_dbg_gfx_SOURCES = $(SPECIAL_SRC)
mgsim_sparc_dbg_gfx_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(GFX_FLAGS) $(FLAGS_SPECIAL)
mgsim_sparc_dbg_gfx_LDADD = libsparc_g.a $(SDL_LIBS)
endif

endif

## 
## Manual page rules
##
$(dist_man1_MANS): $(top_srcdir)/.version
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) $(@:.1=)
	$(AM_V_GEN)$(HELP2MAN) -N --output=$@ \
	    `test -r $(srcdir)/$(@:.1=) && echo $(srcdir) || echo .`/$(@:.1=)

MAINTAINERCLEANFILES = $(dist_man1_MANS)
