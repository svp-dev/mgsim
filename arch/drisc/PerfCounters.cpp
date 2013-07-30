#include "DRISC.h"
#include <sim/config.h>

#include <sys/time.h>
#include <ctime>

namespace Simulator
{

    class DRISC::PerfCounters::Helpers
{
template<typename F>
static void apply_place(DRISC& cpu, LFID fid, const F& fun)
{
    auto placeSize  = cpu.m_familyTable[fid].placeSize;
    auto placeStart = (cpu.m_pid / placeSize) * placeSize;
    auto placeEnd   = placeStart + placeSize;
    for (auto i = placeStart; i < placeEnd; ++i)
        fun(i);
}

public:
    // Simulation master cycle counter
    static Integer master_cycles(DRISC& cpu, LFID ) { return cpu.GetKernel()->GetCycleNo(); }

    // Executed instructions on all cores in place
    static Integer exec_ops(DRISC& cpu, LFID fid) {
        Integer ops = 0;
        apply_place(cpu, fid, [&](size_t i) { ops += cpu.m_grid[i]->GetPipeline().GetOp(); });
        return ops;
    }

    // Issued FP instructions on all cores in place
    static Integer issued_flops(DRISC& cpu, LFID fid) {
        Integer flops = 0;
        apply_place(cpu, fid, [&](size_t i) { flops += cpu.m_grid[i]->GetPipeline().GetFlop(); });
        return flops;
    }

    // Completed loads on all cores in place
    static Integer completed_loads(DRISC& cpu, LFID fid) {
        uint64_t n = 0, dummy;
        apply_place(cpu, fid, [&](size_t i) { cpu.m_grid[i]->GetPipeline().CollectMemOpStatistics(n, dummy, dummy, dummy); });
        return n;
    }

    // Completed stores on all cores in place
    static Integer completed_stores(DRISC& cpu, LFID fid) {
        uint64_t n = 0, dummy;
        apply_place(cpu, fid, [&](size_t i) { cpu.m_grid[i]->GetPipeline().CollectMemOpStatistics(dummy, n, dummy, dummy); });
        return n;
    }

    // Loaded bytes on all cores in place
    static Integer loaded_bytes(DRISC& cpu, LFID fid) {
        uint64_t n = 0, dummy;
        apply_place(cpu, fid, [&](size_t i) { cpu.m_grid[i]->GetPipeline().CollectMemOpStatistics(dummy, dummy, n, dummy); });
        return n;
    }

    // Stored bytes on all cores in place
    static Integer stored_bytes(DRISC& cpu, LFID fid) {
        uint64_t n = 0, dummy;
        apply_place(cpu, fid, [&](size_t i) { cpu.m_grid[i]->GetPipeline().CollectMemOpStatistics(dummy, dummy, dummy, n); });
        return n;
    }

    // Line load requests issued from L1 to L2
    static Integer lines_loaded(DRISC& cpu, LFID) {
        uint64_t n = 0, dummy;
        cpu.m_memory->GetMemoryStatistics(n, dummy, dummy, dummy, dummy, dummy);
        return n;
    }

    // Line store requests issued from L1 to L2
    static Integer lines_stored(DRISC& cpu, LFID) {
        uint64_t n = 0, dummy;
        cpu.m_memory->GetMemoryStatistics(dummy, n, dummy, dummy, dummy, dummy);
        return n;
    }

    // Place size
    static Integer place_size(DRISC& cpu, LFID fid) { return cpu.m_familyTable[fid].placeSize; }

    // Total cumulative allocated thread slots
    static Integer allocated_threads(DRISC& cpu, LFID fid) {
        Integer n = 0;
        apply_place(cpu, fid, [&](size_t i) { n += cpu.m_grid[i]->GetTotalThreadsAllocated(); });
        return n;
    }

    // Total cumulative allocated family slots
    static Integer allocated_families(DRISC& cpu, LFID fid) {
        Integer n = 0;
        apply_place(cpu, fid, [&](size_t i) { n += cpu.m_grid[i]->GetTotalFamiliesAllocated(); });
        return n;
    }

    // Total cumulative exclusive allocate queue size
    static Integer allocated_xfamilies(DRISC& cpu, LFID fid) {
        Integer n = 0;
        apply_place(cpu, fid, [&](size_t i) { n += cpu.m_grid[i]->GetTotalAllocateExQueueSize(); });
        return n;
    }

    // Unix time in seconds
    static Integer unix_time(DRISC&, LFID) { return time(0); }

    // Packed wall clock date
    // bits 0-4: day in month
    // bits 5-8: month in year
    // bits 9-31: year from 1900
    static Integer packed_date(DRISC&, LFID) {
        time_t c = time(0);
        struct tm * tm = gmtime(&c);
        return (Integer)tm->tm_mday |
            ((Integer)tm->tm_mon << 5) |
            ((Integer)tm->tm_year << 9);
    }

