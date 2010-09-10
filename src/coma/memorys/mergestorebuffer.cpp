#include "mergestorebuffer.h"

namespace MemSim
{

// write to the buffer
bool MergeStoreBuffer::WriteBuffer(ST_request* req)
{
    __address_t address = req->getlineaddress();
    if (m_fabLines.FindBufferItem(address) != NULL)
    {
        // update the current merged line
        if (IsSlotLocked(address, req))
        {

            return false;
        }

        // proceed update
        UpdateSlot(req);
    }
    else
    {
        // allocate a new line for the incoming request
        int retfree = IsFreeSlotAvailable();
        if (retfree != -1)
        {
            // proceed allocating
            int index;
            AllocateSlot(address, index);

            // Update Slot
            UpdateSlot(req);
        }
        else
        {

            return false;
        }
    }

    // Update Request to return type
    req->type = MemoryState::REQUEST_WRITE_REPLY;

    //UpdateMergedRequest(address, NULL, NULL);
    UpdateMergedRequest(address, NULL, req);

    return true;
}

// load from the buffer
//virtual bool LoadBuffer(__address_t address, char* data, ST_request* req)
bool MergeStoreBuffer::LoadBuffer(ST_request* req, cache_line_t *linecache)
{
    //__address_t address = req->getreqaddress();
    __address_t addrline = req->getlineaddress();

    assert(req->nsize == g_nCacheLineSize);

    cache_line_t* line = m_fabLines.FindBufferItem(addrline);
    if (line == NULL)
    {
        return false;
    }

    for (unsigned int i = 0; i < CACHE_BIT_MASK_WIDTH; i++)
    {
        if (line->bitmask[i])
        {
            req->data[i] = line->data[i];
        }
        else if (linecache->bitmask[i])
        {
            req->data[i] = linecache->data[i];
        }
        else
        {
            // lock the line 
            line->llock = true;
            return false;
        }
    }

    req->type = MemoryState::REQUEST_READ_REPLY;
    req->dataavailable = true;

    return true;
}

// return -1 : failed, nothing is free
// return  n : succeed, n == index
int MergeStoreBuffer::IsFreeSlotAvailable()
{
    return m_fabLines.IsEmptySlotAvailable();
}

bool MergeStoreBuffer::IsAddressPresent(__address_t address)
{
    address = (address / g_nCacheLineSize) * g_nCacheLineSize;
    return m_fabLines.FindBufferItem(address) != NULL;
}

int MergeStoreBuffer::AddressSlotPosition(__address_t address)
{
    int index = -1;
    address = (address / g_nCacheLineSize) * g_nCacheLineSize;
    m_fabLines.FindBufferItem(address, index);
    return index;
}

bool MergeStoreBuffer::IsSlotLocked(__address_t address, ST_request* req)
{
    address = (address / g_nCacheLineSize) * g_nCacheLineSize;
    cache_line_t* line = m_fabLines.FindBufferItem(address);
    return line != NULL && line->llock;
}

void MergeStoreBuffer::UpdateMergedRequest(__address_t address, cache_line_t* linecache, ST_request* reqpas)
{
    assert(reqpas != NULL);

    int index = -1;
    __address_t addrline = (address / g_nCacheLineSize) * g_nCacheLineSize;
    cache_line_t* line = m_fabLines.FindBufferItem(addrline, index);
    assert(line != NULL);

    for (unsigned int i = 0; i < CACHE_BIT_MASK_WIDTH; i++)
    {
        if (reqpas->bitmask[i])
        {
            m_ppMergedRequest[index]->bitmask[i] = true;
            line->bitmask[i] = true;
            m_ppMergedRequest[index]->data[i] = reqpas->data[i];
            line->data[i] = reqpas->data[i];
        }
    }

    m_ppMergedRequest[index]->type = MemoryState::REQUEST_ACQUIRE_TOKEN_DATA;           // NEED TO CHANGE, JONY XXXXXXX
    m_ppMergedRequest[index]->tokenacquired = 0;
    m_ppMergedRequest[index]->tokenrequested = CacheState::GetTotalTokenNum();
    m_ppMergedRequest[index]->addresspre = address / g_nCacheLineSize;
    m_ppMergedRequest[index]->offset = 0;
    m_ppMergedRequest[index]->nsize = 4;            /// NEED TO CHANGE  JONY XXXXXXX
    m_ppMergedRequest[index]->bmerged = true;
}

void MergeStoreBuffer::DuplicateRequestQueue(__address_t address, ST_request* reqrev)
{
    int index = AddressSlotPosition(address);
    // duplicate
    vector<ST_request*>* pvec = new vector<ST_request*>();
    for (unsigned int i=0;i<m_pvecRequestQueue[index]->size();i++)
    {
        pvec->push_back((*m_pvecRequestQueue[index])[i]);
    }
    reqrev->msbcopy = pvec;
}

ST_request* MergeStoreBuffer::GetMergedRequest(__address_t address)
{
    int index = AddressSlotPosition(address);
    if (index == -1)
        return NULL;

    return m_ppMergedRequest[index];
}

vector<ST_request*>* MergeStoreBuffer::GetQueuedRequestVector(__address_t address)
{
    int index = AddressSlotPosition(address);
    if (index == -1)
        return NULL;

    return m_pvecRequestQueue[index];
}

bool MergeStoreBuffer::CleanSlot(__address_t address)
{
    int index;
    address = (address / g_nCacheLineSize) * g_nCacheLineSize;
    if (m_fabLines.FindBufferItem(address, index) != NULL)
    {
        // clean slot with data
        m_fabLines.RemoveBufferItem(address);

        // clean the merged request
        m_ppMergedRequest[index]->type = MemoryState::REQUEST_NONE;

        std::fill(m_ppMergedRequest[index]->bitmask, m_ppMergedRequest[index]->bitmask + CACHE_BIT_MASK_WIDTH, false);

        // clean the request queue
        m_pvecRequestQueue[index]->clear();

        assert(!m_fabLines.FindBufferItem(address));
        return true;
    }
    return false;
}

// return false : no allocation
// return  treu : allocated
// index always return the current/allocated index, -1 represent total failure
bool MergeStoreBuffer::AllocateSlot(__address_t address, int& index)
{
    address = (address / g_nCacheLineSize) * g_nCacheLineSize;
    int ind = AddressSlotPosition(address);

    if (ind != -1)
    {
        index = ind;
        return false;
    }

    ind = IsFreeSlotAvailable();
    index = ind;

    if (ind == -1)
        return false;

    cache_line_t* line = (cache_line_t*)malloc(sizeof(cache_line_t));

    line->llock = false;

    std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, false);
    std::fill(m_ppMergedRequest[index]->bitmask, m_ppMergedRequest[index]->bitmask + CACHE_BIT_MASK_WIDTH, false);

