#include "Processor.h"
#include <cassert>
using namespace Simulator;
using namespace std;

//
// Processor implementation
//
Processor::Processor(Object* parent, Kernel& kernel, PID pid, PSize numProcs, const std::string& name, IMemory& memory, const Config& config, MemAddr runAddress, bool legacy)
:   IComponent(parent, kernel, name, 0),
    m_pid(pid), m_kernel(kernel), m_memory(memory), m_numProcs(numProcs),
    m_localFamilyCompletion(0),
	m_allocator   (*this, "alloc",    m_familyTable, m_threadTable, m_registerFile, m_raunit, m_icache, m_network, m_pipeline, numProcs, config.allocator),
    m_icache      (*this, "icache",   m_allocator, config.icache),
    m_dcache      (*this, "dcache",   m_allocator, m_familyTable, m_registerFile, config.dcache),
	m_registerFile(*this,             m_allocator, config.registerFile),
    m_pipeline    (*this, "pipeline", m_registerFile, m_network, m_allocator, m_familyTable, m_threadTable, m_icache, m_dcache, m_fpu, config.pipeline),
	m_fpu         (*this, "fpu",      m_registerFile, config.fpu),
    m_raunit      (*this, "rau",      m_registerFile, config.raunit),
	m_familyTable (*this,             config.familyTable),
	m_threadTable (*this,             config.threadTable),
    m_network     (*this, "network",  m_allocator, m_registerFile, m_familyTable)
{
    //
    // Set port priorities and connections on all components
    //
	m_registerFile.p_asyncW.SetPriority(m_fpu,       0);
    m_registerFile.p_asyncW.SetPriority(m_dcache,    1);
    m_registerFile.p_asyncW.SetPriority(m_network,   2);
    m_registerFile.p_asyncW.SetPriority(m_allocator, 3);
    m_registerFile.p_asyncR.SetPriority(m_network,   0);
    m_registerFile.p_pipelineR1.SetComponent(m_pipeline.m_read);
    m_registerFile.p_pipelineR2.SetComponent(m_pipeline.m_read);
    m_registerFile.p_pipelineW.SetComponent(m_pipeline.m_writeback);
    
    // Set thread activation (cache-line query) order.
    // Primary concern is keeping the pipeline flowing. So order from back to front.
    m_icache.p_request.SetPriority(m_pipeline.m_writeback, 0);  // Writebacks
    m_icache.p_request.SetPriority(m_pipeline.m_execute,   1);  // Reschedules
    m_icache.p_request.SetPriority(m_fpu,                  2);  // FPU writeback
    m_icache.p_request.SetPriority(m_dcache,               3);  // Memory read writebacks
    m_icache.p_request.SetPriority(m_network,              4);  // Remote shareds writebacks
    m_icache.p_request.SetPriority(m_allocator,            5);  // Family completion, etc

    m_allocator.p_cleanup.SetPriority(m_pipeline.m_read, 0);

    m_memory.RegisterListener(*this);

    if (pid == 0)
    {
        // Allocate the startup family on the first processor
        m_allocator.AllocateInitialFamily(runAddress,legacy);
    }
}

void Processor::Initialize(Processor& prev, Processor& next)
{
    m_network.Initialize(prev.m_network, next.m_network);
}

Result Processor::ReadMemory(MemAddr address, void* data, MemSize size, MemTag tag)
{
	return m_memory.Read(*this, address, data, size, tag);
}

Result Processor::WriteMemory(MemAddr address, void* data, MemSize size, MemTag tag)
{
	return m_memory.Write(*this, address, data, size, tag);
}

bool Processor::CheckPermissions(MemAddr address, MemSize size, int access) const
{
	return m_memory.CheckPermissions(address, size, access);
}

bool Processor::OnMemoryReadCompleted(const MemData& data)
{
	// Dispatch result to I-Cache, D-Cache or Create Process, depending on tag
	assert(data.tag.cid != INVALID_CID);
	if (data.tag.data)
	{
		return m_dcache.OnMemoryReadCompleted(data);
	}
	return m_icache.OnMemoryReadCompleted(data);
}

bool Processor::OnMemoryWriteCompleted(const MemTag& tag)
{
	// Dispatch result to D-Cache
	assert(tag.fid != INVALID_LFID);
	return m_dcache.OnMemoryWriteCompleted(tag);
}

bool Processor::OnMemorySnooped(MemAddr addr, const MemData& data)
{
	return m_dcache.OnMemorySnooped(addr, data);
}

void Processor::OnFamilyTerminatedLocally(MemAddr pc)
{
    CycleNo cycle = GetKernel().GetCycleNo();
    m_localFamilyCompletion = cycle;
}