    // Packed wall clock time
    // bits 0-14 = microseconds / 2^5  (topmost 15 bits)
    // bits 15-20 = seconds
    // bits 21-26 = minutes
    // bits 27-31 = hours
    static Integer packed_time(DRISC&, LFID) {
        struct timeval tv;
        gettimeofday(&tv, 0);
        struct tm * tm = gmtime(&tv.tv_sec);

        // get topmost 15 bits of precision of the usec field
        // usec is 0-999999; so it has 20 bits of value
        Integer usec = (tv.tv_usec >> (20-15)) & 0x7fff;
        return  usec | (tm->tm_sec << 15) | (tm->tm_min << 21) | (tm->tm_hour << 27);
    }

    // Line load requests issued from chip to outside
    static Integer extlines_loaded(DRISC& cpu, LFID) {
        uint64_t n = 0, dummy;
        cpu.m_memory->GetMemoryStatistics(dummy, dummy, dummy, dummy, n, dummy);
        return n;
    }

    // Line store requests issued from chip to outside
    static Integer extlines_stored(DRISC& cpu, LFID) {
        uint64_t n = 0, dummy;
        cpu.m_memory->GetMemoryStatistics(dummy, dummy, dummy, dummy, dummy, n);
        return n;
    }

    // Number of created threads
    static Integer created_threads(DRISC& cpu, LFID fid) {
        Integer n = 0;
        apply_place(cpu, fid, [&](size_t i) { n += cpu.m_grid[i]->GetTotalThreadsCreated(); });
        return n;
    }

    // Total cumulative created family slots
    static Integer created_families(DRISC& cpu, LFID fid) {
        Integer n = 0;
        apply_place(cpu, fid, [&](size_t i) { n += cpu.m_grid[i]->GetTotalFamiliesCreated(); });
        return n;
    }

    // Core cycle counter
    static Integer core_cycles(DRISC& cpu, LFID) { return cpu.GetCycleNo(); }

};

DRISC::PerfCounters::PerfCounters(DRISC& parent, Config& config)
    : DRISC::MMIOComponent("perfcounters", parent, parent.GetClock()),
      m_counters(),
      m_nCycleSampleOps(0),
      m_nOtherSampleOps(0)
{
    m_counters.push_back(&Helpers::master_cycles);
    m_counters.push_back(&Helpers::exec_ops);
    m_counters.push_back(&Helpers::issued_flops);
    m_counters.push_back(&Helpers::completed_loads);
    m_counters.push_back(&Helpers::completed_stores);
    m_counters.push_back(&Helpers::loaded_bytes);
    m_counters.push_back(&Helpers::stored_bytes);
    m_counters.push_back(&Helpers::lines_loaded);
    m_counters.push_back(&Helpers::lines_stored);
    m_counters.push_back(&Helpers::place_size);
    m_counters.push_back(&Helpers::allocated_threads);
    m_counters.push_back(&Helpers::allocated_families);
    m_counters.push_back(&Helpers::allocated_xfamilies);
    m_counters.push_back(&Helpers::unix_time);
    m_counters.push_back(&Helpers::packed_date);
    m_counters.push_back(&Helpers::packed_time);
    m_counters.push_back(&Helpers::extlines_loaded);
    m_counters.push_back(&Helpers::extlines_stored);
    m_counters.push_back(&Helpers::created_threads);
    m_counters.push_back(&Helpers::created_families);
    m_counters.push_back(&Helpers::core_cycles);

    parent.WriteASR(ASR_NUM_PERFCOUNTERS, m_counters.size());
    parent.WriteASR(ASR_PERFCOUNTERS_BASE, config.getValue<MemAddr>(*this, "MMIO_BaseAddr"));

    RegisterSampleVariableInObject(m_nCycleSampleOps, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nOtherSampleOps, SVC_CUMULATIVE);
}


size_t DRISC::PerfCounters::GetSize() const { return m_counters.size() * sizeof(Integer);  }

Result DRISC::PerfCounters::Write(MemAddr /*address*/, const void * /*data*/, MemSize /*size*/, LFID /*fid*/, TID /*tid*/)
{
    UNREACHABLE;
}

Result DRISC::PerfCounters::Read(MemAddr address, void *data, MemSize size, LFID fid, TID tid, const RegAddr& /*writeback*/)
{
    if (size != sizeof(Integer) || address % sizeof(Integer) != 0 || address / sizeof(Integer) >= m_counters.size())
    {
        throw exceptf<InvalidArgumentException>(*this, "Invalid read to performance counter address by F%u/T%u: %#016llx/%u",
                                                (unsigned)fid, (unsigned)tid, (unsigned long long)address, (unsigned)size);
    }
    address /= sizeof(Integer);

    DRISC& cpu = *static_cast<DRISC*>(GetParent());

    Integer value = m_counters[address](cpu, fid);

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

}
