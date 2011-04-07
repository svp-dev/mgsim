#ifndef COUNTERS_H
#define COUNTERS_H

#include "MMIO.h"

namespace Simulator
{

class Processor;

class PerfCounters : public MMIOComponent
{
public:
    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid);
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid) { return FAILED; };
    
    PerfCounters(MMIOInterface& parent);

    ~PerfCounters() {}
};


}

#endif
