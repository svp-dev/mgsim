MEMORY_SRC = \
        arch/mem/DDR.cpp \
        arch/mem/DDR.h

if ENABLE_MEM_BANKED
MEMORY_SRC += \
	arch/mem/BankedMemory.cpp \
	arch/mem/BankedMemory.h
endif
if ENABLE_MEM_PARALLEL
MEMORY_SRC += \
	arch/mem/ParallelMemory.cpp \
	arch/mem/ParallelMemory.h
endif
if ENABLE_MEM_SERIAL
MEMORY_SRC += \
	arch/mem/SerialMemory.cpp \
	arch/mem/SerialMemory.h
endif
if ENABLE_MEM_DDR
MEMORY_SRC += \
        arch/mem/DDRMemory.cpp \
        arch/mem/DDRMemory.h
endif

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
        arch/drisc/ActionInterface.cpp \
        arch/drisc/ActionInterface.h \
	arch/drisc/Allocator.cpp \
	arch/drisc/Allocator.h \
	arch/drisc/AncillaryRegisterFile.cpp \
	arch/drisc/AncillaryRegisterFile.h \
	arch/drisc/DCache.cpp \
	arch/drisc/DCache.h \
	arch/drisc/DebugChannel.h \
	arch/drisc/DebugChannel.cpp \
	arch/drisc/DecodeStage.cpp \
	arch/drisc/DummyStage.cpp \
	arch/drisc/ExecuteStage.cpp \
	arch/drisc/FamilyTable.cpp \
	arch/drisc/FamilyTable.h \
	arch/drisc/FetchStage.cpp \
        arch/drisc/forward.h \
	arch/drisc/ICache.cpp \
	arch/drisc/ICache.h \
	arch/drisc/IOResponseMultiplexer.h \
	arch/drisc/IOResponseMultiplexer.cpp \
	arch/drisc/IONotificationMultiplexer.h \
	arch/drisc/IONotificationMultiplexer.cpp \
	arch/drisc/IOBusInterface.h \
	arch/drisc/IOBusInterface.cpp \
	arch/drisc/IOInterface.h \
	arch/drisc/IOInterface.cpp \
	arch/drisc/IOMatchUnit.h \
	arch/drisc/IOMatchUnit.cpp \
        arch/drisc/IODirectCacheAccess.h \
        arch/drisc/IODirectCacheAccess.cpp \
        arch/drisc/ISA.cpp \
	arch/drisc/ISA.mtalpha.h \
	arch/drisc/ISA.mtsparc.h \
	arch/drisc/ISA.mips.h \
	arch/drisc/MemoryStage.cpp \
	arch/drisc/MMUInterface.cpp \
	arch/drisc/MMUInterface.h \
	arch/drisc/Network.cpp \
	arch/drisc/Network.h \
	arch/drisc/Pipeline.cpp \
	arch/drisc/Pipeline.h \
	arch/drisc/DRISC.cpp \
	arch/drisc/DRISC.h \
	arch/drisc/RAUnit.cpp \
	arch/drisc/RAUnit.h \
	arch/drisc/ReadStage.cpp \
	arch/drisc/RegisterFile.cpp \
	arch/drisc/RegisterFile.h \
	arch/drisc/ThreadTable.cpp \
	arch/drisc/ThreadTable.h \
	arch/drisc/WritebackStage.cpp \
	arch/drisc/PerfCounters.h \
	arch/drisc/PerfCounters.cpp

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

if ENABLE_MEM_ZLCDMA
MEMORY_SRC += \
	arch/mem/zlcdma/CDMA.h \
	arch/mem/zlcdma/CDMA.cpp \
	arch/mem/zlcdma/Cache.h \
	arch/mem/zlcdma/Cache.cpp \
	arch/mem/zlcdma/Directory.h \
	arch/mem/zlcdma/Directory.cpp \
	arch/mem/zlcdma/Node.h \
	arch/mem/zlcdma/Node.cpp \
	arch/mem/zlcdma/RootDirectory.h \
	arch/mem/zlcdma/RootDirectory.cpp 
endif

if ENABLE_MEM_CDMA
MEMORY_SRC += \
	arch/mem/cdma/CDMA.h \
	arch/mem/cdma/CDMA.cpp \
	arch/mem/cdma/Cache.h \
	arch/mem/cdma/Cache.cpp \
	arch/mem/cdma/Directory.h \
	arch/mem/cdma/Directory.cpp \
	arch/mem/cdma/Node.h \
	arch/mem/cdma/Node.cpp \
	arch/mem/cdma/RootDirectory.h \
	arch/mem/cdma/RootDirectory.cpp 
endif

EXTRA_DIST += \
	arch/drisc/ISA.mips.cpp \
	arch/drisc/ISA.mtalpha.cpp \
	arch/drisc/ISA.mtsparc.cpp

ARCH_SOURCES = $(COMMON_SRC) $(PROCESSOR_SRC) $(MEMORY_SRC) $(DEVICE_SRC)

