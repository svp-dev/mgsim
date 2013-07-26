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
        arch/proc/ActionInterface.cpp \
        arch/proc/ActionInterface.h \
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
        arch/proc/ISA.cpp \
	arch/proc/ISA.mtalpha.h \
	arch/proc/ISA.mtsparc.h \
	arch/proc/ISA.mips.h \
	arch/proc/MemoryStage.cpp \
	arch/proc/MMUInterface.cpp \
	arch/proc/MMUInterface.h \
	arch/proc/Network.cpp \
	arch/proc/Network.h \
	arch/proc/Pipeline.cpp \
	arch/proc/Pipeline.h \
	arch/proc/DRISC.cpp \
	arch/proc/DRISC.h \
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
	arch/proc/ISA.mips.cpp \
	arch/proc/ISA.mtalpha.cpp \
	arch/proc/ISA.mtsparc.cpp

ARCH_SOURCES = $(COMMON_SRC) $(PROCESSOR_SRC) $(MEMORY_SRC) $(DEVICE_SRC)

