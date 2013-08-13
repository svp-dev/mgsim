#ifndef PERFCOUNTERS_H
#define PERFCOUNTERS_H

#include "IOMatchUnit.h"

class Config;

namespace Simulator
{

class DRISC;

namespace drisc
{

class PerfCounters : public drisc::MMIOComponent
{
    class Helpers;
    friend class Simulator::DRISC;

    std::vector<Integer (*)(DRISC&, LFID)> m_counters;
    uint64_t                  m_nCycleSampleOps; // nr of samplings of the cycle counter by program
    uint64_t                  m_nOtherSampleOps; // nr of samplings of other counters

public:

    size_t GetSize() const;

    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);

    PerfCounters(DRISC& parent, Config& config);

    ~PerfCounters() {}
};

}
}

#endif
