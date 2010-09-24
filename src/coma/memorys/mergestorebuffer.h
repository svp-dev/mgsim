#ifndef ZLCOMA_MERGESTOREBUFFER_H
#define ZLCOMA_MERGESTOREBUFFER_H

#include "Cache.h"

namespace Simulator
{

class ZLCOMA::Cache::MergeStoreBuffer
{
    struct Entry;

    std::vector<Entry> m_entries;
    size_t m_lineSize;

public:
    MergeStoreBuffer(unsigned int size, size_t lineSize);
    ~MergeStoreBuffer();

    bool LoadBuffer(MemAddr address, MemData& data, const Line& linecache);
    bool WriteBuffer(MemAddr address, const MemData& data, const WriteAck& ack);
    bool CleanSlot(MemAddr address);
    bool IsSlotLocked(MemAddr address) const;    
    bool DumpMergedLine(MemAddr address, char* data, bool* bitmask, std::vector<WriteAck>& ack_queue);
    
private:
          Entry* GetEmptyLine();
          Entry* FindBufferItem(MemAddr address);
    const Entry* FindBufferItem(MemAddr address) const;
};

}

#endif
