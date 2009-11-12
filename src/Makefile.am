# PROGRAMS is redefined below.
bin_PROGRAMS =
noinst_PROGRAMS = 

SUBDIRS = coma .

FLAGS_OPT = -O3 -DNDEBUG
FLAGS_DEBUG = -O0 -g -pg -D_GLIBCXX_DEBUG -DMDBLOG
FLAGS_COMMON = -W -Wall -Wwrite-strings -Wconversion \
       -DMGSIM_CONFIG_PATH=\"$(pkgdatadir)/config.ini\"
FLAGS_COMA = -DENABLE_COMA=1 

#       -Wshorten-64-to-32 -Wextra
#       -Weffc++ -Woverloaded-virtual 

COMA_LDADD = -lsystemc -ltinyxml
COMA_LIBS = coma/memorys/libmemorys.a coma/simlink/libsimlink.a  
COMA_LIBS_G = coma/memorys/libmemorys_g.a coma/simlink/libsimlink_g.a 
COMA_SRC = \
	SimpleMemory.cpp \
	SimpleMemory.h \
	memdump.h \
	memdump.cpp \
	CMLink.cpp \
	CMLink.h 

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
	ExecuteStage.cpp \
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
	elf.h \
	except.h \
	except.cpp \
	kernel.h \
	loader.cpp \
	loader.h \
	ports.h \
	ports.cpp \
	range.h \
	simreadline.h \
	simtypes.cpp \
	simtypes.h \
	storage.h \
	storage.cpp \
	memsstoresequence.cpp \
	memsstoresequence.h \
	memstrace.cpp \
	memstrace.h \
	types.h

SPECIAL_SRC = \
	kernel.cpp \
	main.cpp \
	gfx.h \
	gfx.cpp

EXTRA_DIST = gfxscaler.cpp gfxscreen.cpp

ALPHA_SRC = $(COMMON_SRC) \
	ISA.alpha.h \
	ISA.alpha.cpp

SPARC_SRC = $(COMMON_SRC) \
	ISA.sparc.h \
	ISA.sparc.cpp

FLAGS_ALPHA = -DTARGET_ARCH=ARCH_ALPHA
FLAGS_SPARC = -DTARGET_ARCH=ARCH_SPARC

if ENABLE_USE_SDL
GFX_FLAGS = $(SDL_CFLAGS) -DUSE_SDL=1
endif


if ENABLE_MTALPHA

bin_PROGRAMS += mgsim-alpha
noinst_PROGRAMS += mgsim-alpha.dbg 

noinst_LIBRARIES = libalpha.a libalpha_g.a 

libalpha_a_SOURCES = $(COMMON_SRC) $(ALPHA_SRC)
libalpha_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON)

libalpha_g_a_SOURCES = $(COMMON_SRC) $(ALPHA_SRC)
libalpha_g_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)

mgsim_alpha_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON)
mgsim_alpha_LDADD = libalpha.a

mgsim_alpha_dbg_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_dbg_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)
mgsim_alpha_dbg_LDADD = libalpha_g.a

if ENABLE_COMA
bin_PROGRAMS += simx-alpha simx-alpha.dbg

simx_alpha_SOURCES = $(SPECIAL_SRC) $(COMA_SRC)
simx_alpha_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON) $(FLAGS_COMA)
simx_alpha_LDADD = $(COMA_LIBS) libalpha.a $(COMA_LDADD)

simx_alpha_dbg_SOURCES = $(SPECIAL_SRC) $(COMA_SRC)
simx_alpha_dbg_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(FLAGS_COMA)
simx_alpha_dbg_LDADD = $(COMA_LIBS_G) libalpha_g.a $(COMA_LDADD)
endif

if ENABLE_USE_SDL
bin_PROGRAMS += mgsim-alpha.gfx
noinst_PROGRAMS += mgsim-alpha.dbg.gfx 

mgsim_alpha_gfx_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_gfx_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON) $(GFX_FLAGS)
mgsim_alpha_gfx_LDADD = libalpha.a $(SDL_LIBS) 

mgsim_alpha_dbg_gfx_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_dbg_gfx_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(GFX_FLAGS)
mgsim_alpha_dbg_gfx_LDADD = libalpha_g.a $(SDL_LIBS)

endif

endif

if ENABLE_MTSPARC

bin_PROGRAMS += mgsim-sparc
noinst_PROGRAMS += mgsim-sparc.dbg

noinst_LIBRARIES = libsparc.a libsparc_g.a

libsparc_a_SOURCES = $(COMMON_SRC) $(SPARC_SRC)
libsparc_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON)

libsparc_g_a_SOURCES = $(COMMON_SRC) $(SPARC_SRC)
libsparc_g_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)

mgsim_sparc_SOURCES = $(SPECIAL_SRC)
mgsim_sparc_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON)
mgsim_sparc_LDADD = libsparc.a

mgsim_sparc_dbg_SOURCES = $(SPECIAL_SRC)
mgsim_sparc_dbg_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)
mgsim_sparc_dbg_LDADD = libsparc_g.a

# if ENABLE_COMA
# bin_PROGRAMS += simx-sparc simx-sparc.dbg

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
mgsim_sparc_gfx_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON) $(GFX_FLAGS)
mgsim_sparc_gfx_LDADD = libsparc.a $(SDL_LIBS) 

mgsim_sparc_dbg_gfx_SOURCES = $(SPECIAL_SRC)
mgsim_sparc_dbg_gfx_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(GFX_FLAGS)
mgsim_sparc_dbg_gfx_LDADD = libsparc_g.a $(SDL_LIBS)
endif

endif
