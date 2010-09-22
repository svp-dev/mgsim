#ifndef _MERGESTOREBUFFER_H
#define _MERGESTOREBUFFER_H

#include "predef.h"

namespace MemSim
{

class MergeStoreBuffer
{
    struct Entry;

    std::vector<Entry> m_entries;

public:
    MergeStoreBuffer(unsigned int size);
    ~MergeStoreBuffer();

    bool LoadBuffer(Message* req, const cache_line_t& linecache);
    bool WriteBuffer(Message* req);
    bool CleanSlot(MemAddr address);
    bool IsAddressPresent(MemAddr address) const;
    bool IsSlotLocked(MemAddr address) const;
    
    const Message&               GetMergedRequest(MemAddr address) const;
    const std::vector<Message*>& GetQueuedRequestVector(MemAddr address) const;

private:
          Entry* GetEmptyLine();
          Entry* FindBufferItem(MemAddr address);
    const Entry* FindBufferItem(MemAddr address) const;
};

}

#endif
