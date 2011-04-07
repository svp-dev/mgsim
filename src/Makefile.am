AUTOMAKE_OPTIONS = subdir-objects

# PROGRAMS is redefined below.
bin_PROGRAMS = 
bin_SCRIPTS = readtrace
noinst_PROGRAMS = 
noinst_LIBRARIES =
dist_man1_MANS =

FLAGS_OPT = -O3 -DNDEBUG
FLAGS_DEBUG = -g -DDEBUG 

FLAGS_COMMON = -W -Wall -Wwrite-strings -Wno-unused \
       -DMGSIM_CONFIG_PATH=\"$(pkgdatadir)/config.ini\"
#       -Wshorten-64-to-32 -Wextra
#       -Weffc++ -Woverloaded-virtual 

LIBS_COMMON =

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
	MMIO.h \
	MMIO.cpp \
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
	breakpoints.cpp \
	breakpoints.h \
	config.cpp \
	config.h \
	counters.h \
	counters.cpp \
	delegate.h \
	elf.h \
	except.h \
	except.cpp \
	kernel.h \
	lcd.cpp \
	lcd.h \
	lineprinter.cpp \
	lineprinter.h \
	loader.cpp \
	loader.h \
	log2.h \
	ports.h \
	ports.cpp \
	range.h \
	sampling.h \
	sampling.cpp \
	simtypes.cpp \
	simtypes.h \
	symtable.h \
	symtable.cpp \
	storage.h \
	types.h

COMA1_SRC = \
	zlcoma/COMA.h \
	zlcoma/COMA.cpp \
	zlcoma/Cache.h \
	zlcoma/Cache.cpp \
	zlcoma/Directory.h \
	zlcoma/Directory.cpp \
	zlcoma/Node.h \
	zlcoma/Node.cpp \
	zlcoma/RootDirectory.h \
	zlcoma/RootDirectory.cpp \
	zlcoma/DDR.h \
	zlcoma/DDR.cpp

COMA2_SRC = \
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

if ENABLE_MONITOR
COMMON_SRC += monitor.h monitor.cpp
FLAGS_COMMON += $(PTHREAD_CFLAGS) -DENABLE_MONITOR=1
LIBS_COMMON += $(PTHREAD_LIBS)
endif


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

if ENABLE_MTALPHA

bin_PROGRAMS += mgsim-alpha
noinst_PROGRAMS += mgsim-alpha.dbg 
dist_man1_MANS += mgsim-alpha.1

ALPHA_LIBS = libalpha.a \
	libalpha_coma1.a \
	libalpha_coma2.a
ALPHA_LIBS_G = libalpha_g.a \
	libalpha_coma1_g.a \
	libalpha_coma2_g.a

noinst_LIBRARIES += $(ALPHA_LIBS) $(ALPHA_LIBS_G)

libalpha_coma1_a_SOURCES = $(COMA1_SRC)
libalpha_coma1_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)
libalpha_coma2_a_SOURCES = $(COMA2_SRC)
libalpha_coma2_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)
libalpha_a_SOURCES = $(COMMON_SRC) $(ALPHA_SRC)
libalpha_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON)

libalpha_coma1_g_a_SOURCES = $(COMA1_SRC)
libalpha_coma1_g_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)
libalpha_coma2_g_a_SOURCES = $(COMA2_SRC)
libalpha_coma2_g_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)
libalpha_g_a_SOURCES = $(COMMON_SRC) $(ALPHA_SRC)
libalpha_g_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)

mgsim_alpha_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON) $(FLAGS_SPECIAL)
mgsim_alpha_LDADD = $(ALPHA_LIBS)
mgsim_alpha_LDFLAGS = $(LIBS_COMMON)

mgsim_alpha_dbg_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_dbg_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(FLAGS_SPECIAL)
mgsim_alpha_dbg_LDADD = $(ALPHA_LIBS_G)
mgsim_alpha_dbg_LDFLAGS = $(LIBS_COMMON)

if ENABLE_USE_SDL
bin_PROGRAMS += mgsim-alpha.gfx
noinst_PROGRAMS += mgsim-alpha.dbg.gfx 

mgsim_alpha_gfx_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_gfx_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON) $(GFX_FLAGS) $(FLAGS_SPECIAL)
mgsim_alpha_gfx_LDADD = $(ALPHA_LIBS) $(SDL_LIBS) 
mgsim_alpha_gfx_LDFLAGS = $(LIBS_COMMON)

mgsim_alpha_dbg_gfx_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_dbg_gfx_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(GFX_FLAGS) $(FLAGS_SPECIAL)
mgsim_alpha_dbg_gfx_LDADD = $(ALPHA_LIBS_G) $(SDL_LIBS)
mgsim_alpha_dbg_gfx_LDFLAGS = $(LIBS_COMMON)

endif

endif

if ENABLE_MTSPARC

bin_PROGRAMS += mgsim-sparc
noinst_PROGRAMS += mgsim-sparc.dbg
dist_man1_MANS += mgsim-sparc.1

SPARC_LIBS = libsparc.a \
	libsparc_coma1.a \
	libsparc_coma2.a
SPARC_LIBS_G = libsparc_g.a \
	libsparc_coma1_g.a \
	libsparc_coma2_g.a

noinst_LIBRARIES += $(SPARC_LIBS) $(SPARC_LIBS_G)

libsparc_coma1_a_SOURCES = $(COMA1_SRC)
libsparc_coma1_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)
libsparc_coma2_a_SOURCES = $(COMA2_SRC)
libsparc_coma2_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)
libsparc_a_SOURCES = $(COMMON_SRC) $(SPARC_SRC)
libsparc_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON)

libsparc_coma1_g_a_SOURCES = $(COMA1_SRC)
libsparc_coma1_g_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)
libsparc_coma2_g_a_SOURCES = $(COMA2_SRC)
libsparc_coma2_g_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)
libsparc_g_a_SOURCES = $(COMMON_SRC) $(SPARC_SRC)
libsparc_g_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)

mgsim_sparc_SOURCES = $(SPECIAL_SRC)
mgsim_sparc_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON) $(FLAGS_SPECIAL)
mgsim_sparc_LDADD = $(SPARC_LIBS)
mgsim_sparc_LDFLAGS = $(LIBS_COMMON)

mgsim_sparc_dbg_SOURCES = $(SPECIAL_SRC)
mgsim_sparc_dbg_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(FLAGS_SPECIAL)
mgsim_sparc_dbg_LDADD = $(SPARC_LIBS_G)
mgsim_sparc_dbg_LDFLAGS = $(LIBS_COMMON)

if ENABLE_USE_SDL
bin_PROGRAMS += mgsim-sparc.gfx
noinst_PROGRAMS += mgsim-sparc.dbg.gfx

mgsim_sparc_gfx_SOURCES = $(SPECIAL_SRC)
mgsim_sparc_gfx_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON) $(GFX_FLAGS) $(FLAGS_SPECIAL)
mgsim_sparc_gfx_LDADD = $(SPARC_LIBS) $(SDL_LIBS) 
mgsim_sparc_gfx_LDFLAGS = $(LIBS_COMMON)

mgsim_sparc_dbg_gfx_SOURCES = $(SPECIAL_SRC)
mgsim_sparc_dbg_gfx_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON) $(GFX_FLAGS) $(FLAGS_SPECIAL)
mgsim_sparc_dbg_gfx_LDADD = $(SPARC_LIBS_G) $(SDL_LIBS)
mgsim_sparc_dbg_gfx_LDFLAGS = $(LIBS_COMMON)
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
