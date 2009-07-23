# PROGRAMS is redefined below.
bin_PROGRAMS =
noinst_PROGRAMS = 

FLAGS_OPT = -O3 -DNDEBUG
FLAGS_DEBUG = -O0 -g -pg -D_GLIBCXX_DEBUG
FLAGS_COMMON = -W -Wall -Wwrite-strings -Wconversion \
       -DMGSIM_CONFIG_PATH=\"$(pkgdatadir)/config.ini\"

#       -Wshorten-64-to-32 -Wextra
#       -Weffc++ -Woverloaded-virtual 

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
	IdealMemory.cpp \
	IdealeMemory.h \
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
	kernel.cpp \
	kernel.h \
	loader.cpp \
	loader.h \
	main.cpp \
	ports.h \
	ports.cpp \
	range.h \
	simreadline.h \
	simtypes.cpp \
	simtypes.h \
	storage.h \
	storage.cpp \
	types.h

ALPHA_SRC = $(COMMON_SRC) \
	ISA.alpha.h \
	ISA.alpha.cpp

SPARC_SRC = $(COMMON_SRC) \
	ISA.sparc.h \
	ISA.sparc.cpp

FLAGS_ALPHA = -DTARGET_ARCH=ARCH_ALPHA
FLAGS_SPARC = -DTARGET_ARCH=ARCH_SPARC


if ENABLE_MTALPHA

bin_PROGRAMS += mgsim-alpha
noinst_PROGRAMS += mgsim-alpha.dbg

mgsim_alpha_SOURCES = $(ALPHA_SRC)
mgsim_alpha_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON)

mgsim_alpha_dbg_SOURCES = $(ALPHA_SRC)
mgsim_alpha_dbg_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)

endif

if ENABLE_MTSPARC

bin_PROGRAMS += mgsim-sparc
noinst_PROGRAMS += mgsim-sparc.dbg

mgsim_sparc_SOURCES = $(SPARC_SRC)
mgsim_sparc_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON)

mgsim_sparc_dbg_SOURCES = $(SPARC_SRC)
mgsim_sparc_dbg_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)

endif
