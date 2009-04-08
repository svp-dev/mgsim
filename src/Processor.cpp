#include "Processor.h"
#include <cassert>
using namespace Simulator;
using namespace std;

//
// Processor implementation
//
Processor::Processor(Object* parent, Kernel& kernel, GPID gpid, LPID lpid, const vector<Processor*>& grid, PSize gridSize, PSize placeSize, const std::string& name, IMemory& memory, const Config& config, MemAddr runAddress)
:   IComponent(parent, kernel, name, ""),
    m_pid(gpid), m_kernel(kernel), m_memory(memory), m_grid(grid), m_gridSize(gridSize), m_placeSize(placeSize),
    m_localFamilyCompletion(0),
	m_allocator   (*this, "alloc",    m_familyTable, m_threadTable, m_registerFile, m_raunit, m_icache, m_network, m_pipeline, lpid, config.allocator),
    m_icache      (*this, "icache",   m_allocator, config.icache),
    m_dcache      (*this, "dcache",   m_allocator, m_familyTable, m_registerFile, config.dcache),
	m_registerFile(*this,             m_allocator, m_network, config.registerFile),
    m_pipeline    (*this, "pipeline", lpid, m_registerFile, m_network, m_allocator, m_familyTable, m_threadTable, m_icache, m_dcache, m_fpu, config.pipeline),
	m_fpu         (*this, "fpu",      m_registerFile, config.fpu),
    m_raunit      (*this, "rau",      m_registerFile, config.raunit),
	m_familyTable (*this,             config.familyTable),
	m_threadTable (*this,             config.threadTable),
    m_network     (*this, "network",  grid, lpid, m_allocator, m_registerFile, m_familyTable)
{
    //
    // Set port priorities and connections on all components
    //
	m_registerFile.p_asyncW.SetPriority(ArbitrationSource(&m_fpu,       0), 0);
    m_registerFile.p_asyncW.SetPriority(ArbitrationSource(&m_dcache,    0), 1);
    m_registerFile.p_asyncW.SetPriority(ArbitrationSource(&m_network,   0), 2);
    m_registerFile.p_asyncW.SetPriority(ArbitrationSource(&m_network,   1), 3);
    m_registerFile.p_asyncW.SetPriority(ArbitrationSource(&m_allocator, 0), 4);
    m_registerFile.p_asyncW.SetPriority(ArbitrationSource(&m_allocator, 4), 5);
    m_registerFile.p_asyncR.SetPriority(ArbitrationSource(&m_network,   1), 0);
    m_registerFile.p_pipelineR1.SetSource(ArbitrationSource(&m_pipeline, 3));
    m_registerFile.p_pipelineR2.SetSource(ArbitrationSource(&m_pipeline, 3));
    m_registerFile.p_pipelineW .SetSource(ArbitrationSource(&m_pipeline, 0));
    
    m_network.p_registerResponseOut.SetPriority(ArbitrationSource(&m_pipeline, 0), 0);
    m_network.p_registerResponseOut.SetPriority(ArbitrationSource(&m_network,  0), 1);
    m_network.p_registerResponseOut.SetPriority(ArbitrationSource(&m_fpu,      0), 2);
    m_network.p_registerResponseOut.SetPriority(ArbitrationSource(&m_dcache,   0), 3);
    // The Allocator shouldn't be able to trigger a
    // remote register write, so we don't give them access.
        
    m_memory.RegisterListener(*this);
    
    if (gpid == 0)
    {
        // Allocate the startup family on the first processor
        m_allocator.AllocateInitialFamily(runAddress);
    }
}

void Processor::Initialize(Processor& prev, Processor& next)
{
    m_network.Initialize(prev.m_network, next.m_network);
}

bool Processor::IsIdle() const
{
    return m_threadTable.IsEmpty() && m_familyTable.IsEmpty() && m_icache.IsEmpty();
}

void Processor::ReserveTLS(MemAddr address, MemSize size)
{
    return m_memory.Reserve(address, size, IMemory::PERM_READ | IMemory::PERM_WRITE);
}

void Processor::UnreserveTLS(MemAddr address)
{
    return m_memory.Unreserve(address);
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

void Processor::OnFamilyTerminatedLocally(MemAddr /* pc */)
{
    CycleNo cycle = GetKernel().GetCycleNo();
    m_localFamilyCompletion = cycle;
}
