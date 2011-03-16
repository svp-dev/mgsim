#ifndef PROCESSOR_H
#define PROCESSOR_H

#include "kernel.h"
#include "Allocator.h"
#include "ICache.h"
#include "DCache.h"
#include "RegisterFile.h"
#include "Pipeline.h"
#include "Network.h"
#include "FamilyTable.h"
#include "ThreadTable.h"
#include "RAUnit.h"

class Config;

namespace Simulator
{

struct PlaceInfo : public Object
{
    PSize        m_size;            ///< Number of processors in the place
    CombinedFlag m_full_context;    ///< Place-global signal for free context identification
    MultiFlag    m_reserve_context; ///< Place-global flag for reserving a context
    CombinedFlag m_want_token;      ///< Place-global signal for token request

    PlaceInfo(const std::string& name, Clock& clock, Object& parent, unsigned int size)
        : Object(name, parent, clock),
        m_size(size),
        m_full_context("f_fullContext", *this, clock, size),
        m_reserve_context("f_reserveContext", *this, clock, false),
        m_want_token("f_wantToken", *this, clock, size)
    {
    }
};

class FPU;

class Processor : public Object, public IMemoryCallback
{
public:
    Processor(const std::string& name, Object& parent, Clock& clock, GPID pid, LPID lpid, const std::vector<Processor*>& grid, PSize gridSize, PlaceInfo& place, IMemory& m_memory, FPU& fpu, const Config& config);
    ~Processor();
    
    void Initialize(Processor& prev, Processor& next, MemAddr runAddress, bool legacy);

    GPID    GetPID()       const { return m_pid; }
    PSize   GetPlaceSize() const { return m_place.m_size; }
    PSize   GetGridSize()  const { return m_gridSize; }
    bool    IsIdle()       const;


    Pipeline& GetPipeline() { return m_pipeline; }
    
    float GetRegFileAsyncPortActivity() const {
        return (float)m_registerFile.p_asyncW.GetBusyCycles() / (float)GetCycleNo();
    }
	
    TSize GetMaxThreadsAllocated() const { return m_threadTable.GetMaxAllocated(); }
    TSize GetTotalThreadsAllocated() { return m_threadTable.GetTotalAllocated(); }
    TSize GetThreadTableSize() const { return m_threadTable.GetNumThreads(); }
    float GetThreadTableOccupancy() { return (float)GetTotalThreadsAllocated() / (float)GetThreadTableSize() / (float)GetKernel()->GetCycleNo(); }
    FSize GetMaxFamiliesAllocated() const { return m_familyTable.GetMaxAllocated(); }
    FSize GetTotalFamiliesAllocated() { return m_familyTable.GetTotalAllocated(); }
    FSize GetFamilyTableSize() const { return m_familyTable.GetNumFamilies(); }
    float GetFamilyTableOccupancy() { return (float)GetTotalFamiliesAllocated() / (float)GetFamilyTableSize() / (float)GetKernel()->GetCycleNo(); }
    BufferSize GetMaxAllocateExQueueSize() { return m_allocator.GetMaxAllocatedEx(); }
    BufferSize GetTotalAllocateExQueueSize() { return m_allocator.GetTotalAllocatedEx(); }
    float GetAverageAllocateExQueueSize() { return (float)GetTotalAllocateExQueueSize() / (float)GetKernel()->GetCycleNo(); }

    unsigned int GetNumSuspendedRegisters() const;
    
    Integer GetProfileWord(unsigned int i) const;
	
    void WriteRegister(const RegAddr& addr, const RegValue& value) {
        m_registerFile.WriteRegister(addr, value);
    }
	
    // Configuration-dependent helpers
    MemAddr     GetTLSAddress(LFID fid, TID tid) const;
    MemSize     GetTLSSize() const;
    PlaceID     UnpackPlace(Integer id) const;
    FID         UnpackFID(Integer id) const;
    Integer     PackFID(const FID& fid) const;
    FCapability GenerateFamilyCapability() const;

    // All memory requests from caches go through the processor.
    // No memory callback specified, the processor will use the tag to determine where it came from.
    void MapMemory(MemAddr address, MemSize size);
    void UnmapMemory(MemAddr address, MemSize size);
    bool ReadMemory (MemAddr address, MemSize size);
    bool WriteMemory(MemAddr address, const void* data, MemSize size, TID tid);
    bool CheckPermissions(MemAddr address, MemSize size, int access) const;
	
    Network& GetNetwork() { return m_network; }

private:
    GPID                           m_pid;
    IMemory&	                   m_memory;
    const std::vector<Processor*>& m_grid;
    PSize                          m_gridSize;
    PlaceInfo&                     m_place;
    FPU&                           m_fpu;
    
    // Bit counts for packing and unpacking configuration-dependent values
    struct
    {
        unsigned int pid_bits;  ///< Number of bits for a PID (Processor ID)
        unsigned int fid_bits;  ///< Number of bits for a LFID (Local Family ID)
        unsigned int tid_bits;  ///< Number of bits for a TID (Thread ID)
    } m_bits;
    
    // IMemoryCallback
    bool OnMemoryReadCompleted(MemAddr addr, const MemData& data);
    bool OnMemoryWriteCompleted(TID tid);
    bool OnMemoryInvalidated(MemAddr addr);
    bool OnMemorySnooped(MemAddr addr, const MemData& data);

    // The components on the chip
    Allocator       m_allocator;
    ICache          m_icache;
    DCache          m_dcache;
    RegisterFile    m_registerFile;
    Pipeline        m_pipeline;
    RAUnit          m_raunit;
    FamilyTable     m_familyTable;
    ThreadTable     m_threadTable;
    Network         m_network;
};

}
#endif

