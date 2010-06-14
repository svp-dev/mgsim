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

struct PlaceInfo
{
    PSize        m_size;            ///< Number of processors in the place
    CombinedFlag m_full_context;    ///< Place-global signal for free context identification
    MultiFlag    m_reserve_context; ///< Place-global flag for reserving a context
    CombinedFlag m_want_token;      ///< Place-global signal for token request

    PlaceInfo(Kernel& kernel, unsigned int size)
        : m_size(size),
          m_full_context(kernel, size),
          m_reserve_context(kernel, false),
          m_want_token(kernel, size)
    {
    }
};

class FPU;

class Processor : public Object, public IMemoryCallback
{
public:
    Processor(const std::string& name, Object& parent, GPID pid, LPID lpid, const std::vector<Processor*>& grid, PSize gridSize, PlaceInfo& place, IMemory& m_memory, FPU& fpu, const Config& config);
    ~Processor();
    
    void Initialize(Processor& prev, Processor& next, MemAddr runAddress, bool legacy);

    GPID    GetPID()       const { return m_pid; }
    PSize   GetPlaceSize() const { return m_place.m_size; }
    PSize   GetGridSize()  const { return m_gridSize; }
    bool    IsIdle()       const;

    uint64_t GetFlop()     const { return m_pipeline.GetFlop(); }
    uint64_t GetOp()       const { return m_pipeline.GetOp(); }
    void     CollectMemOpStatistics(uint64_t& nr, uint64_t& nw, uint64_t& nrb, uint64_t& nwb) const
    { return m_pipeline.CollectMemOpStatistics(nr, nw, nrb, nwb); }

    float GetRegFileAsyncPortActivity() const {
        return (float)m_registerFile.p_asyncW.GetBusyCycles() / (float)GetKernel()->GetCycleNo();
    }
	
    uint64_t GetMinPipelineIdleTime() const { return m_pipeline.GetMinIdleTime(); }
    uint64_t GetMaxPipelineIdleTime() const { return m_pipeline.GetMaxIdleTime(); }
    uint64_t GetAvgPipelineIdleTime() const { return m_pipeline.GetAvgIdleTime(); }
	
    float GetPipelineEfficiency() const { return m_pipeline.GetEfficiency(); }
	
    CycleNo GetLocalFamilyCompletion() const { return m_localFamilyCompletion; }

    unsigned int GetNumSuspendedRegisters() const;
    
    Integer GetProfileWord(unsigned int i) const;
	
    void WriteRegister(const RegAddr& addr, const RegValue& value) {
        m_registerFile.WriteRegister(addr, value);
    }
	
    void OnFamilyTerminatedLocally(MemAddr pc);

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
    
	
    // Statistics 
    CycleNo m_localFamilyCompletion; 

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

