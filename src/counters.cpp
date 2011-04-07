#include <sys/time.h>
#include <ctime>
#include "counters.h"
#include "Processor.h"

namespace Simulator
{

Result PerfCounters::Read(MemAddr address, void *data, MemSize size, LFID fid, TID tid)
{
    if (size != sizeof(Integer))
        return FAILED;

    Processor& cpu = GetInterface().GetProcessor();

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

    case 7:
    {
        // Return the number of memory loads overall from L1 to L2 (cache lines)
        uint64_t n, dummy;
        cpu.m_memory.GetMemoryStatistics(n, dummy, dummy, dummy, dummy, dummy);
        value = (Integer)n;
    }

    case 8:
    {
        // Return the number of memory stores overall from L1 to L2 (cache lines)
        uint64_t n, dummy;
        cpu.m_memory.GetMemoryStatistics(dummy, n, dummy, dummy, dummy, dummy);
        value = (Integer)n;
    }

    case 9:
    {
        value = (Integer)placeSize;
    }

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

    case 13:
    {
        // Return the Unix time
        value = (Integer)time(0);
    }

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
        value = usec | (tm->tm_sec << 15) | (tm->tm_min << 21) | (tm->tm_hour << 27);
    }       
        
    case 16:
    {
        // Return the number of memory loads overall from external memory (cache lines)
        uint64_t n, dummy;
        cpu.m_memory.GetMemoryStatistics(dummy, dummy, dummy, dummy, n, dummy);
        value = (Integer)n;
    }

    case 17:
    {
        // Return the number of memory stores overall to external memory (cache lines)
        uint64_t n, dummy;
        cpu.m_memory.GetMemoryStatistics(dummy, dummy, dummy, dummy, dummy, n);
        value = (Integer)n;
    }

    default:
        value = 0;
    }

    SerializeRegister(RT_INTEGER, value, data, sizeof(Integer));

    return SUCCESS;
}

PerfCounters::PerfCounters(MMIOInterface& parent)
    : MMIOComponent("perfcounters", parent, parent.GetProcessor().GetClock())
{
}

}
