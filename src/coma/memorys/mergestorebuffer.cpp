#include "mergestorebuffer.h"
using namespace std;

namespace MemSim
{

struct MergeStoreBuffer::Entry
{
    bool                  valid;
    bool                  locked;
    MemAddr               tag;
    char                  data   [MAX_MEMORY_OPERATION_SIZE];
    bool                  bitmask[MAX_MEMORY_OPERATION_SIZE];
    std::vector<Message*> request_queue;
    Message               merged_request;
};

MergeStoreBuffer::MergeStoreBuffer(unsigned int size)
    : m_entries(size)
{
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        m_entries[i].valid = false;
        m_entries[i].merged_request.type = Message::NONE;
    }
}

MergeStoreBuffer::~MergeStoreBuffer()
{
}

// Write to the buffer
bool MergeStoreBuffer::WriteBuffer(Message* req)
{
    // Only write requests hould come here
    assert(req->type == Message::WRITE);

    Entry* line = FindBufferItem(req->address);
    if (line == NULL)
    {
        // Allocate a new line
        line = GetEmptyLine();
        if (line == NULL)
        {
            return false;
        }

        line->valid  = true;
        line->tag    = req->address / g_nCacheLineSize;
        line->locked = false;
        std::fill(line->bitmask, line->bitmask + g_nCacheLineSize, false);
        std::fill(line->data,    line->data    + g_nCacheLineSize, 0);
        std::fill(line->merged_request.bitmask, line->merged_request.bitmask + g_nCacheLineSize, false);
    }
    else if (line->locked)
    {
        // Line is locked
        return false;
    }
    
    // Merge request data into line and merged request
    for (unsigned int i = 0; i < g_nCacheLineSize; i++)
    {
        if (req->bitmask[i])
        {
            line->merged_request.bitmask[i] = line->bitmask[i] = true;
            line->merged_request.data[i]    = line->data[i]    = req->data[i];
        }
    }

    line->merged_request.type = Message::ACQUIRE_TOKEN_DATA;           // NEED TO CHANGE, JONY XXXXXXX
    line->merged_request.tokenacquired = 0;
    line->merged_request.tokenrequested = CacheState::GetTotalTokenNum();
    line->merged_request.address = (req->address / g_nCacheLineSize) * g_nCacheLineSize;
    line->merged_request.size = 4;            /// NEED TO CHANGE  JONY XXXXXXX
    line->merged_request.bmerged = true;

    line->request_queue.push_back(req);

    // Update Request to return type
    req->type = Message::WRITE_REPLY;

    return true;
}

// load from the buffer
bool MergeStoreBuffer::LoadBuffer(Message* req, const cache_line_t& linecache)
{
    assert(req->size == g_nCacheLineSize);

    Entry* line = FindBufferItem(req->address);
    if (line == NULL)
    {
        return false;
    }

    for (unsigned int i = 0; i < MAX_MEMORY_OPERATION_SIZE; i++)
    {
        if (line->bitmask[i])
        {
            req->data[i] = line->data[i];
        }
        else if (linecache.bitmask[i])
        {
            req->data[i] = linecache.data[i];
        }
        else
        {
            // lock the line 
            line->locked = true;
            return false;
        }
    }
    return true;
}

bool MergeStoreBuffer::IsAddressPresent(MemAddr address) const
{
    return FindBufferItem(address) != NULL;
}

bool MergeStoreBuffer::IsSlotLocked(MemAddr address) const
{
    const Entry* line = FindBufferItem(address);
    return line != NULL && line->locked;
}

MergeStoreBuffer::Entry* MergeStoreBuffer::FindBufferItem(MemAddr address)
{
    MemAddr tag = address / g_nCacheLineSize;
    for (unsigned int i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].valid && m_entries[i].tag == tag)
        {
            return &m_entries[i];
        }
    }
    return NULL;
}

const MergeStoreBuffer::Entry* MergeStoreBuffer::FindBufferItem(MemAddr address) const
{
    MemAddr tag = address / g_nCacheLineSize;
    for (unsigned int i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].valid && m_entries[i].tag == tag)
        {
            return &m_entries[i];
        }
    }
    return NULL;
}

MergeStoreBuffer::Entry* MergeStoreBuffer::GetEmptyLine()
{
    for (unsigned int i = 0 ; i < m_entries.size(); ++i)
    {
        if (!m_entries[i].valid)
        {
            return &m_entries[i];
        }
    }
    return NULL;
}

const Message& MergeStoreBuffer::GetMergedRequest(MemAddr address) const
{
    const Entry* line = FindBufferItem(address);
    assert(line != NULL);
    return line->merged_request;
}

const std::vector<Message*>& MergeStoreBuffer::GetQueuedRequestVector(MemAddr address) const
{
    const Entry* line = FindBufferItem(address);
    assert(line != NULL);
    return line->request_queue;
}

bool MergeStoreBuffer::CleanSlot(MemAddr address)
{
    Entry* line = FindBufferItem(address);
    if (line != NULL)
    {
        // Invalidate the line
        line->valid = false;

        // clean the merged request
        line->merged_request.type = Message::NONE;

        std::fill(line->merged_request.bitmask, line->merged_request.bitmask + MAX_MEMORY_OPERATION_SIZE, false);

        // clean the request queue
        line->request_queue.clear();
        return true;
    }
    return false;
}

}
