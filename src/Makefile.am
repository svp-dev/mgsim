bin_PROGRAMS = mgsim-alpha mgsim-sparc
noinst_PROGRAMS = mgsim-alpha.dbg mgsim-sparc.dbg

FLAGS_ALPHA = -DTARGET_ARCH=ARCH_ALPHA
FLAGS_SPARC = -DTARGET_ARCH=ARCH_SPARC
FLAGS_OPT = -O3 -DNDEBUG
FLAGS_DEBUG = -O0 -g -pg
FLAGS_COMMON = -Wall -Wextra -Wwrite-strings -Wconversion \
       -Wshorten-64-to-32 \
       -Weffc++ -Woverloaded-virtual \
       -DMGSIM_CONFIG_PATH=\"$(pkgdatadir)/config.ini\"

COMMON_SRC = \
	Allocator.cpp \
	Allocator.h \
	Archures.h \
	BankedMemory.cpp \
	BankedMemory.h \
	DCache.cpp \
	DCache.h \
	DecodeStage.cpp \
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
	RAUnit.cpp \
	RAUnit.h \
	ReadStage.cpp \
	RegisterFile.cpp \
	RegisterFile.h \
	SimpleMemory.cpp \
	SimpleMemory.h \
	ThreadTable.cpp \
	ThreadTable.h \
	VirtualMemory.cpp \
	VirtualMemory.h \
	WritebackStage.cpp \
	buffer.h \
	commands.cpp \
	commands.h \
	config.cpp \
	config.h \
	elf.h \
	except.cpp \
	except.h \
	functions.cpp \
	functions.h \
	kernel.cpp \
	kernel.h \
	loader.cpp \
	loader.h \
	main.cpp \
	ports.h \
	profile.cpp \
	profile.h \
	simreadline.h \
	simtime.cpp \
	simtime.h \
	simtypes.cpp \
	simtypes.h \
	types.h

ALPHA_SRC = $(COMMON_SRC) \
	ISA.alpha.h \
	ISA.alpha.cpp

SPARC_SRC = $(COMMON_SRC) \
	ISA.sparc.h \
	ISA.sparc.cpp


mgsim_alpha_SOURCES = $(ALPHA_SRC)
mgsim_alpha_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON)

mgsim_alpha_dbg_SOURCES = $(ALPHA_SRC)
mgsim_alpha_dbg_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)

mgsim_sparc_SOURCES = $(SPARC_SRC)
mgsim_sparc_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON)

mgsim_sparc_dbg_SOURCES = $(SPARC_SRC)
mgsim_sparc_dbg_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)

