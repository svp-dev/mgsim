#include "Processor.h"
#include "FPU.h"
#include "sampling.h"
#include "log2.h"

#include <cassert>
#include <sys/time.h>
#include <ctime>

using namespace std;

namespace Simulator
{

template <typename T>
static bool IsPowerOfTwo(const T& x)
{
    return (x & (x - 1)) == 0;
}

//
// Processor implementation
//
Processor::Processor(const std::string& name, Object& parent, Clock& clock, PID pid, const vector<Processor*>& grid, IMemory& memory, FPU& fpu, const Config& config)
:   Object(name, parent, clock),
    m_pid(pid), m_memory(memory), m_grid(grid), m_fpu(fpu),
    m_allocator   ("alloc",     *this, clock, m_familyTable, m_threadTable, m_registerFile, m_raunit, m_icache, m_network, m_pipeline, config),
    m_icache      ("icache",    *this, clock, m_allocator, config),
    m_dcache      ("dcache",    *this, clock, m_allocator, m_familyTable, m_registerFile, config),
    m_registerFile("registers", *this, clock, m_allocator, config),
    m_pipeline    ("pipeline",  *this, clock, m_registerFile, m_network, m_allocator, m_familyTable, m_threadTable, m_icache, m_dcache, fpu, config),
    m_raunit      ("rau",       *this, clock, m_registerFile, config),
    m_familyTable ("families",  *this, clock, config),
    m_threadTable ("threads",   *this, clock, config),
    m_network     ("network",   *this, clock, grid, m_allocator, m_registerFile, m_familyTable, config)
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

void Processor::Initialize(Processor* prev, Processor* next, MemAddr runAddress, bool legacy)
{
    m_network.Initialize(prev != NULL ? &prev->m_network : NULL, next != NULL ? &next->m_network : NULL);

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
    m_allocator.p_allocation.AddProcess(m_network.p_DelegationIn);      // Delegated non-exclusive create
    m_allocator.p_allocation.AddProcess(m_allocator.p_FamilyAllocate);  // Delayed ALLOCATE instruction
    
    m_allocator.p_alloc.AddProcess(m_network.p_Link);                   // Place-wide create
    m_allocator.p_alloc.AddProcess(m_allocator.p_FamilyCreate);         // Local creates
    
    m_allocator.p_readyThreads.AddProcess(m_dcache.p_IncomingReads);        // Thread wakeup due to load completion
    m_allocator.p_readyThreads.AddProcess(m_dcache.p_IncomingWrites);       // Thread wakeup due to write completion
    m_allocator.p_readyThreads.AddProcess(m_fpu.p_Pipeline);                // Thread wakeup due to FP completion
    m_allocator.p_readyThreads.AddProcess(m_network.p_Link);                // Thread wakeup due to write
    m_allocator.p_readyThreads.AddProcess(m_network.p_DelegationIn);        // Thread wakeup due to write
    m_allocator.p_readyThreads.AddProcess(m_allocator.p_ThreadAllocate);    // Thread creation
    m_allocator.p_readyThreads.AddProcess(m_allocator.p_FamilyAllocate);    // Thread wakeup due to family allocation
    m_allocator.p_readyThreads.AddProcess(m_allocator.p_FamilyCreate);      // Thread wakeup due to local create completion

    m_allocator.p_activeThreads.AddProcess(m_icache.p_Incoming);            // Thread activation due to I-Cache line return
    m_allocator.p_activeThreads.AddProcess(m_allocator.p_ThreadActivation); // Thread activation due to I-Cache hit (from Ready Queue)

    m_registerFile.p_asyncW.AddProcess(m_dcache.p_IncomingReads);           // Mem Load writebacks
    m_registerFile.p_asyncW.AddProcess(m_fpu.p_Pipeline);                   // FPU Op writebacks
    m_registerFile.p_asyncW.AddProcess(m_network.p_Link);                   // Place register receives
    m_registerFile.p_asyncW.AddProcess(m_network.p_DelegationIn);           // Remote register receives
    m_registerFile.p_asyncW.AddProcess(m_allocator.p_ThreadAllocate);       // Thread allocation
    
    m_registerFile.p_asyncR.AddProcess(m_network.p_DelegationIn);           // Remote register requests
    
    m_registerFile.p_pipelineR1.SetProcess(m_pipeline.p_Pipeline);          // Pipeline read stage
    m_registerFile.p_pipelineR2.SetProcess(m_pipeline.p_Pipeline);          // Pipeline read stage
    
    m_registerFile.p_pipelineW .SetProcess(m_pipeline.p_Pipeline);          // Pipeline writeback stage
    
    m_network.m_allocResponse.out.AddProcess(m_network.p_AllocResponse);    // Forwarding allocation response
    m_network.m_allocResponse.out.AddProcess(m_allocator.p_FamilyAllocate); // Sending allocation response

    m_network.m_link.out.AddProcess(m_network.p_Link);                      // Forwarding link messages
    m_network.m_link.out.AddProcess(m_network.p_DelegationIn);              // Delegation message forwards onto link
    m_network.m_link.out.AddProcess(m_allocator.p_FamilyAllocate);          // Allocate process sending place-wide allocate
    m_network.m_link.out.AddProcess(m_allocator.p_FamilyCreate);            // Create process sends place-wide create
    m_network.m_link.out.AddProcess(m_allocator.p_ThreadAllocate);          // Thread cleanup causes sync
    
    m_network.m_delegateIn.AddProcess(m_network.p_Link);                    // Link messages causes remote 
    m_network.m_delegateIn.AddProcess(m_allocator.p_ThreadAllocate);        // Allocate process completes family sync
    m_network.m_delegateIn.AddProcess(m_allocator.p_FamilyAllocate);        // Allocate process returning FID
    m_network.m_delegateIn.AddProcess(m_allocator.p_FamilyCreate);          // Create process returning FID
    m_network.m_delegateIn.AddProcess(m_network.p_AllocResponse);           // Allocate response writing back to parent
    m_network.m_delegateIn.AddProcess(m_pipeline.p_Pipeline);               // Sending local messages
    for (size_t i = 0; i < m_grid.size(); i++)
    {
        // Every core can send delegation messages here
        m_network.m_delegateIn.AddProcess(m_grid[i]->m_network.p_DelegationOut);
    }
    
    m_network.m_delegateOut.AddProcess(m_pipeline.p_Pipeline);        // Sending or requesting registers
    m_network.m_delegateOut.AddProcess(m_network.p_DelegationIn);     // Returning registers
    m_network.m_delegateOut.AddProcess(m_network.p_Link);             // Place sync causes final sync
    m_network.m_delegateOut.AddProcess(m_network.p_AllocResponse);    // Allocate response writing back to parent
    m_network.m_delegateOut.AddProcess(m_allocator.p_FamilyAllocate); // Allocation process sends FID
    m_network.m_delegateOut.AddProcess(m_allocator.p_FamilyCreate);   // Create process sends delegated create
    m_network.m_delegateOut.AddProcess(m_allocator.p_ThreadAllocate); // Thread cleanup caused sync

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

        // ignore the lower 1K of TLS heap
        mask ^= 1023;

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

Integer Processor::GetProfileWord(unsigned int i, PSize placeSize) const
{
    const size_t placeStart = (m_pid / placeSize) * placeSize;
    const size_t placeEnd   = placeStart + placeSize;

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
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            ops += m_grid[i]->GetPipeline().GetOp();
        }
        return ops;
    }
    
