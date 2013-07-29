#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <sim/inspect.h>
#include <sim/unreachable.h>
#include <arch/IOBus.h>
#include <arch/Memory.h>
#include <arch/BankSelector.h>

class Config;

namespace Simulator
{

namespace counters {};
class FPU;

const std::vector<std::string>& GetDefaultLocalRegisterAliases(RegType type);

class DRISC : public Object
{
public:
    class Allocator;

#include "FamilyTable.h"
#include "ThreadTable.h"
#include "RegisterFile.h"
#include "Network.h"
#include "ICache.h"
#include "IOMatchUnit.h"
#include "IOInterface.h"
#include "DebugChannel.h"
#include "DCache.h"
#include "Pipeline.h"
#include "RAUnit.h"
#include "Allocator.h"
#include "PerfCounters.h"
#include "MMUInterface.h"
#include "ActionInterface.h"
#include "AncillaryRegisterFile.h"

    DRISC(const std::string& name, Object& parent, Clock& clock, PID pid, const std::vector<DRISC*>& grid, IMemory& memory, IMemoryAdmin& admin, Config& config);
    DRISC(const DRISC&) = delete;
    DRISC& operator=(const DRISC&) = delete;
    ~DRISC();

    void ConnectLink(DRISC* prev, DRISC* next);
    void ConnectFPU(Config& config, FPU* fpu);
    void ConnectIO(Config& config, IIOBus* iobus);

    void Initialize();

    void Boot(MemAddr runAddress, bool legacy, PSize placeSize, SInteger startIndex);

    PID   GetPID()      const { return m_pid; }
    PSize GetGridSize() const { return m_grid.size(); }
    bool  IsIdle()      const;


    Pipeline& GetPipeline() { return m_pipeline; }
    IOMatchUnit& GetIOMatchUnit() { return m_mmio; }
    MemAddr GetDeviceBaseAddress(IODeviceID dev) const;

    float GetRegFileAsyncPortActivity() const {
        return (float)m_registerFile.p_asyncW.GetBusyCycles() / (float)GetCycleNo();
    }

    TSize GetMaxThreadsAllocated() const { return m_threadTable.GetMaxAllocated(); }
    TSize GetTotalThreadsAllocated() { return m_threadTable.GetTotalAllocated(); }
    TSize GetTotalThreadsCreated() { return m_allocator.GetTotalThreadsCreated(); }
    TSize GetThreadTableSize() const { return m_threadTable.GetNumThreads(); }
    float GetThreadTableOccupancy() { return (float)GetTotalThreadsAllocated() / (float)GetThreadTableSize() / (float)GetKernel()->GetCycleNo(); }
    FSize GetMaxFamiliesAllocated() const { return m_familyTable.GetMaxAllocated(); }
    FSize GetTotalFamiliesAllocated() { return m_familyTable.GetTotalAllocated(); }
    FSize GetTotalFamiliesCreated() { return m_allocator.GetTotalFamiliesCreated(); }
    FSize GetFamilyTableSize() const { return m_familyTable.GetNumFamilies(); }
    float GetFamilyTableOccupancy() { return (float)GetTotalFamiliesAllocated() / (float)GetFamilyTableSize() / (float)GetKernel()->GetCycleNo(); }
    BufferSize GetMaxAllocateExQueueSize() { return m_allocator.GetMaxAllocatedEx(); }
    BufferSize GetTotalAllocateExQueueSize() { return m_allocator.GetTotalAllocatedEx(); }
    float GetAverageAllocateExQueueSize() { return (float)GetTotalAllocateExQueueSize() / (float)GetKernel()->GetCycleNo(); }

    unsigned int GetNumSuspendedRegisters() const;

    void WriteRegister(const RegAddr& addr, const RegValue& value) { m_registerFile.WriteRegister(addr, value); }
    void WriteASR(ARAddr which, Integer data) {  m_asr_file.WriteRegister(which, data); }
    Integer ReadASR(ARAddr which) const { return m_asr_file.ReadRegister(which); }
    void WriteAPR(ARAddr which, Integer data) {  m_apr_file.WriteRegister(which, data); }
    Integer ReadAPR(ARAddr which) const { return m_apr_file.ReadRegister(which); }



    // Configuration-dependent helpers
    PSize       GetPlaceSize(LFID fid) const { return m_familyTable[fid].placeSize; }
    MemAddr     GetTLSAddress(LFID fid, TID tid) const;
    MemSize     GetTLSSize() const;
    PlaceID     UnpackPlace(Integer id) const;
    Integer     PackPlace(const PlaceID& id) const;
    FID         UnpackFID(Integer id) const;
    Integer     PackFID(const FID& fid) const;
    FCapability GenerateFamilyCapability() const;

    void MapMemory(MemAddr address, MemSize size, ProcessID pid = 0);
    void UnmapMemory(MemAddr address, MemSize size);
    void UnmapMemory(ProcessID pid);
    bool CheckPermissions(MemAddr address, MemSize size, int access) const;

    Network& GetNetwork() { return m_network; }
    IOInterface* GetIOInterface() { return m_io_if; }
    RegisterFile& GetRegisterFile() { return m_registerFile; }
    ICache& GetICache() { return m_icache; }
    DCache& GetDCache() { return m_dcache; }
    SymbolTable& GetSymbolTable() { return *m_symtable; }
    Allocator& GetAllocator() { return m_allocator; }

private:
    IMemory&                       m_memory;
    IMemoryAdmin&                  m_memadmin;
    const std::vector<DRISC*>& m_grid;
    FPU*                           m_fpu;
    SymbolTable*                   m_symtable;
    PID                            m_pid;

    // Bit counts for packing and unpacking configuration-dependent values
    struct
    {
        unsigned int pid_bits;  ///< Number of bits for a PID (DRISC ID)
        unsigned int fid_bits;  ///< Number of bits for a LFID (Local Family ID)
        unsigned int tid_bits;  ///< Number of bits for a TID (Thread ID)
    } m_bits;

    // The components on the core
    FamilyTable           m_familyTable;
    ThreadTable           m_threadTable;
    RegisterFile          m_registerFile;
    RAUnit                m_raunit;
    Allocator             m_allocator;
    ICache                m_icache;
    DCache                m_dcache;
    Pipeline              m_pipeline;
    Network               m_network;

    // Local MMIO devices
    IOMatchUnit           m_mmio;
    AncillaryRegisterFile m_apr_file;
    AncillaryRegisterFile m_asr_file;
    PerfCounters          m_perfcounters;
    DebugChannel          m_lpout;
    DebugChannel          m_lperr;
    MMUInterface          m_mmu;
    ActionInterface       m_action;

    // External I/O interface, optional
    IOInterface           *m_io_if;

    friend class PerfCounters::Helpers;
};

}
#endif

