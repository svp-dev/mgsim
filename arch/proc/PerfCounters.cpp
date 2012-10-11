#include "Processor.h"
#include <sim/config.h>

#include <sys/time.h>
#include <ctime>

namespace Simulator
{

#define NUM_COUNTERS 18

size_t Processor::PerfCounters::GetSize() const { return NUM_COUNTERS * sizeof(Integer);  }

Result Processor::PerfCounters::Write(MemAddr /*address*/, const void * /*data*/, MemSize /*size*/, LFID /*fid*/, TID /*tid*/)
{
    return FAILED;
}

Result Processor::PerfCounters::Read(MemAddr address, void *data, MemSize size, LFID fid, TID tid, const RegAddr& /*writeback*/)
{
    if (size != sizeof(Integer) || address % sizeof(Integer) != 0 || address / sizeof(Integer) >= NUM_COUNTERS)
    {
        throw exceptf<InvalidArgumentException>(*this, "Invalid read to performance counter address by F%u/T%u: %#016llx/%u",
                                                (unsigned)fid, (unsigned)tid, (unsigned long long)address, (unsigned)size);
    }
    address /= sizeof(Integer);

    Processor& cpu = *static_cast<Processor*>(GetParent());

    const size_t placeSize  = cpu.m_familyTable[fid].placeSize;
    const size_t placeStart = (cpu.m_pid / placeSize) * placeSize;
    const size_t placeEnd   = placeStart + placeSize;
    Integer value;

    switch (address)
    {
    case 0:
    {
        // Return the number of elapsed cycles
        value = (Integer)GetKernel()->GetCycleNo();
    }
    break;
    case 1:
    {
        // Return the number of executed instructions on all cores
        Integer ops = 0;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            ops += cpu.m_grid[i]->GetPipeline().GetOp();
        }
        value = ops;
    }
    break;
    case 2:
    {
        // Return the number of issued FP instructions on all cores
        Integer flops = 0;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            flops += cpu.m_grid[i]->GetPipeline().GetFlop();
        }
        value = flops;
    }
    break;
    case 3:
    {
        // Return the number of completed loads on all cores
        uint64_t n = 0, dummy;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            cpu.m_grid[i]->GetPipeline().CollectMemOpStatistics(n, dummy, dummy, dummy);
        }
        value = (Integer)n;
    }
    break;
    case 4:
    {
        // Return the number of completed stores on all cores
        uint64_t n = 0, dummy;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            cpu.m_grid[i]->GetPipeline().CollectMemOpStatistics(dummy, n, dummy, dummy);
        }
        value = (Integer)n;
    }
    break;
    case 5:
    {
        // Return the number of successfully loaded bytes on all cores
        uint64_t n = 0, dummy;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            cpu.m_grid[i]->GetPipeline().CollectMemOpStatistics(dummy, dummy, n, dummy);
        }
        value = (Integer)n;
    }
    break;
    case 6:
    {
        // Return the number of successfully stored bytes on all cores
        uint64_t n = 0, dummy;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            cpu.m_grid[i]->GetPipeline().CollectMemOpStatistics(dummy, dummy, dummy, n);
        }
        value = (Integer)n;
    }
    break;
    case 7:
    {
        // Return the number of memory loads overall from L1 to L2 (cache lines)
        uint64_t n = 0, dummy;
        cpu.m_memory.GetMemoryStatistics(n, dummy, dummy, dummy, dummy, dummy);
        value = (Integer)n;
    }
    break;
    case 8:
    {
        // Return the number of memory stores overall from L1 to L2 (cache lines)
        uint64_t n = 0, dummy;
        cpu.m_memory.GetMemoryStatistics(dummy, n, dummy, dummy, dummy, dummy);
        value = (Integer)n;
    }
    break;
    case 9:
    {
        value = (Integer)placeSize;
    }
    break;
    case 10:
    {
        // Return the total cumulative allocated thread slots
        Integer alloc = 0;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            alloc += cpu.m_grid[i]->GetTotalThreadsAllocated();
        }
        value = alloc;
    }
    break;
    case 11:
    {
        // Return the total cumulative allocated thread slots
        Integer alloc = 0;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            alloc += cpu.m_grid[i]->GetTotalFamiliesAllocated();
        }
        value = alloc;
    }
    break;
    case 12:
    {
        // Return the total cumulative exclusive allocate queue size
        Integer alloc = 0;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            alloc += cpu.m_grid[i]->GetTotalAllocateExQueueSize();
        }
        value = alloc;
    }
    break;
    case 13:
    {
        // Return the Unix time
        value = (Integer)time(0);
    }
    break;
    case 14:
    {
        // Return the local date as a packed struct
        // bits 0-4: day in month
        // bits 5-8: month in year
        // bits 9-31: year from 1900
        time_t c = time(0);
        struct tm * tm = gmtime(&c);
        value = (Integer)tm->tm_mday |
            ((Integer)tm->tm_mon << 5) |
            ((Integer)tm->tm_year << 9);
    }
    break;
    case 15:
    {
        // Return the local time as a packed struct
        // bits 0-14 = microseconds / 2^5  (topmost 15 bits)
        // bits 15-20 = seconds
        // bits 21-26 = minutes
        // bits 27-31 = hours
        struct timeval tv;
        gettimeofday(&tv, 0);
        struct tm * tm = gmtime(&tv.tv_sec);

        // get topmost 15 bits of precision of the usec field
        // usec is 0-999999; so it has 20 bits of value
        Integer usec = (tv.tv_usec >> (20-15)) & 0x7fff;
        value = usec | (tm->tm_sec << 15) | (tm->tm_min << 21) | (tm->tm_hour << 27);
    }
    break;
    case 16:
    {
        // Return the number of memory loads overall from external memory (cache lines)
        uint64_t n = 0, dummy;
        cpu.m_memory.GetMemoryStatistics(dummy, dummy, dummy, dummy, n, dummy);
        value = (Integer)n;
    }
    break;
    case 17:
    {
        // Return the number of memory stores overall to external memory (cache lines)
        uint64_t n = 0, dummy;
        cpu.m_memory.GetMemoryStatistics(dummy, dummy, dummy, dummy, dummy, n);
        value = (Integer)n;
    }
    break;
    case 18:
    {
        // Return the number of created families
        Integer tc = 0;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            tc += cpu.m_grid[i]->GetTotalFamiliesCreated();
        }
        value = tc;
    }
    break;
    case 19:
    {
        // Return the number of created threads
        Integer fc = 0;
        for (size_t i = placeStart; i < placeEnd; ++i)
        {
            fc += cpu.m_grid[i]->GetTotalFamiliesCreated();
        }
        value = fc;
    }
    default:
        value = 0;
    }

    DebugIOWrite("Read counter %u by F%u/T%u: %#016llx (%llu)",
                 (unsigned)address, (unsigned)fid, (unsigned)tid,
                 (unsigned long long)value, (unsigned long long)value);

    COMMIT{
        if (address == 0)
        {
            ++m_nCycleSampleOps;
        }
        else
        {
            ++m_nOtherSampleOps;
        }
    }

    SerializeRegister(RT_INTEGER, value, data, sizeof(Integer));

    return SUCCESS;
}

Processor::PerfCounters::PerfCounters(Processor& parent, Config& config)
    : Processor::MMIOComponent("perfcounters", parent, parent.GetClock()),
      m_nCycleSampleOps(0),
      m_nOtherSampleOps(0)
{
    parent.WriteASR(ASR_NUM_PERFCOUNTERS, NUM_COUNTERS);
    parent.WriteASR(ASR_PERFCOUNTERS_BASE, config.getValue<MemAddr>(*this, "MMIO_BaseAddr"));

    RegisterSampleVariableInObject(m_nCycleSampleOps, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nOtherSampleOps, SVC_CUMULATIVE);
}

}