    case 2:
    {
        // Return the number of issued FP instructions on all cores
        Integer flops = 0;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            flops += m_grid[i]->GetPipeline().GetFlop();
        }
        return flops;
    }

    case 3:
    {
        // Return the number of completed loads on all cores
        uint64_t n = 0, dummy;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            m_grid[i]->GetPipeline().CollectMemOpStatistics(n, dummy, dummy, dummy);
        }
        return (Integer)n;
    }

    case 4:
    {
        // Return the number of completed stores on all cores
        uint64_t n = 0, dummy;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            m_grid[i]->GetPipeline().CollectMemOpStatistics(dummy, n, dummy, dummy);
        }
        return (Integer)n;
    }

    case 5:
    {
        // Return the number of successfully loaded bytes on all cores
        uint64_t n = 0, dummy;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            m_grid[i]->GetPipeline().CollectMemOpStatistics(dummy, dummy, n, dummy);
        }
        return (Integer)n;
    }

    case 6:
    {
        // Return the number of successfully stored bytes on all cores
        uint64_t n = 0, dummy;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            m_grid[i]->GetPipeline().CollectMemOpStatistics(dummy, dummy, dummy, n);
        }
        return (Integer)n;
    }

    case 7:
    {
        // Return the number of memory loads overall from L1 to L2 (cache lines)
        uint64_t n, dummy;
        m_memory.GetMemoryStatistics(n, dummy, dummy, dummy, dummy, dummy);
        return (Integer)n;
    }

    case 8:
    {
        // Return the number of memory stores overall from L1 to L2 (cache lines)
        uint64_t n, dummy;
        m_memory.GetMemoryStatistics(dummy, n, dummy, dummy, dummy, dummy);
        return (Integer)n;
    }

    case 9:
    {
        return (Integer)placeSize;
    }

    case 10:
    {
        // Return the total cumulative allocated thread slots
        Integer alloc = 0;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            alloc += m_grid[i]->GetTotalThreadsAllocated();
        }
        return alloc;
    }

    case 11:
    {
        // Return the total cumulative allocated thread slots
        Integer alloc = 0;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            alloc += m_grid[i]->GetTotalFamiliesAllocated();
        }
        return alloc;
    }

    case 12:
    {
        // Return the total cumulative exclusive allocate queue size
        Integer alloc = 0;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            alloc += m_grid[i]->GetTotalAllocateExQueueSize();
        }
        return alloc;
    }

    case 13:
    {
        // Return the Unix time
        return (Integer)time(0);
    }

    case 14:
    {
        // Return the local date as a packed struct
        // bits 0-4: day in month
        // bits 5-8: month in year
        // bits 9-31: year from 1900
        time_t c = time(0);
        struct tm * tm = gmtime(&c);
        return (Integer)tm->tm_mday |
            ((Integer)tm->tm_mon << 5) |
            ((Integer)tm->tm_year << 9);
    }
    case 15:
    {
        // Return the local time as a packed struct
        // bits 0-14 = microseconds / 2^17  (topmost 15 bits)
        // bits 15-20 = seconds
        // bits 21-26 = minutes
        // bits 27-31 = hours
        struct timeval tv;
        gettimeofday(&tv, 0);
        struct tm * tm = gmtime(&tv.tv_sec);

        // get topmost 15 bits of precision of the usec field
        Integer usec = (tv.tv_usec >> (32-15)) & 0x7fff;
        return usec | (tm->tm_sec << 15) | (tm->tm_min << 21) | (tm->tm_hour << 27);
    }       
        
    case 16:
    {
        // Return the number of memory loads overall from external memory (cache lines)
        uint64_t n, dummy;
        m_memory.GetMemoryStatistics(dummy, dummy, dummy, dummy, n, dummy);
        return (Integer)n;
    }

    case 17:
    {
        // Return the number of memory stores overall to external memory (cache lines)
        uint64_t n, dummy;
        m_memory.GetMemoryStatistics(dummy, dummy, dummy, dummy, dummy, n);
        return (Integer)n;
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

Integer Processor::PackPlace(const PlaceID& place) const
{
    assert(IsPowerOfTwo(place.size));
    assert(place.pid % place.size == 0);
    
    return place.capability << (m_bits.pid_bits + 1) | (place.pid << 1) | place.size;
}

PlaceID Processor::UnpackPlace(Integer id) const
{
    // Unpack the place value: <Capability:N, PID*2|Size:P+1>
    PlaceID place;
    
    place.size       = 0;
    place.pid        = 0;
    place.capability = id >> (m_bits.pid_bits + 1);

    // Clear the capability bits
    id &= (2ULL << m_bits.pid_bits) - 1;
    
    // "Default" place is encoded as size = 0
    if (id != 0)
    {
        // Find the lowest bit that's set to 1
        unsigned int bits = __builtin_ctz(id);
        place.size = (1 << bits);           // That bit is the size
        place.pid  = (id - place.size) / 2; // Clear bit and shift to get base
    }
    return place;
}

FID Processor::UnpackFID(Integer id) const
{
    // Unpack the FID: <Capability:N, LFID:F, PID:P>
    FID fid;
    fid.pid        =  (PID)((id >>               0) & ((1ULL << m_bits.pid_bits) - 1));
    fid.lfid       = (LFID)((id >> m_bits.pid_bits) & ((1ULL << m_bits.fid_bits) - 1));
    fid.capability = id >> (m_bits.pid_bits + m_bits.fid_bits);
    return fid;
}

Integer Processor::PackFID(const FID& fid) const
{
    // Construct the FID: <Capability:N, LFID:F, PID:P>
    return (fid.capability << (m_bits.pid_bits + m_bits.fid_bits)) | (fid.lfid << m_bits.pid_bits) | fid.pid;
}

}
