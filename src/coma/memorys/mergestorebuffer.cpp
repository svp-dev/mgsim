#include "mergestorebuffer.h"

using namespace MemSim;

    /////////////////////////////////////////////////////////////////
    // load and write function
    // load will always load a whole line, 
    // sometime might need the current line to serve part of the line since the msb line might be imcomplete
    //virtual bool LoadBufferWithLine(cache_line_t* line, __address_t address, char* data, ST_request* req)
    //{
    
    //}

    // write to the buffer
    bool MergeStoreBuffer::WriteBuffer(ST_request* req)
    {
        __address_t address = req->getlineaddress();
        int index;
        cache_line_t* line;
        if (m_fabLines.FindBufferItem(address, index, line))
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

        LOG_VERBOSE_BEGIN(VERBOSE_STATE)
            clog << LOGN_HEAD_OUTPUT << "Write to the buffer " << hex << req->getreqaddress() << endl;
        LOG_VERBOSE_END

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

        int index;
        cache_line_t* line;
        if (!m_fabLines.FindBufferItem(addrline, index, line))
        {
            return false;
        }

        //unsigned int maskoffset = req->offset/CACHE_REQUEST_ALIGNMENT;
        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH;i++)
        {
            unsigned int maskhigh = i/8;
            unsigned int masklow = i%8;
            char masknow = 1 << masklow;
            if (line->bitmask[maskhigh] == masknow)
            {
                memcpy(&req->data[i*CACHE_REQUEST_ALIGNMENT], &line->data[i*CACHE_REQUEST_ALIGNMENT], CACHE_REQUEST_ALIGNMENT);
            }
            else if (linecache->bitmask[maskhigh] == masknow)
            {
                memcpy(&req->data[i*CACHE_REQUEST_ALIGNMENT], &linecache->data[i*CACHE_REQUEST_ALIGNMENT], CACHE_REQUEST_ALIGNMENT);
            }
            else
            {
                // lock the line 
                line->SetLineLock(true);
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
        int index;
        cache_line_t* line;
        address = address >> g_nCacheLineWidth << g_nCacheLineWidth;
        if (m_fabLines.FindBufferItem(address, index, line))
        {
            return true;
        }

        return false;
    }

    int MergeStoreBuffer::AddressSlotPosition(__address_t address)
    {
        int index;
        cache_line_t* line;
        address = address >> g_nCacheLineWidth << g_nCacheLineWidth;
        if (m_fabLines.FindBufferItem(address, index, line))
        {
            return index;
        }
    
        return -1;
    }

    bool MergeStoreBuffer::IsSlotLocked(__address_t address, ST_request* req)
    {
        int index;
        cache_line_t* line;
        address = address >> g_nCacheLineWidth << g_nCacheLineWidth;
        if (m_fabLines.FindBufferItem(address, index, line))
        {
            if (line->IsLineLocked(req))
                return true;
            else 
                return false;
        }

        return false;
    }

    void MergeStoreBuffer::UpdateMergedRequest(__address_t address, cache_line_t* linecache, ST_request* reqpas)
    {
        assert(reqpas != NULL);

        int index;
        cache_line_t* line;
        __address_t addrline = address >> g_nCacheLineWidth << g_nCacheLineWidth;
        if (!m_fabLines.FindBufferItem(addrline, index, line))
        {
            assert(false);
        }
/*
        unsigned int offset = 0xffff;
        // find start position
        for (int i=0;i<CACHE_BIT_MASK_WIDTH;i++)
        {
            unsigned int maskhigh = i / 8;
            unsigned int masklow = i % 8;

            if ((line->bitmask[maskhigh] & (1 << masklow)) == 0)
                continue;
            else
            {
                offset = i*CACHE_REQUEST_ALIGNMENT;
                break;
            }
        }

        assert(offset != 0xffff);
*/

        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH;i++)
        {
            unsigned int maskhigh = i/8;
            unsigned int masklow = i % 8;
            char testchar = 1 << masklow;

            if ((reqpas->bitmask[maskhigh]&testchar) != 0)
            {
                m_ppMergedRequest[index]->bitmask[maskhigh] |= testchar;
                line->bitmask[maskhigh] |= testchar;
                memcpy(&m_ppMergedRequest[index]->data[i*CACHE_REQUEST_ALIGNMENT], &reqpas->data[i*CACHE_REQUEST_ALIGNMENT], CACHE_REQUEST_ALIGNMENT);
                memcpy(&line->data[i*CACHE_REQUEST_ALIGNMENT], &reqpas->data[i*CACHE_REQUEST_ALIGNMENT], CACHE_REQUEST_ALIGNMENT);
            }
        }

        m_ppMergedRequest[index]->type = MemoryState::REQUEST_ACQUIRE_TOKEN_DATA;           // NEED TO CHANGE, JONY XXXXXXX
        m_ppMergedRequest[index]->tokenacquired = 0;
        m_ppMergedRequest[index]->tokenrequested = CacheState::GetTotalTokenNum();
        m_ppMergedRequest[index]->addresspre = address >> g_nCacheLineWidth;
        m_ppMergedRequest[index]->offset = 0;
        m_ppMergedRequest[index]->nsize = 4;            /// NEED TO CHANGE  JONY XXXXXXX
        m_ppMergedRequest[index]->bmerged = true;
        //memcpy(m_ppMergedRequest[index]->data, line->data, g_nCacheLineSize);

/*
        if (reqpas->getlineaddress() == 0x112000)
        {
            for (unsigned int i=0;i<g_nCacheLineSize;i++)
            {
                cout << hex << setw(2) << setfill('0') << (unsigned int)(unsigned char)m_ppMergedRequest[index]->data[i] << " ";
            }
            cout << endl;
        }
*/

        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOGN_HEAD_OUTPUT << "Merged Request @ " << m_ppMergedRequest[index]->getlineaddress() << " updated." << endl;
            print_request(m_ppMergedRequest[index]);
        LOG_VERBOSE_END

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

        reqrev->ref = (unsigned long*)pvec;
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
        cache_line_t* line;
        address = address >> g_nCacheLineWidth << g_nCacheLineWidth;
        if (m_fabLines.FindBufferItem(address, index, line))
        {
            // clean slot with data
            m_fabLines.RemoveBufferItem(address);

            // clean the merged request
            m_ppMergedRequest[index]->type = MemoryState::REQUEST_NONE;

            m_ppMergedRequest[index]->curinitiator = 0;

            for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
            {
                m_ppMergedRequest[index]->bitmask[i] = 0;
            }

            // clean the request queue
            m_pvecRequestQueue[index]->clear();


            if (m_fabLines.FindBufferItem(address, index, line))
                    assert(false);

            return true;
        }

        return false;
    }

    // return false : no allocation
    // return  treu : allocated
    // index always return the current/allocated index, -1 represent total failure
    bool MergeStoreBuffer::AllocateSlot(__address_t address, int& index)
    {
        address = address >> g_nCacheLineWidth << g_nCacheLineWidth;
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

        line->SetLineLock(false);

        for (unsigned int k = 0;k<CACHE_BIT_MASK_WIDTH/8;k++)
        {
            line->bitmask[k] = 0;
            m_ppMergedRequest[index]->bitmask[k] = 0;
        }

        line->data = m_ppData[ind];

        // initialize again
        for (unsigned int i=0;i<g_nCacheLineSize;i++)
            line->data[i] = (char)0;

        if (!m_fabLines.InsertItem2Buffer(address, line, 1))
            assert(false);

        // verify...
        assert(AddressSlotPosition(address) == ind);

        free(line);

        m_ppMergedRequest[index]->curinitiator = 0;
        ADD_INITIATOR_NODE(m_ppMergedRequest[index], this);

        return true;
    }

    // req should be only write request
    bool MergeStoreBuffer::UpdateSlot(ST_request* req)
    {
        assert(req->type == MemoryState::REQUEST_WRITE);
        __address_t address = req->getlineaddress();

        cache_line_t* line;
        int index;
        if (m_fabLines.FindBufferItem(address, index, line))
        {
            // update data and bit-mask

            assert(line->data == m_ppData[index]);


//            unsigned int offset = req->offset;
//
//            char maskchar[CACHE_BIT_MASK_WIDTH/8];
//            for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
//                maskchar[i] = 0;
//
//            unsigned int maskoffset = offset/CACHE_REQUEST_ALIGNMENT;
//            for (unsigned int i = 0;i<req->nsize;i+=CACHE_REQUEST_ALIGNMENT)
//            {
//                unsigned int currentpos = i + offset;
//
//                memcpy(&line->data[currentpos], &req->data[currentpos], CACHE_REQUEST_ALIGNMENT);
//
//                unsigned int maskhigh = maskoffset / 8;
//                unsigned int masklow = maskoffset % 8;
//
//                maskchar[maskhigh] |= (1 << masklow);
//
//                maskoffset++;
//            }
//
//            for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
//                line->bitmask[i] |= maskchar[i];

            for (unsigned int i=0;i<g_nCacheLineSize;i+=CACHE_REQUEST_ALIGNMENT)
            {
                unsigned int maskhigh = i / (8*CACHE_REQUEST_ALIGNMENT);
                unsigned int masklow = i % (8*CACHE_REQUEST_ALIGNMENT);

                char testchar = 1 << masklow;

                if ((req->bitmask[maskhigh]&testchar) != 0)
                {
                    line->bitmask[maskhigh] |= testchar;
                    memcpy(&line->data[i], &req->data[i], CACHE_REQUEST_ALIGNMENT);
                }
            }

            // update request queue
            m_pvecRequestQueue[index]->push_back(req);
        }
        else
        {
            assert(false);
        }

        return true;
    }

