#include "ddrmemorysys.h"

namespace MemSim
{

bool DDRChannel::ScheduleRequest(Message* req)
{
    assert(req != NULL);
    MemAddr addr = req->address;
    bool bwrite=false;

    if (req->type == Message::DISSEMINATE_TOKEN_DATA)
        bwrite = true;
    else if (req->type == Message::ACQUIRE_TOKEN_DATA)
        bwrite = false;
    else assert(false);

    if ((AddrRowID(addr) != m_nLastRow) || (AddrRankID(addr) != m_nLastRank))
    {
        if (m_pMSP->ScheduleNext(req, m_tRP+m_tBurst, bwrite?m_tWL:m_tRL))
        {
            m_nLastRank = AddrRankID(addr);
            m_nLastBank = AddrBankID(addr);
            m_nLastRow = AddrRowID(addr);
            m_bLastWrite = bwrite;

            return true;
        }
        else
            return false;
    }
    else
    {
        if (m_pMSP->ScheduleNext(req, m_tBurst, bwrite?m_tWL:m_tRL))
        {
            m_nLastRank = AddrRankID(addr);
            m_nLastBank = AddrBankID(addr);
            m_nLastRow = AddrRowID(addr);
            m_bLastWrite = bwrite;

            return true;
        }
        else
            return false;
    }
}


void DDRChannel::FunRead(Message* req)
{
    // Always fetch the whole line size
    char tempdata[MAX_MEMORY_OPERATION_SIZE];
    m_pMemoryDataContainer.Read(req->address, tempdata, g_nCacheLineSize);

    // JXXX JONY ?? should update the memory data if it's read exclusive?
    // update
    if ((req->type == Message::ACQUIRE_TOKEN_DATA) && (req->tokenrequested == CacheState::GetTotalTokenNum()))
    {
        for (unsigned int i = 0; i < MAX_MEMORY_OPERATION_SIZE; i++)
        {
            if (req->bitmask[i])
            {
                tempdata[i] = req->data[i];
            }
        }
    }

    memcpy(req->data, tempdata, g_nCacheLineSize);

    if (req->type == Message::ACQUIRE_TOKEN_DATA && req->tokenrequested < CacheState::GetTotalTokenNum())
    {
        // Align to cache line
        req->address = (req->address / g_nCacheLineSize) * g_nCacheLineSize;
        req->size    = g_nCacheLineSize;
    }

    unsigned int offset = req->address % g_nCacheLineSize;
    for (unsigned int i = 0; i < g_nCacheLineSize; i++)
    {
        req->bitmask[i] = (i >= offset && i < offset + req->size);
    }

    if (req->bprocessed)
    {
        assert(!req->dataavailable);
        req->dataavailable = true;
    }
    else
	     assert (req->type == Message::ACQUIRE_TOKEN_DATA);
    {
        // if (req->tokenacquired > 0)
        // no data but sometoken, two situations, 
        // 1. already got all the token, but nodata when in directory, so go down here
        // 2. already returned to initiator once, but no data acquired. 
        req->tokenacquired = (req->tokenacquired > 0) ? req->tokenacquired : CacheState::GetTotalTokenNum();
        req->dataavailable = true;
        req->bpriority = true;
    }
}

void DDRChannel::FunWrite(Message* req)
{
    assert(req->type == Message::DISSEMINATE_TOKEN_DATA);
    assert(req->size == g_nCacheLineSize);

    m_pMemoryDataContainer.Write(req->address, req->data, req->size);
}


void DDRChannel::ProcessRequest(Message *req)
{
    switch (req->type)
    {
    case Message::ACQUIRE_TOKEN_DATA:
        FunRead(req);
        break;
    case Message::DISSEMINATE_TOKEN_DATA:
        FunWrite(req);
        break;
    default:
        break;
    }


}

void DDRChannel::ExecuteCycle()
{
    // schedule incoming request if any
    if (!m_lstReq.empty())
    {
        // get request in the queue
        Message* req_incoming = m_lstReq.front();

        if (ScheduleRequest(req_incoming))
        {
            m_lstReq.pop_front();
        }
    }

    // Advance pipeline 
    unsigned int eventid;
    Message* req = m_pMSP->AdvancePipeline(eventid);

    // row close
    if (eventid == 1)
    {
        m_nLastRank = DDR_NA_ID;
        m_nLastBank = DDR_NA_ID;
        m_nLastRow = DDR_NA_ID;
        m_bLastWrite = false;
    }

    // send when not NULL
    if (req != NULL)
    {
        // Process request
        ProcessRequest(req);

        if (req->type == Message::DISSEMINATE_TOKEN_DATA)
        {
            // terminate and delete the eviction request
            delete req;
        }
        else
        {
            // push to output queue
            m_lstReqOut.push_back(req);
        }
    }
}

void DDRMemorySys::Behavior()
{
    // check incoming request
    Message* req_incoming = NULL;
    if (!m_pfifoReqIn.empty())
    {
        req_incoming = m_pfifoReqIn.front();
        m_pfifoReqIn.pop();
    }
    
    if (req_incoming != NULL)
    {
        // dispatch it to the channel
        m_channel.InsertRequest(req_incoming);
    }

    // execute cycle
    m_channel.ExecuteCycle();

    // get the reply request. and send them over network interface
    Message* req = m_channel.GetOutputRequest();
    if (req != NULL)
    {
        // send reply transaction
        channel_fifo_slave.push(req);
        m_channel.PopOutputRequest();
    }
}

}