    line->data = m_ppData[ind];

    // initialize again
    for (unsigned int i=0;i<g_nCacheLineSize;i++)
        line->data[i] = (char)0;

    m_fabLines.InsertItem2Buffer(address, line, 1);

    // verify...
    assert(AddressSlotPosition(address) == ind);

    free(line);

    return true;
}

// req should be only write request
bool MergeStoreBuffer::UpdateSlot(ST_request* req)
{
    assert(req->type == MemoryState::REQUEST_WRITE);
    __address_t address = req->getlineaddress();

    int index =0;
    cache_line_t* line = m_fabLines.FindBufferItem(address, index);
    assert(line != NULL);

    {
        // update data and bit-mask
        assert(line->data == m_ppData[index]);

        bool maskchar[CACHE_BIT_MASK_WIDTH];
        std::fill(maskchar, maskchar + CACHE_BIT_MASK_WIDTH, false);

        for (unsigned int i = req->offset; i < req->offset + req->nsize; i++)
        {
            line->data[i] = req->data[i];
            maskchar[i] = true;
        }

        for (unsigned int i = 0; i <CACHE_BIT_MASK_WIDTH; i++)
            line->bitmask[i] = maskchar[i];

        for (unsigned int i=0; i < g_nCacheLineSize; i++)
        {
            if (req->bitmask[i])
            {
                line->bitmask[i] = true;
                line->data[i] = req->data[i];
            }
        }

        // update request queue
        m_pvecRequestQueue[index]->push_back(req);
    }

    return true;
}

}
