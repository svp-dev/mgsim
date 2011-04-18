#ifndef COUNTERS_H
#define COUNTERS_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class PerfCounters : public MMIOComponent
{
public:

    size_t GetSize() const;
    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid);
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid) { return FAILED; };
    
    PerfCounters(MMIOInterface& parent);

    ~PerfCounters() {}
};

#endif
