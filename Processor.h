#ifndef PROCESSOR_H
#define PROCESSOR_H

#include "kernel.h"
#include "Allocator.h"
#include "ICache.h"
#include "DCache.h"
#include "RegisterFile.h"
#include "Pipeline.h"
#include "FPU.h"
#include "Network.h"
#include "FamilyTable.h"
#include "ThreadTable.h"
#include "RAUnit.h"

namespace Simulator
{

class Processor : public IComponent, public IMemoryCallback
{
public:
    struct Config
    {
		ICache::Config		 icache;
		DCache::Config		 dcache;
		ThreadTable::Config  threadTable;
		FamilyTable::Config  familyTable;
		RegisterFile::Config registerFile;
		Allocator::Config    allocator;
		Pipeline::Config     pipeline;
		RAUnit::Config		 raunit;
		FPU::Config          fpu;
    };

    Processor(Object* parent, Kernel& kernel, PID pid, PSize numProcs, const std::string& name, IMemory& m_memory, const Config& config, MemAddr runAddress, bool legacy);
    void Initialize(Processor& prev, Processor& next);

    PID     GetPID()      const { return m_pid;      }
    PSize   GetNumProcs() const { return m_numProcs; }
    Kernel& GetKernel()   const { return m_kernel;   }

    uint64_t GetFlop() const { return m_pipeline.GetFlop(); }
    uint64_t GetOp()   const { return m_pipeline.GetOp(); }
	float GetRegFileAsyncPortActivity() const {
		return (float)m_registerFile.p_asyncW.GetBusyCycles() / m_kernel.GetCycleNo();
	}
	
	uint64_t GetTotalActiveQueueSize() const { return m_allocator.GetTotalActiveQueueSize(); }
	uint64_t GetMaxActiveQueueSize()   const { return m_allocator.GetMaxActiveQueueSize(); }
	uint64_t GetMinActiveQueueSize()   const { return m_allocator.GetMinActiveQueueSize(); }
	
	uint64_t GetMinPipelineIdleTime() const { return m_pipeline.GetMinIdleTime(); }
	uint64_t GetMaxPipelineIdleTime() const { return m_pipeline.GetMaxIdleTime(); }
	uint64_t GetAvgPipelineIdleTime() const { return m_pipeline.GetAvgIdleTime(); }
	
	float GetPipelineEfficiency() const { return m_pipeline.GetEfficiency(); }
	
	CycleNo GetLocalFamilyCompletion() const { return m_localFamilyCompletion; }
	
	void WriteRegister(const RegAddr& addr, const RegValue& value) {
		m_registerFile.WriteRegister(addr, value);
	}
	
	void OnFamilyTerminatedLocally(MemAddr pc);

	// All memory requests from caches go through the processor.
	// No memory callback specified, the processor will use the tag to determine where it came from.
	void   ReserveTLS(MemAddr address, MemSize size);
	void   UnreserveTLS(MemAddr address);
	Result ReadMemory (MemAddr address, void* data, MemSize size, MemTag tag);
	Result WriteMemory(MemAddr address, void* data, MemSize size, MemTag tag);
	bool   CheckPermissions(MemAddr address, MemSize size, int access) const;

private:
    PID         m_pid;
    Kernel&     m_kernel;
	IMemory&	m_memory;
	PSize       m_numProcs;
	
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
	FPU				m_fpu;
    RAUnit          m_raunit;
    FamilyTable     m_familyTable;
    ThreadTable     m_threadTable;
    Network         m_network;
};

}
#endif

