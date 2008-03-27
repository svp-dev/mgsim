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

    Processor(Object* parent, Kernel& kernel, PID pid, PSize numProcs, const std::string& name, IMemory& m_memory, const Config& config, MemAddr runAddress);
    void initialize(Processor& prev, Processor& next);

    PID     getPID()      const { return m_pid;      }
    PSize   getNumProcs() const { return m_numProcs; }
    Kernel& getKernel()   const { return m_kernel;   }
    bool    idle()        const;

    uint64_t getFlop() const { return m_pipeline.getFlop(); }
    uint64_t getOp()   const { return m_pipeline.getOp(); }
	float getRegFileAsyncPortActivity() const {
		return (float)m_registerFile.p_asyncW.getBusyCycles() / m_kernel.getCycleNo();
	}
	
	void writeRegister(const RegAddr& addr, const RegValue& value) {
		m_registerFile.writeRegister(addr, value);
	}

	// All memory requests from caches go through the processor.
	// No memory callback specified, the processor will use the tag to determine where it came from.
	Result readMemory (MemAddr address, void* data, MemSize size, MemTag tag);
	Result writeMemory(MemAddr address, void* data, MemSize size, MemTag tag);

private:
    PID         m_pid;
    Kernel&     m_kernel;
	IMemory&	m_memory;
	PSize       m_numProcs;

    // IMemoryCallback
    bool onMemoryReadCompleted(const MemData& data);
    bool onMemoryWriteCompleted(const MemTag& tag);
    bool onMemorySnooped(MemAddr addr, const MemData& data);

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

