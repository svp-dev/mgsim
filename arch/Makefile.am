noinst_LIBRARIES =

FLAGS_OPT = -O3 -DNDEBUG
FLAGS_DEBUG = -g -DDEBUG 

FLAGS_COMMON = -W -Wall -Wwrite-strings -Wno-unused \
       -DMGSIM_CONFIG_PATH=\"$(pkgdatadir)/config.ini\"

MEMORY_SRC = \
	mem/BankedMemory.cpp \
	mem/BankedMemory.h \
	mem/ParallelMemory.cpp \
	mem/ParallelMemory.h \
	mem/RandomBankedMemory.cpp \
	mem/RandomBankedMemory.h \
	mem/SerialMemory.cpp \
	mem/SerialMemory.h

DEVICE_SRC = \
	dev/lcd.cpp \
	dev/lcd.h \
	dev/lineprinter.cpp \
	dev/lineprinter.h

PROCESSOR_SRC = \
	proc/Allocator.cpp \
	proc/Allocator.h \
	proc/DCache.cpp \
	proc/DCache.h \
	proc/DecodeStage.cpp \
	proc/DummyStage.cpp \
	proc/ExecuteStage.cpp \
	proc/FamilyTable.cpp \
	proc/FamilyTable.h \
	proc/FetchStage.cpp \
	proc/ICache.cpp \
	proc/ICache.h \
	proc/MemoryStage.cpp \
	proc/Network.cpp \
	proc/Network.h \
	proc/Pipeline.cpp \
	proc/Pipeline.h \
	proc/Processor.cpp \
	proc/Processor.h \
	proc/RAUnit.cpp \
	proc/RAUnit.h \
	proc/ReadStage.cpp \
	proc/RegisterFile.cpp \
	proc/RegisterFile.h \
	proc/ThreadTable.cpp \
	proc/ThreadTable.h \
	proc/WritebackStage.cpp \
	proc/counters.h \
	proc/counters.cpp

COMMON_SRC = \
    $(MEMORY_SRC) \
    $(DEVICE_SRC) \
    $(PROCESSOR_SRC) \
	elf.h \
	FPU.cpp \
	FPU.h \
	Memory.h \
	MMIO.h \
	MMIO.cpp \
	MGSystem.h \
	MGSystem.cpp \
	loader.cpp \
	loader.h \
	VirtualMemory.cpp \
	VirtualMemory.h

ZLCOMA_SRC = \
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

MLCOMA_SRC = \
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

ALPHA_SRC = $(COMMON_SRC) \
	ISA.alpha.h \
	ISA.alpha.cpp

SPARC_SRC = $(COMMON_SRC) \
	ISA.sparc.h \
	ISA.sparc.cpp

FLAGS_ALPHA = -DTARGET_ARCH=ARCH_ALPHA
FLAGS_SPARC = -DTARGET_ARCH=ARCH_SPARC

if ENABLE_MTALPHA
ALPHA_LIBS = \
    libalpha.a \
	libalpha_zlcoma.a \
	libalpha_mlcoma.a
	
ALPHA_LIBS_G = \
    libalpha_g.a \
	libalpha_zlcoma_g.a \
	libalpha_mlcoma_g.a

noinst_LIBRARIES += $(ALPHA_LIBS) $(ALPHA_LIBS_G)

libalpha_zlcoma_a_SOURCES = $(ZLCOMA_SRC)
libalpha_zlcoma_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON)
libalpha_mlcoma_a_SOURCES = $(MLCOMA_SRC)
libalpha_mlcoma_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON)
libalpha_a_SOURCES = $(COMMON_SRC) $(ALPHA_SRC)
libalpha_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON)

libalpha_zlcoma_g_a_SOURCES = $(ZLCOMA_SRC)
libalpha_zlcoma_g_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)
libalpha_mlcoma_g_a_SOURCES = $(MLCOMA_SRC)
libalpha_mlcoma_g_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)
libalpha_g_a_SOURCES = $(COMMON_SRC) $(ALPHA_SRC)
libalpha_g_a_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)
endif

if ENABLE_MTSPARC
SPARC_LIBS = \
    libsparc.a \
	libsparc_zlcoma.a \
	libsparc_mlcoma.a
	
SPARC_LIBS_G = \
    libsparc_g.a \
	libsparc_zlcoma_g.a \
	libsparc_mlcoma_g.a

noinst_LIBRARIES += $(SPARC_LIBS) $(SPARC_LIBS_G)

libsparc_zlcoma_a_SOURCES = $(ZLCOMA_SRC)
libsparc_zlcoma_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON)
libsparc_mlcoma_a_SOURCES = $(MLCOMA_SRC)
libsparc_mlcoma_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON)
libsparc_a_SOURCES = $(COMMON_SRC) $(SPARC_SRC)
libsparc_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON)

libsparc_zlcoma_g_a_SOURCES = $(ZLCOMA_SRC)
libsparc_zlcoma_g_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)
libsparc_mlcoma_g_a_SOURCES = $(MLCOMA_SRC)
libsparc_mlcoma_g_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)
libsparc_g_a_SOURCES = $(COMMON_SRC) $(SPARC_SRC)
libsparc_g_a_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)
endif
