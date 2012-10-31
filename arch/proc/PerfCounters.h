#ifndef PERFCOUNTERS_H
#define PERFCOUNTERS_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class PerfCounters : public MMIOComponent
{
    class Helpers;
    friend class Processor;

    std::vector<Integer (*)(Processor&, LFID)> m_counters;
    uint64_t                  m_nCycleSampleOps; // nr of samplings of the cycle counter by program
    uint64_t                  m_nOtherSampleOps; // nr of samplings of other counters

public:

    size_t GetSize() const;

    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);

    PerfCounters(Processor& parent, Config& config);

    ~PerfCounters() {}
};

#endif
