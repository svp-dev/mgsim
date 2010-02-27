#include "Processor.h"
#include "FPU.h"
#include <cassert>
using namespace std;

namespace Simulator
{

//
// Processor implementation
//
Processor::Processor(const std::string& name, Object& parent, GPID gpid, LPID lpid, const vector<Processor*>& grid, PSize gridSize, PlaceInfo& place, IMemory& memory, FPU& fpu, const Config& config)
:   Object(name, parent),
    m_pid(gpid), m_memory(memory), m_grid(grid), m_gridSize(gridSize), m_place(place), m_fpu(fpu),
    m_localFamilyCompletion(0),
    m_allocator   ("alloc",     *this, m_familyTable, m_threadTable, m_registerFile, m_raunit, m_icache, m_network, m_pipeline, place, lpid, config),
    m_icache      ("icache",    *this, m_allocator, config),
    m_dcache      ("dcache",    *this, m_allocator, m_familyTable, m_registerFile, config),
    m_registerFile("registers", *this, m_allocator, m_network, config),
    m_pipeline    ("pipeline",  *this, lpid, m_registerFile, m_network, m_allocator, m_familyTable, m_threadTable, m_icache, m_dcache, fpu, config),
    m_raunit      ("rau",       *this, m_registerFile, config),
    m_familyTable ("families",  *this, config),
    m_threadTable ("threads",   *this, config),
    m_network     ("network",   *this, place, grid, lpid, m_allocator, m_registerFile, m_familyTable)
{
    const Process* sources[] = {
        &m_icache.p_Outgoing,   // Outgoing process in I-Cache
        &m_dcache.p_Outgoing,   // Outgoing process in D-Cache
        NULL
    };
    
    m_memory.RegisterClient(m_pid, *this, sources);
}

Processor::~Processor()
{
    m_memory.UnregisterClient(m_pid);
}

void Processor::Initialize(Processor& prev, Processor& next, MemAddr runAddress, bool legacy)
{
    m_network.Initialize(prev.m_network, next.m_network);

    //
    // Set port priorities and connections on all components.
    // First source on a port has the highest priority.
    //

    m_icache.p_service.AddProcess(m_icache.p_Incoming);             // Cache-line returns
    m_icache.p_service.AddProcess(m_allocator.p_ThreadActivation);  // Thread activation
    m_icache.p_service.AddProcess(m_allocator.p_FamilyCreate);      // Create process

    // Unfortunately the D-Cache needs priority here because otherwise all cache-lines can
    // remain filled and we get deadlock because the pipeline keeps wanting to do a read.
    m_dcache.p_service.AddProcess(m_dcache.p_IncomingReads);   // Memory read returns
    m_dcache.p_service.AddProcess(m_pipeline.p_Pipeline);       // Memory read/write

    m_allocator.p_allocation.AddProcess(m_pipeline.p_Pipeline);         // ALLOCATE instruction
    m_allocator.p_allocation.AddProcess(m_network.p_Creation);          // Group create
    m_allocator.p_allocation.AddProcess(m_network.p_Delegation);        // Delegated create
    m_allocator.p_allocation.AddProcess(m_allocator.p_FamilyAllocate);  // Delayed ALLOCATE instruction
    
    m_allocator.p_alloc.AddProcess(m_network.p_Creation);               // Group creates
    m_allocator.p_alloc.AddProcess(m_allocator.p_FamilyCreate);         // Local creates
    
    m_allocator.p_readyThreads.AddProcess(m_pipeline.p_Pipeline);           // Thread reschedule / wakeup due to write
    m_allocator.p_readyThreads.AddProcess(m_fpu.p_Pipeline);                // Thread wakeup due to FP completion
    m_allocator.p_readyThreads.AddProcess(m_dcache.p_IncomingReads);        // Thread wakeup due to load completion
    m_allocator.p_readyThreads.AddProcess(m_dcache.p_IncomingWrites);       // Thread wakeup due to write completion
    m_allocator.p_readyThreads.AddProcess(m_network.p_RegResponseInGroup);  // Thread wakeup due to shared write
    m_allocator.p_readyThreads.AddProcess(m_network.p_RegResponseInRemote); // Thread wakeup due to shared write
    m_allocator.p_readyThreads.AddProcess(m_allocator.p_ThreadAllocate);    // Thread creation
    m_allocator.p_readyThreads.AddProcess(m_allocator.p_RegWrites);         // Thread wakeup due to sync

    m_allocator.p_activeThreads.AddProcess(m_icache.p_Incoming);            // Thread activation due to I-Cache line return
    m_allocator.p_activeThreads.AddProcess(m_allocator.p_ThreadActivation); // Thread activation due to I-Cache hit (from Ready Queue)

    m_registerFile.p_asyncW.AddProcess(m_fpu.p_Pipeline);                   // FPU Op writebacks
    m_registerFile.p_asyncW.AddProcess(m_dcache.p_IncomingReads);           // Mem Load writebacks
    m_registerFile.p_asyncW.AddProcess(m_network.p_RegResponseInGroup);     // Group register receives
    m_registerFile.p_asyncW.AddProcess(m_network.p_RegRequestIn);           // Register sends (waiting writeback)
    m_registerFile.p_asyncW.AddProcess(m_network.p_RegResponseInRemote);    // Remote register receives
    m_registerFile.p_asyncW.AddProcess(m_allocator.p_ThreadAllocate);       // Thread allocation
    m_registerFile.p_asyncW.AddProcess(m_allocator.p_RegWrites);            // Syncs
    
    m_registerFile.p_asyncR.AddProcess(m_network.p_RegRequestIn);           // Remote register sends
    
    m_registerFile.p_pipelineR1.SetProcess(m_pipeline.p_Pipeline);          // Pipeline read stage
    m_registerFile.p_pipelineR2.SetProcess(m_pipeline.p_Pipeline);          // Pipeline read stage
    
    m_registerFile.p_pipelineW .SetProcess(m_pipeline.p_Pipeline);          // Pipeline writeback stage
    
    for (size_t i = 0; i < m_grid.size(); i++)
    {
        // Every core can delegate to this core
        m_network.m_delegateRemote      .AddProcess(m_grid[i]->m_network.p_Delegation);
        m_network.m_delegateFailedRemote.AddProcess(m_grid[i]->m_network.p_DelegateFailedOut);

        // Every core can request registers
        m_network.m_registerRequestRemote .in.AddProcess(m_grid[i]->m_network.p_RegRequestOutRemote);
        
        m_network.m_registerResponseRemote.in.AddProcess(m_grid[i]->m_network.p_RegResponseOutRemote);
    }
    
    m_network.m_delegateLocal             .AddProcess(m_allocator.p_FamilyCreate);  // Create process sends delegated create
    m_network.m_createLocal               .AddProcess(m_allocator.p_FamilyCreate);  // Create process broadcasts create
    m_network.m_createRemote              .AddProcess(prev.m_network.p_Creation);   // Forward of group create
    m_network.m_delegateFailedLocal       .AddProcess(m_network.p_Delegation);      // Delegation fails
    
    m_network.m_registerRequestRemote.out .AddProcess(m_pipeline.p_Pipeline);       // Non-full register with remote mapping read
    m_network.m_registerRequestRemote.out .AddProcess(m_network.p_RegRequestIn);    // Forward of global request to remote parent
    
    m_network.m_registerRequestGroup.out  .AddProcess(m_network.p_RegRequestIn);    // Forward of global request to group place
    m_network.m_registerRequestGroup.out  .AddProcess(m_pipeline.p_Pipeline);       // Non-full register with remote mapping read
    
    m_network.m_registerRequestGroup.in   .AddProcess(next.m_network.p_RegRequestOutGroup); // From neighbour
    
    m_network.m_registerResponseGroup.in  .AddProcess(prev.m_network.p_RegResponseOutGroup); // From neighbour
    
    m_network.m_registerResponseGroup.out .AddProcess(m_pipeline.p_Pipeline);           // Pipeline write to register with remote mapping
    m_network.m_registerResponseGroup.out .AddProcess(m_fpu.p_Pipeline);                // FP operation to a shared
    m_network.m_registerResponseGroup.out .AddProcess(m_dcache.p_IncomingReads);        // Memory load to a shared completes
    m_network.m_registerResponseGroup.out .AddProcess(m_network.p_RegRequestIn);        // Returning register from a request
    m_network.m_registerResponseGroup.out .AddProcess(m_network.p_RegResponseInGroup);  // Forwarding global onto group
    m_network.m_registerResponseGroup.out .AddProcess(m_network.p_RegResponseInRemote); // Forwarding response from remote parent onto group
    
    m_network.m_registerResponseRemote.out.AddProcess(m_fpu.p_Pipeline);                // FP operation to a shared
    m_network.m_registerResponseRemote.out.AddProcess(m_pipeline.p_Pipeline);           // Pipeline write to register with remote mapping
    m_network.m_registerResponseRemote.out.AddProcess(m_network.p_RegRequestIn);        // Returning register from a request
    m_network.m_registerResponseRemote.out.AddProcess(m_network.p_RegResponseInGroup);  // Forwarding response from remote parent onto group
    m_network.m_registerResponseRemote.out.AddProcess(m_dcache.p_IncomingReads);        // Memory load to a shared completes
    
    m_network.m_completedThread           .AddProcess(next.m_pipeline.p_Pipeline);          // Thread terminated (reschedule at WB stage)
    m_network.m_cleanedUpThread           .AddProcess(prev.m_allocator.p_ThreadAllocate);   // Thread cleaned up
    m_network.m_synchronizedFamily        .AddProcess(prev.m_network.p_FamilySync);         // Forwarding
    m_network.m_synchronizedFamily        .AddProcess(m_allocator.p_ThreadAllocate);        // Dependencies resolved
    m_network.m_terminatedFamily          .AddProcess(next.m_network.p_FamilyTermination);  // Forwarding
    m_network.m_terminatedFamily          .AddProcess(m_network.p_Creation);                // Create with no threads
    m_network.m_terminatedFamily          .AddProcess(m_allocator.p_ThreadAllocate);        // Last thread cleaned up
    m_network.m_remoteSync                .AddProcess(prev.m_network.p_FamilySync);         // Sync token caused sync
    m_network.m_remoteSync                .AddProcess(m_allocator.p_ThreadAllocate);        // Thread administration caused sync

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

bool Processor::ReadMemory(MemAddr address, MemSize size)
{
    return m_memory.Read(m_pid, address, size);
}

bool Processor::WriteMemory(MemAddr address, const void* data, MemSize size, TID tid)
{
    return m_memory.Write(m_pid, address, data, size, tid);
}

bool Processor::CheckPermissions(MemAddr address, MemSize size, int access) const
{
    return m_memory.CheckPermissions(address, size, access);
}

bool Processor::OnMemoryReadCompleted(MemAddr address, const MemData& data)
{
    // Notify I-Cache and D-Cache (they both snoop: they're on a bus)
    return m_dcache.OnMemoryReadCompleted(address, data) && 
           m_icache.OnMemoryReadCompleted(address, data);
}

bool Processor::OnMemoryWriteCompleted(TID tid)
{
    // Dispatch result to D-Cache
    return m_dcache.OnMemoryWriteCompleted(tid);
}

bool Processor::OnMemorySnooped(MemAddr addr, const MemData& data)
{
    return m_dcache.OnMemorySnooped(addr, data) &&
           m_icache.OnMemorySnooped(addr, data);
}

bool Processor::OnMemoryInvalidated(MemAddr addr)
{
    return m_dcache.OnMemoryInvalidated(addr) &&
           m_icache.OnMemoryInvalidated(addr);
}

void Processor::OnFamilyTerminatedLocally(MemAddr /* pc */)
{
    m_localFamilyCompletion = GetKernel()->GetCycleNo();
}

Integer Processor::GetProfileWord(unsigned int i) const
{
    switch (i)
    {
    case 0:
    {
        // Return the number of elapsed cycles
        return (Integer)GetKernel()->GetCycleNo();
    }
    case 1:
    {
        // Return the number of executed instructions on all cores
        Integer ops = 0;
        for (size_t i = 0; i < m_grid.size(); ++i) {
            ops += m_grid[i]->GetOp();
        }
        return ops;
    }
    
    case 2:
    {
        // Return the number of issued FP instructions on all cores
        Integer flops = 0;
        for (size_t i = 0; i < m_grid.size(); ++i) {
            flops += m_grid[i]->GetFlop();
        }
        return flops;
    }

    case 3:
    {
        // Return the number of completed loads on all cores
        uint64_t n = 0, dummy;
        for (size_t i = 0; i < m_grid.size(); ++i) {
            m_grid[i]->CollectMemOpStatistics(n, dummy, dummy, dummy);
        }
        return (Integer)n;
    }

    case 4:
    {
        // Return the number of completed stores on all coresp
        uint64_t n = 0, dummy;
        for (size_t i = 0; i < m_grid.size(); ++i) {
            m_grid[i]->CollectMemOpStatistics(dummy, n, dummy, dummy);
        }
        return (Integer)n;
    }

    case 5:
    {
        // Return the number of successfully loaded bytes on all cores
        uint64_t n = 0, dummy;
        for (size_t i = 0; i < m_grid.size(); ++i) {
            m_grid[i]->CollectMemOpStatistics(dummy, dummy, n, dummy);
        }
        return (Integer)n;
    }

    case 6:
    {
        // Return the number of successfully stored bytes on all coresp
        uint64_t n = 0, dummy;
        for (size_t i = 0; i < m_grid.size(); ++i) {
            m_grid[i]->CollectMemOpStatistics(dummy, dummy, dummy, n);
        }
        return (Integer)n;
    }

    case 7:
    {
        // Return the number of external memory loads (cache lines)
        uint64_t n, dummy;
        m_memory.GetMemoryStatistics(n, dummy, dummy, dummy);
        return (Integer)n;
    }

    case 8:
    {
        // Return the number of external memory stores (cache lines)
        uint64_t n, dummy;
        m_memory.GetMemoryStatistics(dummy, n, dummy, dummy);
        return (Integer)n;
    }
        
    default:
        return 0;
    }
}

}
