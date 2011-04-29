#ifndef PERFCOUNTERS_H
#define PERFCOUNTERS_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class PerfCounters : public MMIOComponent
{

    uint64_t                  m_nCycleSampleOps; // nr of samplings of the cycle counter by program
    uint64_t                  m_nOtherSampleOps; // nr of samplings of other counters

public:

    static const size_t numCounters;

    size_t GetSize() const;

    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid) { return FAILED; };
    
    PerfCounters(Processor& parent);

    ~PerfCounters() {}
};

#endif
