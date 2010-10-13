#include "Processor.h"
#include "FPU.h"
#include "sampling.h"
#include "log2.h"

#include <cassert>

using namespace std;

namespace Simulator
{

//
// Processor implementation
//
Processor::Processor(const std::string& name, Object& parent, Clock& clock, GPID gpid, LPID lpid, const vector<Processor*>& grid, PSize gridSize, PlaceInfo& place, IMemory& memory, FPU& fpu, const Config& config)
:   Object(name, parent, clock),
    m_pid(gpid), m_memory(memory), m_grid(grid), m_gridSize(gridSize), m_place(place), m_fpu(fpu),
    m_allocator   ("alloc",     *this, clock, m_familyTable, m_threadTable, m_registerFile, m_raunit, m_icache, m_network, m_pipeline, place, lpid, config),
    m_icache      ("icache",    *this, clock, m_allocator, config),
    m_dcache      ("dcache",    *this, clock, m_allocator, m_familyTable, m_registerFile, config),
    m_registerFile("registers", *this, clock, m_allocator, config),
    m_pipeline    ("pipeline",  *this, clock, lpid, m_registerFile, m_network, m_allocator, m_familyTable, m_threadTable, m_icache, m_dcache, fpu, config),
    m_raunit      ("rau",       *this, clock, m_registerFile, config),
    m_familyTable ("families",  *this, clock, config),
    m_threadTable ("threads",   *this, clock, config),
    m_network     ("network",   *this, clock, place, grid, lpid, m_allocator, m_registerFile, m_familyTable)
{
    const Process* sources[] = {
        &m_icache.p_Outgoing,   // Outgoing process in I-Cache
        &m_dcache.p_Outgoing,   // Outgoing process in D-Cache
        NULL
    };
    
    m_memory.RegisterClient(m_pid, *this, sources);

    // Get the size, in bits, of various identifiers.
    // This is used for packing and unpacking various fields.
    m_bits.pid_bits = ilog2(GetGridSize());
    m_bits.fid_bits = ilog2(m_familyTable.GetFamilies().size());
    m_bits.tid_bits = ilog2(m_threadTable.GetNumThreads());
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
    m_dcache.p_service.AddProcess(m_dcache.p_IncomingReads);    // Memory read returns
    m_dcache.p_service.AddProcess(m_pipeline.p_Pipeline);       // Memory read/write

    m_allocator.p_allocation.AddProcess(m_pipeline.p_Pipeline);         // ALLOCATE instruction
    m_allocator.p_allocation.AddProcess(m_network.p_Creation);          // Group create
    m_allocator.p_allocation.AddProcess(m_network.p_DelegationIn);      // Delegated non-exclusive create
    m_allocator.p_allocation.AddProcess(m_allocator.p_FamilyAllocate);  // Delayed ALLOCATE instruction
    
    m_allocator.p_alloc.AddProcess(m_network.p_CreateResult);           // Non-last group creates
    m_allocator.p_alloc.AddProcess(m_network.p_Creation);               // Last group creates
    m_allocator.p_alloc.AddProcess(m_allocator.p_FamilyCreate);         // Local creates
    
    m_allocator.p_readyThreads.AddProcess(m_fpu.p_Pipeline);                // Thread wakeup due to FP completion
    m_allocator.p_readyThreads.AddProcess(m_dcache.p_IncomingReads);        // Thread wakeup due to load completion
    m_allocator.p_readyThreads.AddProcess(m_dcache.p_IncomingWrites);       // Thread wakeup due to write completion
    m_allocator.p_readyThreads.AddProcess(m_network.p_Registers);           // Thread wakeup due to write
    m_allocator.p_readyThreads.AddProcess(m_network.p_DelegationIn);        // Thread wakeup due to write
    m_allocator.p_readyThreads.AddProcess(m_allocator.p_ThreadAllocate);    // Thread creation
    m_allocator.p_readyThreads.AddProcess(m_allocator.p_RegWrites);         // Thread wakeup due to sync
    m_allocator.p_readyThreads.AddProcess(m_allocator.p_FamilyAllocate);    // Thread wakeup due to family allocation
    m_allocator.p_readyThreads.AddProcess(m_network.p_CreateResult);        // Thread wakeup due to group create completion
    m_allocator.p_readyThreads.AddProcess(m_allocator.p_FamilyCreate);      // Thread wakeup due to local create completion

    m_allocator.p_activeThreads.AddProcess(m_icache.p_Incoming);            // Thread activation due to I-Cache line return
    m_allocator.p_activeThreads.AddProcess(m_allocator.p_ThreadActivation); // Thread activation due to I-Cache hit (from Ready Queue)

    m_registerFile.p_asyncW.AddProcess(m_fpu.p_Pipeline);                   // FPU Op writebacks
    m_registerFile.p_asyncW.AddProcess(m_dcache.p_IncomingReads);           // Mem Load writebacks
    m_registerFile.p_asyncW.AddProcess(m_network.p_Registers);              // Group register receives
    m_registerFile.p_asyncW.AddProcess(m_network.p_DelegationIn);           // Remote register receives
    m_registerFile.p_asyncW.AddProcess(m_allocator.p_ThreadAllocate);       // Thread allocation
    m_registerFile.p_asyncW.AddProcess(m_allocator.p_RegWrites);            // Syncs
    m_registerFile.p_asyncW.AddProcess(m_allocator.p_FamilyAllocate);       // Family allocation
    m_registerFile.p_asyncW.AddProcess(m_network.p_CreateResult);           // Group create completion
    m_registerFile.p_asyncW.AddProcess(m_allocator.p_FamilyCreate);         // Local create completion
    
    m_registerFile.p_asyncR.AddProcess(m_network.p_Registers);              // Remote register sends
    m_registerFile.p_asyncR.AddProcess(m_network.p_DelegationIn);           // Remote register sends
    
    m_registerFile.p_pipelineR1.SetProcess(m_pipeline.p_Pipeline);          // Pipeline read stage
    m_registerFile.p_pipelineR2.SetProcess(m_pipeline.p_Pipeline);          // Pipeline read stage
    
    m_registerFile.p_pipelineW .SetProcess(m_pipeline.p_Pipeline);          // Pipeline writeback stage
    
    m_network.m_delegateIn.AddProcess(m_pipeline.p_Pipeline);               // Sending local messages
    for (size_t i = 0; i < m_grid.size(); i++)
    {
        // Every core can send delegation messages here
        m_network.m_delegateIn.AddProcess(m_grid[i]->m_network.p_DelegationOut);
    }
    
    m_network.m_createLocal           .AddProcess(m_allocator.p_FamilyCreate);   // Create process broadcasts create
    m_network.m_createRemote          .AddProcess(prev.m_network.p_Creation);    // Forward of group create
    m_network.m_createResult.out      .AddProcess(m_network.p_CreateResult);     // Forward create result
    m_network.m_createResult.out      .AddProcess(m_network.p_Creation);         // Group create triggers result
    
    m_network.m_delegateOut           .AddProcess(m_network.p_DelegationIn);     // Returning registers
    m_network.m_delegateOut           .AddProcess(m_allocator.p_FamilyCreate);   // Create process sends delegated create
    m_network.m_delegateOut           .AddProcess(m_allocator.p_FamilyAllocate); // Allocation process sends FID
    m_network.m_delegateOut           .AddProcess(m_pipeline.p_Pipeline);        // Sending or requesting registers
    
    m_network.m_registers.out         .AddProcess(m_network.p_Registers);        // Forwarding register messages
    m_network.m_registers.out         .AddProcess(m_network.p_DelegationIn);     // Forwarding incoming globals and shareds to next core
    m_network.m_registers.out         .AddProcess(m_pipeline.p_Pipeline);        // Pipeline write to register with remote mapping
    
    m_network.m_registers.out         .AddProcess(m_allocator.p_ThreadAllocate); // Thread cleaned up
    m_network.m_synchronizedFamily.out.AddProcess(m_network.p_FamilySync);       // Forwarding
    m_network.m_synchronizedFamily.out.AddProcess(m_allocator.p_ThreadAllocate); // Dependencies resolved
    m_network.m_synchronizedFamily.out.AddProcess(m_network.p_CreateResult);     // Create completion on next core causes family synch
    m_network.m_delegateOut           .AddProcess(m_allocator.p_ThreadAllocate); // Thread administration caused sync

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

void Processor::MapMemory(MemAddr address, MemSize size)
{
    m_memory.Reserve(address, size, IMemory::PERM_READ | IMemory::PERM_WRITE);
}

void Processor::UnmapMemory(MemAddr address, MemSize size)
{
    // TODO: possibly check the size matches the reserved size
    m_memory.Unreserve(address);
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
    bool mp = m_memory.CheckPermissions(address, size, access);
    if (!mp && (access & IMemory::PERM_READ) && (address & (1ULL << (sizeof(MemAddr) * 8 - 1))))
    {
        // we allow reads to the first cache line (64 bytes) of TLS to always succeed.

        // find the mask for the lower bits of TLS
        MemAddr mask = 1ULL << (sizeof(MemAddr) * 8 - (m_bits.pid_bits + m_bits.tid_bits + 1));
        mask -= 1;

        // ignore the lower 63 bytes
        mask ^= 63;

        if ((address & mask) == 0)
            return true;
    }
    return mp;
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

Integer Processor::GetProfileWord(unsigned int i) const
{
    vector<Processor*>::const_iterator gi;

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
        for (gi = m_grid.begin(); gi != m_grid.end(); ++gi)
        {
            Processor* p = *gi;
            if (&p->m_place == &m_place)
            {
                ops += p->GetPipeline().GetOp();
            }
        }
        return ops;
    }
    
    case 2:
    {
        // Return the number of issued FP instructions on all cores
        Integer flops = 0;
        for (gi = m_grid.begin(); gi != m_grid.end(); ++gi)
        {
            Processor* p = *gi;
            if (&p->m_place == &m_place)
            {
                flops += p->GetPipeline().GetFlop();
            }
        }
        return flops;
    }

    case 3:
    {
        // Return the number of completed loads on all cores
        uint64_t n = 0, dummy;
        for (gi = m_grid.begin(); gi != m_grid.end(); ++gi)
        {
            Processor* p = *gi;
            if (&p->m_place == &m_place)
            {
                p->GetPipeline().CollectMemOpStatistics(n, dummy, dummy, dummy);
            }
        }
        return (Integer)n;
    }

    case 4:
    {
        // Return the number of completed stores on all cores
        uint64_t n = 0, dummy;
        for (gi = m_grid.begin(); gi != m_grid.end(); ++gi)
        {
            Processor* p = *gi;
            if (&p->m_place == &m_place)
            {
                p->GetPipeline().CollectMemOpStatistics(dummy, n, dummy, dummy);
            }
        }
        return (Integer)n;
    }

    case 5:
    {
        // Return the number of successfully loaded bytes on all cores
        uint64_t n = 0, dummy;
        for (gi = m_grid.begin(); gi != m_grid.end(); ++gi)
        {
            Processor* p = *gi;
            if (&p->m_place == &m_place)
            {
                p->GetPipeline().CollectMemOpStatistics(dummy, dummy, n, dummy);
            }
        }
        return (Integer)n;
    }

    case 6:
    {
        // Return the number of successfully stored bytes on all cores
        uint64_t n = 0, dummy;
        for (gi = m_grid.begin(); gi != m_grid.end(); ++gi)
        {
            Processor* p = *gi;
            if (&p->m_place == &m_place)
            {
                p->GetPipeline().CollectMemOpStatistics(dummy, dummy, dummy, n);
            }
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

    case 9:
    {
        return (Integer)GetPlaceSize();
    }

    case 10:
    {
        // Return the total cumulative allocated thread slots
        Integer alloc = 0;
        for (gi = m_grid.begin(); gi != m_grid.end(); ++gi)
        {
            Processor* p = *gi;
            if (&p->m_place == &m_place)
            {
                alloc += p->GetTotalThreadsAllocated();
            }
        }
        return alloc;
    }

    case 11:
    {
        // Return the total cumulative allocated thread slots
        Integer alloc = 0;
        for (gi = m_grid.begin(); gi != m_grid.end(); ++gi)
        {
            Processor* p = *gi;
            if (&p->m_place == &m_place)
            {
                alloc += p->GetTotalFamiliesAllocated();
            }
        }
        return alloc;
    }

    case 12:
    {
        // Return the total cumulative exclusive allocate queue size
        Integer alloc = 0;
        for (gi = m_grid.begin(); gi != m_grid.end(); ++gi)
        {
            Processor* p = *gi;
            if (&p->m_place == &m_place)
            {
                alloc += p->GetTotalAllocateExQueueSize();
            }
        }
        return alloc;
    }

        
    default:
        return 0;
    }
}

//
// Below are the various functions that construct configuration-dependent values
//
MemAddr Processor::GetTLSAddress(LFID /* fid */, TID tid) const
{
    // 1 bit for TLS/GS
    // P bits for CPU
    // T bits for TID
    assert(sizeof(MemAddr) * 8 > m_bits.pid_bits + m_bits.tid_bits + 1);

    unsigned int Ls  = sizeof(MemAddr) * 8 - 1;
    unsigned int Ps  = Ls - m_bits.pid_bits;
    unsigned int Ts  = Ps - m_bits.tid_bits;

    return (static_cast<MemAddr>(1)     << Ls) |
           (static_cast<MemAddr>(m_pid) << Ps) |
           (static_cast<MemAddr>(tid)   << Ts);
}

MemSize Processor::GetTLSSize() const
{
    assert(sizeof(MemAddr) * 8 > m_bits.pid_bits + m_bits.tid_bits + 1);

    return static_cast<MemSize>(1) << (sizeof(MemSize) * 8 - (1 + m_bits.pid_bits + m_bits.tid_bits));
}

static Integer GenerateCapability(unsigned int bits)
{
    Integer capability = 0;
    Integer step = (Integer)RAND_MAX + 1;
    for (Integer limit = (1ULL << bits) + step - 1; limit > 0; limit /= step)
    {
        capability = capability * step + rand();
    }
    return capability & ((1ULL << bits) - 1);
}

FCapability Processor::GenerateFamilyCapability() const
{
    assert(sizeof(Integer) * 8 > m_bits.pid_bits + m_bits.fid_bits);
    return GenerateCapability(sizeof(Integer) * 8 - m_bits.pid_bits - m_bits.fid_bits);
}

PlaceID Processor::UnpackPlace(Integer id) const
{
    // Unpack the place value: <Capability:N, PID:P, Suspend:1, Type:2, Exclusive:1>
    PlaceID place;
    place.exclusive  = (((id >> 0) & 1) != 0);
    place.type       = (PlaceType)((id >> 1) & 3);
    place.suspend    = (((id >> 3) & 1) != 0) || place.exclusive;
    place.pid        = (GPID)((id >> 4) & ((1ULL << m_bits.pid_bits) - 1));
    place.capability = id >> (m_bits.pid_bits + 4);
    return place;
}

FID Processor::UnpackFID(Integer id) const
{
    // Unpack the FID: <Capability:N, LFID:F, PID:P>
    FID fid;
    fid.pid        = (GPID)((id >>               0) & ((1ULL << m_bits.pid_bits) - 1));
    fid.lfid       = (GPID)((id >> m_bits.pid_bits) & ((1ULL << m_bits.fid_bits) - 1));
    fid.capability = id >> (m_bits.pid_bits + m_bits.fid_bits);
    return fid;
}

Integer Processor::PackFID(const FID& fid) const
{
    // Construct the FID: <Capability:N, LFID:F, PID:P>
    return (fid.capability << (m_bits.pid_bits + m_bits.fid_bits)) | (fid.lfid << m_bits.pid_bits) | fid.pid;
}

}
