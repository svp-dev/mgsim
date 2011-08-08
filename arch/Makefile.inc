MEMORY_SRC = \
	arch/mem/BankedMemory.cpp \
	arch/mem/BankedMemory.h \
	arch/mem/ParallelMemory.cpp \
	arch/mem/ParallelMemory.h \
	arch/mem/SerialMemory.cpp \
	arch/mem/SerialMemory.h \
        arch/mem/DDR.cpp \
        arch/mem/DDR.h

DEVICE_SRC = \
	arch/dev/IODeviceDatabase.h \
	arch/dev/IODeviceDatabase.cpp \
	arch/dev/NullIO.cpp \
	arch/dev/NullIO.h \
	arch/dev/RTC.h \
	arch/dev/RTC.cpp \
	arch/dev/LCD.cpp \
	arch/dev/LCD.h \
        arch/dev/Display.h \
        arch/dev/Display.cpp \
        arch/dev/ELF.h \
        arch/dev/ELFLoader.h \
        arch/dev/ELFLoader.cpp \
        arch/dev/ActiveROM.h \
        arch/dev/ActiveROM.cpp \
        arch/dev/RPC.h \
        arch/dev/RPC.cpp \
        arch/dev/RPC_unix.h \
        arch/dev/RPC_unix.cpp \
        arch/dev/RPCServiceDatabase.h \
        arch/dev/Selector.h \
        arch/dev/Selector.cpp \
        arch/dev/SMC.h \
        arch/dev/SMC.cpp \
        arch/dev/UART.h \
        arch/dev/UART.cpp

PROCESSOR_SRC = \
	arch/proc/Allocator.cpp \
	arch/proc/Allocator.h \
	arch/proc/AncillaryRegisterFile.cpp \
	arch/proc/AncillaryRegisterFile.h \
	arch/proc/DCache.cpp \
	arch/proc/DCache.h \
	arch/proc/DebugChannel.h \
	arch/proc/DebugChannel.cpp \
	arch/proc/DecodeStage.cpp \
	arch/proc/DummyStage.cpp \
	arch/proc/ExecuteStage.cpp \
	arch/proc/FamilyTable.cpp \
	arch/proc/FamilyTable.h \
	arch/proc/FetchStage.cpp \
	arch/proc/ICache.cpp \
	arch/proc/ICache.h \
	arch/proc/IOResponseMultiplexer.h \
	arch/proc/IOResponseMultiplexer.cpp \
	arch/proc/IONotificationMultiplexer.h \
	arch/proc/IONotificationMultiplexer.cpp \
	arch/proc/IOBusInterface.h \
	arch/proc/IOBusInterface.cpp \
	arch/proc/IOInterface.h \
	arch/proc/IOInterface.cpp \
	arch/proc/IOMatchUnit.h \
	arch/proc/IOMatchUnit.cpp \
        arch/proc/IODirectCacheAccess.h \
        arch/proc/IODirectCacheAccess.cpp \
        arch/proc/ISA.h \
        arch/proc/ISA.cpp \
	arch/proc/MemoryStage.cpp \
	arch/proc/Network.cpp \
	arch/proc/Network.h \
	arch/proc/Pipeline.cpp \
	arch/proc/Pipeline.h \
	arch/proc/Processor.cpp \
	arch/proc/Processor.h \
	arch/proc/RAUnit.cpp \
	arch/proc/RAUnit.h \
	arch/proc/ReadStage.cpp \
	arch/proc/RegisterFile.cpp \
	arch/proc/RegisterFile.h \
	arch/proc/ThreadTable.cpp \
	arch/proc/ThreadTable.h \
	arch/proc/WritebackStage.cpp \
	arch/proc/PerfCounters.h \
	arch/proc/PerfCounters.cpp

COMMON_SRC = \
    arch/area.cpp \
	arch/Archures.h \
        arch/BankSelector.h \
        arch/BankSelector.cpp \
	arch/FPU.cpp \
	arch/FPU.h \
	arch/IOBus.h \
        arch/IOBus.cpp \
	arch/Memory.h \
	arch/MGSystem.h \
	arch/MGSystem.cpp \
	arch/simtypes.h \
	arch/simtypes.cpp \
	arch/symtable.h \
	arch/symtable.cpp \
	arch/VirtualMemory.cpp \
	arch/VirtualMemory.h

ZLCOMA_SRC = \
	arch/mem/zlcoma/COMA.h \
	arch/mem/zlcoma/COMA.cpp \
	arch/mem/zlcoma/Cache.h \
	arch/mem/zlcoma/Cache.cpp \
	arch/mem/zlcoma/Directory.h \
	arch/mem/zlcoma/Directory.cpp \
	arch/mem/zlcoma/Node.h \
	arch/mem/zlcoma/Node.cpp \
	arch/mem/zlcoma/RootDirectory.h \
	arch/mem/zlcoma/RootDirectory.cpp 

MLCOMA_SRC = \
	arch/mem/coma/COMA.h \
	arch/mem/coma/COMA.cpp \
	arch/mem/coma/Cache.h \
	arch/mem/coma/Cache.cpp \
	arch/mem/coma/Directory.h \
	arch/mem/coma/Directory.cpp \
	arch/mem/coma/Node.h \
	arch/mem/coma/Node.cpp \
	arch/mem/coma/RootDirectory.h \
	arch/mem/coma/RootDirectory.cpp 

EXTRA_DIST += \
	arch/proc/ISA.mtalpha.h \
	arch/proc/ISA.mtalpha.cpp \
	arch/proc/ISA.mtsparc.h \
	arch/proc/ISA.mtsparc.cpp

ARCH_SOURCES = $(COMMON_SRC) $(PROCESSOR_SRC) $(MEMORY_SRC) $(DEVICE_SRC) $(ZLCOMA_SRC) $(MLCOMA_SRC)

ARCH_EXTRA_CPPFLAGS = \
       -I$(srcdir)/arch -I$(builddir)/arch \
       -I$(srcdir)/arch/proc -I$(builddir)/arch/proc
