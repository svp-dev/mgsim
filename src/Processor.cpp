#include "Processor.h"
#include "FPU.h"
#include <cassert>
using namespace Simulator;
using namespace std;

//
// Processor implementation
//
Processor::Processor(Object* parent, Kernel& kernel, GPID gpid, LPID lpid, const vector<Processor*>& grid, PSize gridSize, PSize placeSize, const std::string& name, IMemory& memory, FPU& fpu, const Config& config, MemAddr runAddress)
:   IComponent(parent, kernel, name, ""),
    m_pid(gpid), m_kernel(kernel), m_memory(memory), m_grid(grid), m_gridSize(gridSize), m_placeSize(placeSize), m_fpu(fpu),
    m_localFamilyCompletion(0),
	m_allocator   (*this, "alloc",    m_familyTable, m_threadTable, m_registerFile, m_raunit, m_icache, m_network, m_pipeline, lpid, config),
    m_icache      (*this, "icache",   m_allocator, config),
    m_dcache      (*this, "dcache",   m_allocator, m_familyTable, m_registerFile, config),
	m_registerFile(*this,             m_allocator, m_network, config),
    m_pipeline    (*this, "pipeline", lpid, m_registerFile, m_network, m_allocator, m_familyTable, m_threadTable, m_icache, m_dcache, fpu, config),
    m_raunit      (*this, "rau",      m_registerFile, config),
	m_familyTable (*this,             config),
	m_threadTable (*this,             config),
    m_network     (*this, "network",  grid, lpid, m_allocator, m_registerFile, m_familyTable)
{
    if (m_pid == 0)
    {
        // Allocate the startup family on the first processor
        m_allocator.AllocateInitialFamily(runAddress);
    }
}

void Processor::Initialize(Processor& prev, Processor& next)
{
    m_network.Initialize(prev.m_network, next.m_network);

    //
    // Set port priorities and connections on all components.
    // First source on a port has the highest priority.
    //

    for (int i = 0; i < FPU_NUM_OPS; ++i)
    {    
	    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_fpu, i));
	}

    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_dcache,    0)); // Mem Load writebacks
    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_network,   0)); // Register receives
    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_network,   1)); // Register sends (waiting writeback)
    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_allocator, 0)); // Thread allocation
    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_allocator, 4)); // Syncs
    m_registerFile.p_asyncR.AddSource(ArbitrationSource(&m_network,   1)); // Remote register sends
    
    m_registerFile.p_pipelineR1.SetSource(ArbitrationSource(&m_pipeline, 3)); // Pipeline read stage
    m_registerFile.p_pipelineR2.SetSource(ArbitrationSource(&m_pipeline, 3)); // Pipeline read stage
    
    m_registerFile.p_pipelineW .SetSource(ArbitrationSource(&m_pipeline, 0)); // Pipeline writeback stage
    
    for (size_t i = 0; i < m_grid.size(); i++)
    {
        // Every core can delegate to this core
        m_network.m_delegateRemote.AddSource(ArbitrationSource(&m_grid[i]->m_network, 4));

        // Every core can request registers
        m_network.m_registerRequestRemote .in.AddSource(ArbitrationSource(&m_grid[i]->m_network, 11));
        m_network.m_registerResponseRemote.in.AddSource(ArbitrationSource(&m_grid[i]->m_network, 12));
    }
    
    m_network.m_delegateLocal             .AddSource(ArbitrationSource(&m_allocator,      2)); // Create process sends delegated create
    m_network.m_createLocal               .AddSource(ArbitrationSource(&m_allocator,      2)); // Create process broadcasts create
    m_network.m_createRemote              .AddSource(ArbitrationSource(&prev.m_network,   5)); // Forward of group create
    m_network.m_registerRequestRemote.out .AddSource(ArbitrationSource(&m_pipeline,       2)); // Non-full register with remote mapping read
    m_network.m_registerRequestRemote.out .AddSource(ArbitrationSource(&m_network,        1)); // Forward of global request to remote parent
    m_network.m_registerRequestGroup.out  .AddSource(ArbitrationSource(&m_network,        1)); // Forward of global request to group place
    m_network.m_registerRequestGroup.out  .AddSource(ArbitrationSource(&m_pipeline,       2)); // Non-full register with remote mapping read
    m_network.m_registerRequestGroup.in   .AddSource(ArbitrationSource(&next.m_network,   3)); // From neighbour
    m_network.m_registerResponseGroup.in  .AddSource(ArbitrationSource(&prev.m_network,   2)); // From neighbour
    m_network.m_registerResponseGroup.out .AddSource(ArbitrationSource(&m_pipeline,       0)); // Pipeline write to register with remote mapping
    m_network.m_registerResponseGroup.out .AddSource(ArbitrationSource(&m_network,        1)); // Returning register from a request
    m_network.m_registerResponseGroup.out .AddSource(ArbitrationSource(&m_network,        0)); // Forwarding response from remote parent onto group
    for (int i = 0; i < FPU_NUM_OPS; ++i)
    {    
        m_network.m_registerResponseGroup.out.AddSource(ArbitrationSource(&m_fpu, i)); // FP operation to a shared
    }
    m_network.m_registerResponseRemote.out.AddSource(ArbitrationSource(&m_network,        1)); // Returning register from a request
    m_network.m_registerResponseRemote.out.AddSource(ArbitrationSource(&m_pipeline,       0)); // Writing a remote shared from pipeline
    m_network.m_completedThread           .AddSource(ArbitrationSource(&next.m_pipeline,  0)); // Thread terminated (reschedule at WB stage)
    m_network.m_cleanedUpThread           .AddSource(ArbitrationSource(&prev.m_allocator, 0)); // Thread cleaned up
    m_network.m_synchronizedFamily        .AddSource(ArbitrationSource(&prev.m_network,   9)); // Forwarding
    m_network.m_synchronizedFamily        .AddSource(ArbitrationSource(&m_allocator,      0)); // Dependencies resolved
    m_network.m_terminatedFamily          .AddSource(ArbitrationSource(&next.m_network,  10)); // Forwarding
    m_network.m_terminatedFamily          .AddSource(ArbitrationSource(&m_network,        5)); // Create with no threads
    m_network.m_terminatedFamily          .AddSource(ArbitrationSource(&m_allocator,      0)); // Last thread cleaned up
    m_network.m_remoteSync                .AddSource(ArbitrationSource(&prev.m_network,   9)); // Sync token caused sync
    m_network.m_remoteSync                .AddSource(ArbitrationSource(&m_allocator,      0)); // Thread administration caused sync
    
    m_memory.RegisterListener(*this);
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
