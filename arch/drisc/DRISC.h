// -*- c++ -*-
#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <sim/inspect.h>
#include <sim/unreachable.h>
#include <arch/IOMessageInterface.h>
#include <arch/Memory.h>
#include <arch/BankSelector.h>
#include <arch/FPU.h>
#include <arch/drisc/RAUnit.h>
#include <arch/drisc/IOMatchUnit.h>
#include <arch/drisc/DebugChannel.h>
#include <arch/drisc/ActionInterface.h>
#include <arch/drisc/AncillaryRegisterFile.h>
#include <arch/drisc/PerfCounters.h>
#include <arch/drisc/MMUInterface.h>
#include <arch/drisc/RegisterFile.h>
#include <arch/drisc/FamilyTable.h>
#include <arch/drisc/ThreadTable.h>
#include <arch/drisc/ICache.h>
#include <arch/drisc/DCache.h>
#include <arch/drisc/IOInterface.h>
#include <arch/drisc/Network.h>
#include <arch/drisc/Allocator.h>
#include <arch/drisc/Pipeline.h>

class Config;

namespace Simulator
{

    class BreakPointManager;

#define GetDRISC() (static_cast<DRISC&>(GetDRISCParent()))

class DRISC : public Object
{
public:
    class Allocator;

    DRISC(const std::string& name, Object& parent, Clock& clock, PID pid, const std::vector<DRISC*>& grid, BreakPointManager& bp);
    DRISC(const DRISC&) = delete;
    DRISC& operator=(const DRISC&) = delete;
    ~DRISC();

public:
    void ConnectMemory(IMemory* memory, IMemoryAdmin *admin);
    void ConnectLink(DRISC* prev, DRISC* next);
    void ConnectFPU(FPU* fpu);
    void ConnectIO(IOMessageInterface* ioif);

    void Initialize();

    bool Boot(MemAddr addr, bool legacy);

private:
    // Helper to Initialize()
    void InitializeRegisters();
public:
    CycleNo GetCycleNo() const { return m_clock.GetCycleNo(); }
    PID   GetPID()      const { return m_pid; }
    PSize GetGridSize() const { return m_grid.size(); }
    bool  IsIdle()      const;

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

    void WriteASR(drisc::ARAddr which, Integer data) {  m_asr_file.WriteRegister(which, data); }
    Integer ReadASR(drisc::ARAddr which) const { return m_asr_file.ReadRegister(which); }
    void WriteAPR(drisc::ARAddr which, Integer data) {  m_apr_file.WriteRegister(which, data); }
    Integer ReadAPR(drisc::ARAddr which) const { return m_apr_file.ReadRegister(which); }



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

    BreakPointManager& GetBreakPointManager() { return m_bp_manager; }
    drisc::Network& GetNetwork() { return m_network; }
    drisc::IOInterface* GetIOInterface() { return m_io_if; }
    drisc::RegisterFile& GetRegisterFile() { return m_registerFile; }
    drisc::ICache& GetICache() { return m_icache; }
    drisc::DCache& GetDCache() { return m_dcache; }
    drisc::Allocator& GetAllocator() { return m_allocator; }
    drisc::RAUnit& GetRAUnit() { return m_raunit; }
    drisc::Pipeline& GetPipeline() { return m_pipeline; }
    drisc::IOMatchUnit& GetIOMatchUnit() { return m_mmio; }
    drisc::FamilyTable& GetFamilyTable() { return m_familyTable; }
    drisc::ThreadTable& GetThreadTable() { return m_threadTable; }
    SymbolTable& GetSymbolTable() { return *m_symtable; }

private:
    Clock&                         m_clock;
    BreakPointManager&             m_bp_manager;
    IMemory*                       m_memory;
    IMemoryAdmin*                  m_memadmin;
    const std::vector<DRISC*>&     m_grid;
    FPU*                           m_fpu;
    SymbolTable*                   m_symtable;
    PID                            m_pid;
    // Register initializers
    std::map<RegAddr, std::string> m_reginits;

    // Bit counts for packing and unpacking configuration-dependent values
    struct
    {
        unsigned int pid_bits;  ///< Number of bits for a PID (DRISC ID)
        unsigned int fid_bits;  ///< Number of bits for a LFID (Local Family ID)
        unsigned int tid_bits;  ///< Number of bits for a TID (Thread ID)
    } m_bits;

    // The components on the core
    drisc::FamilyTable    m_familyTable;
    drisc::ThreadTable    m_threadTable;
    drisc::RegisterFile   m_registerFile;
    drisc::RAUnit         m_raunit;
    drisc::Allocator      m_allocator;
    drisc::ICache         m_icache;
    drisc::DCache         m_dcache;
    drisc::Pipeline       m_pipeline;
    drisc::Network        m_network;

    // Local MMIO devices
    drisc::IOMatchUnit    m_mmio;
    drisc::AncillaryRegisterFile m_apr_file;
    drisc::AncillaryRegisterFile m_asr_file;
    drisc::PerfCounters   m_perfcounters;
    drisc::DebugChannel   m_lpout;
    drisc::DebugChannel   m_lperr;
    drisc::MMUInterface   m_mmu;
    drisc::ActionInterface m_action;

    // External I/O interface, optional
    drisc::IOInterface    *m_io_if;

    friend class drisc::PerfCounters::Helpers;
};

}
#endif
