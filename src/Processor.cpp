#include "Processor.h"
#include "FPU.h"
#include <cassert>
using namespace std;

namespace Simulator
{

//
// Processor implementation
//
Processor::Processor(Object* parent, Kernel& kernel, GPID gpid, LPID lpid, const vector<Processor*>& grid, PSize gridSize, PlaceInfo& place, const std::string& name, IMemory& memory, FPU& fpu, const Config& config)
:   IComponent(parent, kernel, name, ""),
    m_pid(gpid), m_kernel(kernel), m_memory(memory), m_grid(grid), m_gridSize(gridSize), m_place(place), m_fpu(fpu),
    m_localFamilyCompletion(0),
	m_allocator   (*this, "alloc",    m_familyTable, m_threadTable, m_registerFile, m_raunit, m_icache, m_network, m_pipeline, place, lpid, config),
    m_icache      (*this, "icache",   m_allocator, config),
    m_dcache      (*this, "dcache",   m_allocator, m_familyTable, m_registerFile, config),
	m_registerFile(*this,             m_allocator, m_network, config),
    m_pipeline    (*this, "pipeline", lpid, m_registerFile, m_network, m_allocator, m_familyTable, m_threadTable, m_icache, m_dcache, fpu, config),
    m_raunit      (*this, "rau",      m_registerFile, config),
	m_familyTable (*this,             config),
	m_threadTable (*this,             config),
    m_network     (*this, "network",  place, grid, lpid, m_allocator, m_registerFile, m_familyTable)
{
    const ArbitrationSource sources[] = {
        ArbitrationSource(&m_icache, 0),    // Outgoing process in I-Cache
        ArbitrationSource(&m_dcache, 2),    // Outgoing process in D-Cache
        ArbitrationSource(NULL, -1)
    };
    
    m_memory.RegisterListener(m_pid, *this, sources);
}

void Processor::Initialize(Processor& prev, Processor& next, MemAddr runAddress, bool legacy)
{
    m_network.Initialize(prev.m_network, next.m_network);

    //
    // Set port priorities and connections on all components.
    // First source on a port has the highest priority.
    //
    
    m_icache.p_service.AddSource(ArbitrationSource(&m_icache,    1)); // Cache-line returns
    m_icache.p_service.AddSource(ArbitrationSource(&m_allocator, 3)); // Thread activation
    m_icache.p_service.AddSource(ArbitrationSource(&m_allocator, 2)); // Create process

    // Unfortunately the D-Cache needs priority here because otherwise all cache-lines can
    // remain filled and we get deadlock because the pipeline keeps wanting to do a read.
    m_dcache.p_service.AddSource(ArbitrationSource(&m_dcache,   0)); // Memory read returns
    m_dcache.p_service.AddSource(ArbitrationSource(&m_pipeline, 0)); // Memory read/write

    m_allocator.p_allocation.AddSource(ArbitrationSource(&m_pipeline,  0)); // ALLOCATE instruction
    m_allocator.p_allocation.AddSource(ArbitrationSource(&m_network,   5)); // Group create
    m_allocator.p_allocation.AddSource(ArbitrationSource(&m_network,   4)); // Delegated create
    m_allocator.p_allocation.AddSource(ArbitrationSource(&m_allocator, 1)); // Delayed ALLOCATE instruction
    
    m_allocator.p_alloc.AddSource(ArbitrationSource(&m_network,   5)); // Group creates
    m_allocator.p_alloc.AddSource(ArbitrationSource(&m_allocator, 2)); // Local creates
    
    m_allocator.p_readyThreads.AddSource(ArbitrationSource(&m_pipeline,  0)); // Thread reschedule / wakeup due to write
    m_allocator.p_readyThreads.AddSource(ArbitrationSource(&m_fpu,       0)); // Thread wakeup due to FP completion
    m_allocator.p_readyThreads.AddSource(ArbitrationSource(&m_dcache,    0)); // Thread wakeup due to load completion
    m_allocator.p_readyThreads.AddSource(ArbitrationSource(&m_dcache,    1)); // Thread wakeup due to write completion
    m_allocator.p_readyThreads.AddSource(ArbitrationSource(&m_network,   0)); // Thread wakeup due to shared write
    m_allocator.p_readyThreads.AddSource(ArbitrationSource(&m_network,  13)); // Thread wakeup due to shared write
    m_allocator.p_readyThreads.AddSource(ArbitrationSource(&m_allocator, 0)); // Thread creation
    m_allocator.p_readyThreads.AddSource(ArbitrationSource(&m_allocator, 4)); // Thread wakeup due to sync

    m_allocator.p_activeThreads.AddSource(ArbitrationSource(&m_icache,    1)); // Thread activation due to I-Cache line return
    m_allocator.p_activeThreads.AddSource(ArbitrationSource(&m_allocator, 3)); // Thread activation due to I-Cache hit (from Ready Queue)

    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_fpu,       0)); // FPU Op writebacks
    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_dcache,    0)); // Mem Load writebacks
    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_network,   0)); // Group register receives
    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_network,   1)); // Register sends (waiting writeback)
    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_network,  13)); // Remote register receives
    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_allocator, 0)); // Thread allocation
    m_registerFile.p_asyncW.AddSource(ArbitrationSource(&m_allocator, 4)); // Syncs
    
    m_registerFile.p_asyncR.AddSource(ArbitrationSource(&m_network,   1)); // Remote register sends
    
    m_registerFile.p_pipelineR1.SetSource(ArbitrationSource(&m_pipeline, 0)); // Pipeline read stage
    m_registerFile.p_pipelineR2.SetSource(ArbitrationSource(&m_pipeline, 0)); // Pipeline read stage
    
    m_registerFile.p_pipelineW .SetSource(ArbitrationSource(&m_pipeline, 0)); // Pipeline writeback stage
    
    for (size_t i = 0; i < m_grid.size(); i++)
    {
        // Every core can delegate to this core
        m_network.m_delegateRemote      .AddSource(ArbitrationSource(&m_grid[i]->m_network, 4));
        m_network.m_delegateFailedRemote.AddSource(ArbitrationSource(&m_grid[i]->m_network, 15));

        // Every core can request registers
        m_network.m_registerRequestRemote .in.AddSource(ArbitrationSource(&m_grid[i]->m_network, 11));
        
        m_network.m_registerResponseRemote.in.AddSource(ArbitrationSource(&m_grid[i]->m_network, 12));
    }
    
    m_network.m_delegateLocal             .AddSource(ArbitrationSource(&m_allocator,      2)); // Create process sends delegated create
    m_network.m_createLocal               .AddSource(ArbitrationSource(&m_allocator,      2)); // Create process broadcasts create
    m_network.m_createRemote              .AddSource(ArbitrationSource(&prev.m_network,   5)); // Forward of group create
    m_network.m_delegateFailedLocal       .AddSource(ArbitrationSource(&m_network,        4)); // Delegation fails
    
    m_network.m_registerRequestRemote.out .AddSource(ArbitrationSource(&m_pipeline,       0)); // Non-full register with remote mapping read
    m_network.m_registerRequestRemote.out .AddSource(ArbitrationSource(&m_network,        1)); // Forward of global request to remote parent
    
    m_network.m_registerRequestGroup.out  .AddSource(ArbitrationSource(&m_network,        1)); // Forward of global request to group place
    m_network.m_registerRequestGroup.out  .AddSource(ArbitrationSource(&m_pipeline,       0)); // Non-full register with remote mapping read
    
    m_network.m_registerRequestGroup.in   .AddSource(ArbitrationSource(&next.m_network,   3)); // From neighbour
    
    m_network.m_registerResponseGroup.in  .AddSource(ArbitrationSource(&prev.m_network,   2)); // From neighbour
    
    m_network.m_registerResponseGroup.out .AddSource(ArbitrationSource(&m_pipeline,       0)); // Pipeline write to register with remote mapping
    m_network.m_registerResponseGroup.out .AddSource(ArbitrationSource(&m_fpu,            0)); // FP operation to a shared
    m_network.m_registerResponseGroup.out .AddSource(ArbitrationSource(&m_dcache,         0)); // Memory load to a shared completes
    m_network.m_registerResponseGroup.out .AddSource(ArbitrationSource(&m_network,        1)); // Returning register from a request
    m_network.m_registerResponseGroup.out .AddSource(ArbitrationSource(&m_network,        0)); // Forwarding global onto group
    m_network.m_registerResponseGroup.out .AddSource(ArbitrationSource(&m_network,       13)); // Forwarding response from remote parent onto group
    
    m_network.m_registerResponseRemote.out.AddSource(ArbitrationSource(&m_fpu,            0)); // FP operation to a shared
    m_network.m_registerResponseRemote.out.AddSource(ArbitrationSource(&m_pipeline,       0)); // Pipeline write to register with remote mapping
    m_network.m_registerResponseRemote.out.AddSource(ArbitrationSource(&m_network,        1)); // Returning register from a request
    m_network.m_registerResponseRemote.out.AddSource(ArbitrationSource(&m_network,        0)); // Forwarding response from remote parent onto group
    m_network.m_registerResponseRemote.out.AddSource(ArbitrationSource(&m_dcache,         0)); // Memory load to a shared completes
    
    m_network.m_completedThread           .AddSource(ArbitrationSource(&next.m_pipeline,  0)); // Thread terminated (reschedule at WB stage)
    m_network.m_cleanedUpThread           .AddSource(ArbitrationSource(&prev.m_allocator, 0)); // Thread cleaned up
    m_network.m_synchronizedFamily        .AddSource(ArbitrationSource(&prev.m_network,   9)); // Forwarding
    m_network.m_synchronizedFamily        .AddSource(ArbitrationSource(&m_allocator,      0)); // Dependencies resolved
    m_network.m_terminatedFamily          .AddSource(ArbitrationSource(&next.m_network,  10)); // Forwarding
    m_network.m_terminatedFamily          .AddSource(ArbitrationSource(&m_network,        5)); // Create with no threads
    m_network.m_terminatedFamily          .AddSource(ArbitrationSource(&m_allocator,      0)); // Last thread cleaned up
    m_network.m_remoteSync                .AddSource(ArbitrationSource(&prev.m_network,   9)); // Sync token caused sync
    m_network.m_remoteSync                .AddSource(ArbitrationSource(&m_allocator,      0)); // Thread administration caused sync

    if (m_pid == 0)
    {
        // Allocate the startup family on the first processor
        m_allocator.AllocateInitialFamily(runAddress, legacy);
    }    
}    

bool Processor::IsIdle() const
{
    return m_threadTable.IsEmpty() && m_familyTable.IsEmpty() && m_icache.IsEmpty();
}

unsigned int Processor::GetNumSuspendedRegisters() const
{
    unsigned int num = 0;
    for (RegType i = 0; i < NUM_REG_TYPES; ++i)
    {
        RegSize size = m_registerFile.GetSize(i);
        for (RegIndex r = 0; r < size; ++r)
        {
            RegValue value;
            m_registerFile.ReadRegister(MAKE_REGADDR(i, r), value);
            if (value.m_state == RST_WAITING) {
                ++num;
            }
        }
    }
    return num;
}

void Processor::ReserveTLS(MemAddr address, MemSize size)
{
    return m_memory.Reserve(address, size, IMemory::PERM_READ | IMemory::PERM_WRITE);
}

void Processor::UnreserveTLS(MemAddr address)
{
    return m_memory.Unreserve(address);
}

bool Processor::ReadMemory(MemAddr address, MemSize size, MemTag tag)
{
	return m_memory.Read(*this, address, size, tag);
}

bool Processor::WriteMemory(MemAddr address, const void* data, MemSize size, MemTag tag)
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

}
