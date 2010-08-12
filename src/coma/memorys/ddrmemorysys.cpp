#include "ddrmemorysys.h"

using namespace MemSim;

namespace MemSim{

bool DDRChannel::ScheduleRequest(ST_request* req)
{
    assert(req != NULL);
    __address_t addr = req->getlineaddress();
    bool bwrite=false;

    if (req->type == MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA)
        bwrite = true;
    else if (req->type == MemoryState::REQUEST_ACQUIRE_TOKEN_DATA)
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


void DDRChannel::FunRead(ST_request* req)
{
    // always fetch the whole line size
    char * tempdata = (char*)malloc(g_nCacheLineSize);
    m_pMemoryDataContainer.Read(req->getlineaddress(), tempdata, g_nCacheLineSize);

    // JXXX JONY ?? should update the memory data if it's read exclusive?
    // update
    if ((req->type == REQUEST_ACQUIRE_TOKEN_DATA) && (req->tokenrequested == CacheState::GetTotalTokenNum()))
    {
        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH;i++)
        {
            unsigned int maskhigh = i/8;
            unsigned int masklow = i%8;

            char testchar = 1 << masklow;

            if ((req->bitmask[maskhigh]&testchar) != 0)
            {
                
                memcpy(&tempdata[i*CACHE_REQUEST_ALIGNMENT], &req->data[i*CACHE_REQUEST_ALIGNMENT], CACHE_REQUEST_ALIGNMENT);
            }

        }
    }

    memcpy(req->data, tempdata, g_nCacheLineSize);

    if ((req->type == REQUEST_ACQUIRE_TOKEN_DATA) && (req->tokenrequested < CacheState::GetTotalTokenNum()))
    {
        req->offset = 0;
        req->nsize = g_nCacheLineSize;

    }

    req->Conform2BitVecFormat();


    free(tempdata);

    if (req->bprocessed)
    {
        assert(!req->dataavailable);

        req->dataavailable = true;
    }
    else
	     assert (req->type == REQUEST_ACQUIRE_TOKEN_DATA);
    {
        // if (req->tokenacquired > 0)
        // no data but sometoken, two situations, 
        // 1. already got all the token, but nodata when in directory, so go down here
        // 2. already returned to initiator once, but no data acquired. 
        req->tokenacquired = (req->tokenacquired > 0)?req->tokenacquired:CacheState::GetTotalTokenNum();
        req->dataavailable = true;
        req->bpriority = true;
    }
}

void DDRChannel::FunWrite(ST_request* req)
{
    assert (req->nsize <= g_nCacheLineSize);
    assert(req->type == REQUEST_DISSEMINATE_TOKEN_DATA);
    assert(req->nsize == g_nCacheLineSize);

    m_pMemoryDataContainer.Write(req->getlineaddress(), req->data, req->nsize); // 32 bit alert
}


void DDRChannel::ProcessRequest(ST_request *req)
{
    MemoryState::REQUEST reqtype = req->type;

    // handle request
    switch (reqtype)
    {
    case MemoryState::REQUEST_ACQUIRE_TOKEN_DATA:
        FunRead(req);
        break;
    case MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA:
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
        ST_request* req_incoming = m_lstReq.front();

        if (ScheduleRequest(req_incoming))
        {
            m_lstReq.pop_front();
        }
    }

    // Advance pipeline 
    unsigned int eventid;
    ST_request* req = m_pMSP->AdvancePipeline(eventid);

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

        if (req->type == MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA)
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
    ST_request* req_incoming = NULL;
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
    ST_request* req = m_channel.GetOutputRequest();
    if (req != NULL)
    {
        // send reply transaction
        channel_fifo_slave.push(req);
        m_channel.PopOutputRequest();
    }
}

}
