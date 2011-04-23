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
	dev/IODeviceDatabase.h \
	dev/IODeviceDatabase.cpp \
	dev/NullIO.cpp \
	dev/NullIO.h \
	dev/lcd.cpp \
	dev/lcd.h 

PROCESSOR_SRC = \
	proc/Allocator.cpp \
	proc/Allocator.h \
	proc/AncillaryRegisterFile.cpp \
	proc/AncillaryRegisterFile.h \
	proc/DCache.cpp \
	proc/DCache.h \
	proc/DebugChannel.h \
	proc/DebugChannel.cpp \
	proc/DecodeStage.cpp \
	proc/DummyStage.cpp \
	proc/ExecuteStage.cpp \
	proc/FamilyTable.cpp \
	proc/FamilyTable.h \
	proc/FetchStage.cpp \
	proc/ICache.cpp \
	proc/ICache.h \
	proc/IOResponseMultiplexer.h \
	proc/IOResponseMultiplexer.cpp \
	proc/IOInterruptMultiplexer.h \
	proc/IOInterruptMultiplexer.cpp \
	proc/IOBusInterface.h \
	proc/IOBusInterface.cpp \
	proc/IOInterface.h \
	proc/IOInterface.cpp \
	proc/IOMatchUnit.h \
	proc/IOMatchUnit.cpp \
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
	proc/PerfCounters.h \
	proc/PerfCounters.cpp

COMMON_SRC = \
    $(MEMORY_SRC) \
    $(DEVICE_SRC) \
    $(PROCESSOR_SRC) \
    Archures.h \
	elf.h \
	FPU.cpp \
	FPU.h \
	IOBus.h \
	Memory.h \
	MGSystem.h \
	MGSystem.cpp \
	loader.cpp \
	loader.h \
	simtypes.h \
	simtypes.cpp \
	symtable.h \
	symtable.cpp \
	VirtualMemory.cpp \
	VirtualMemory.h

ZLCOMA_SRC = \
	mem/zlcoma/COMA.h \
	mem/zlcoma/COMA.cpp \
	mem/zlcoma/Cache.h \
	mem/zlcoma/Cache.cpp \
	mem/zlcoma/Directory.h \
	mem/zlcoma/Directory.cpp \
	mem/zlcoma/Node.h \
	mem/zlcoma/Node.cpp \
	mem/zlcoma/RootDirectory.h \
	mem/zlcoma/RootDirectory.cpp \
	mem/zlcoma/DDR.h \
	mem/zlcoma/DDR.cpp

MLCOMA_SRC = \
	mem/coma/COMA.h \
	mem/coma/COMA.cpp \
	mem/coma/Cache.h \
	mem/coma/Cache.cpp \
	mem/coma/Directory.h \
	mem/coma/Directory.cpp \
	mem/coma/Node.h \
	mem/coma/Node.cpp \
	mem/coma/RootDirectory.h \
	mem/coma/RootDirectory.cpp \
	mem/coma/DDR.h \
	mem/coma/DDR.cpp

ALPHA_SRC = $(COMMON_SRC) \
	proc/ISA.alpha.h \
	proc/ISA.alpha.cpp

SPARC_SRC = $(COMMON_SRC) \
	proc/ISA.sparc.h \
	proc/ISA.sparc.cpp

noinst_LIBRARIES = libarch.a libarch_mlcoma.a libarch_zlcoma.a

libarch_zlcoma_a_SOURCES = $(ZLCOMA_SRC)
libarch_mlcoma_a_SOURCES = $(MLCOMA_SRC)
libarch_a_SOURCES = $(COMMON_SRC) $(ALPHA_SRC)
