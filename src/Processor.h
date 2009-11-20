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
    Flag         m_reserve_context; ///< Place-global flag for reserving a context
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

class Processor : public IComponent, public IMemoryCallback
{
public:
    Processor(Object* parent, Kernel& kernel, GPID pid, LPID lpid, const std::vector<Processor*>& grid, PSize gridSize, PlaceInfo& place, const std::string& name, IMemory& m_memory, Display& display, FPU& fpu, const Config& config);
    void Initialize(Processor& prev, Processor& next, MemAddr runAddress, bool legacy);

    GPID    GetPID()       const { return m_pid; }
    PSize   GetPlaceSize() const { return m_place.m_size; }
    PSize   GetGridSize()  const { return m_gridSize; }
    Kernel& GetKernel()    const { return m_kernel; }
    bool    IsIdle()       const;

    uint64_t GetFlop() const { return m_pipeline.GetFlop(); }
    uint64_t GetOp()   const { return m_pipeline.GetOp(); }
	float GetRegFileAsyncPortActivity() const {
		return (float)m_registerFile.p_asyncW.GetBusyCycles() / (float)m_kernel.GetCycleNo();
	}
	
	uint64_t GetTotalActiveQueueSize() const { return m_allocator.GetTotalActiveQueueSize(); }
	uint64_t GetMaxActiveQueueSize()   const { return m_allocator.GetMaxActiveQueueSize(); }
	uint64_t GetMinActiveQueueSize()   const { return m_allocator.GetMinActiveQueueSize(); }
	
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

	// All memory requests from caches go through the processor.
	// No memory callback specified, the processor will use the tag to determine where it came from.
	void ReserveTLS(MemAddr address, MemSize size);
	void UnreserveTLS(MemAddr address);
	bool ReadMemory (MemAddr address, MemSize size, MemTag tag);
	bool WriteMemory(MemAddr address, const void* data, MemSize size, MemTag tag);
	bool CheckPermissions(MemAddr address, MemSize size, int access) const;
	
	Network& GetNetwork() { return m_network; }

private:
    GPID                           m_pid;
    Kernel&                        m_kernel;
	IMemory&	                   m_memory;
	const std::vector<Processor*>& m_grid;
	PSize                          m_gridSize;
	PlaceInfo&                     m_place;
	FPU&                           m_fpu;
	
	// Statistics 
    CycleNo m_localFamilyCompletion; 

    // IMemoryCallback
    bool OnMemoryReadCompleted(const MemData& data);
    bool OnMemoryWriteCompleted(const MemTag& tag);
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

