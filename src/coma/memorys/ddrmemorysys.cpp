#include "ddrmemorysys.h"

using namespace MemSim;

namespace MemSim{
extern unsigned int g_uMemoryAccessesL;
extern unsigned int g_uMemoryAccessesS;



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
#if defined(TEMP_BASIC_DEBUG) && ((TEMP_BASIC_DEBUG & TEMP_BASIC_DEBUG_MM) == TEMP_BASIC_DEBUG_MM)
    cout << "memory container [r] " << hex << (unsigned int)m_pMemoryDataContainer << " @" << req->getlineaddress() << " # " << req->nsize << dec << endl;
#endif

#ifdef SIMULATE_DATA_TRANSACTION
    // always fetch the whole line size
    unsigned int linesize = g_nCacheLineSize;
    char * tempdata = (char*)malloc(g_nCacheLineSize);
    m_pMemoryDataContainer->Fetch(req->getlineaddress(), linesize, tempdata);

    // JXXX JONY ?? should update the memory data if it's read exclusive?
    // update
#ifdef TOKEN_COHERENCE
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

    memcpy(req->data, tempdata, linesize);


#endif

#ifndef TOKEN_COHERENCE
    if (req->type != REQUEST_REMOTE_READ_EXCLUSIVE)
#else
    if ((req->type == REQUEST_ACQUIRE_TOKEN_DATA) && (req->tokenrequested < CacheState::GetTotalTokenNum()))
#endif
    {
        req->offset = 0;
        req->nsize = linesize;

    }

    req->Conform2BitVecFormat();


    free(tempdata);

#endif

#ifndef TOKEN_COHERENCE
	if (req->type == REQUEST_REMOTE_READ_SHARED)
		req->type = REQUEST_REMOTE_SHARED_READ_REPLY;
	else if (req->type == REQUEST_REMOTE_READ_EXCLUSIVE)
		req->type = REQUEST_REMOTE_EXCLUSIVE_READ_REPLY;
	else
		assert(false);
#else
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    if (req->bprocessed)
    {
        assert(!req->dataavailable);

        req->dataavailable = true;
    }
    else
#endif

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
#endif

    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
        clog << LOGN_HEAD_OUTPUT << "read done address@" << FMT_ADDR(req->getreqaddress()) << ", " << FMT_DTA(req->data[0]) << " returned." << endl;
        print_request(req);
    LOG_VERBOSE_END
}

void DDRChannel::FunWrite(ST_request* req)
{
  assert (req->nsize <= g_nCacheLineSize);

#ifdef SIMULATE_DATA_TRANSACTION
#if defined(TEMP_BASIC_DEBUG) && ((TEMP_BASIC_DEBUG & TEMP_BASIC_DEBUG_MM) == TEMP_BASIC_DEBUG_MM)
    cout << "memory container [w] " << hex << (unsigned int)m_pMemoryDataContainer << dec << "{" << req->getlineaddress() << "," << req->getreqaddress() << "}" << endl;
#endif

#ifdef TOKEN_COHERENCE
    assert(req->type == REQUEST_DISSEMINATE_TOKEN_DATA);
#else
    assert(req->type == REQUEST_WRITE_BACK);
#endif

    assert(req->nsize == g_nCacheLineSize);

    //m_pMemoryDataContainer->Update(req->getreqaddress(), req->nsize*sizeof(UINT32), (char*)req->data);     // 32 bit alert
    m_pMemoryDataContainer->Update(req->getlineaddress(), req->nsize, (char*)req->data);     // 32 bit alert

    //m_pMemoryDataContainer->Verify(req->getreqaddress(), req->nsize*sizeof(UINT32), (char*)req->data);     // 32 bit alert
#endif

// JONYXXX
//    req->type = MemoryState::REQUEST_WRITE_REPLY_X;

LOG_VERBOSE_BEGIN(VERBOSE_STATE)
    clog << LOGN_HEAD_OUTPUT << "write done address " << FMT_ADDR(req->getreqaddress()) << " with " << FMT_DTA(req->data[0]) << endl;
LOG_VERBOSE_END
}


void DDRChannel::ProcessRequest(ST_request *req)
{
    MemoryState::REQUEST reqtype = req->type;

LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOGN_HEAD_OUTPUT << "process request " << endl;
            print_request(req);
LOG_VERBOSE_END

    // handle request
    switch (reqtype)
    {
#ifndef TOKEN_COHERENCE
    case MemoryState::REQUEST_READ:
    case MemoryState::REQUEST_READ_REDIRECT:
    case MemoryState::REQUEST_REMOTE_READ_SHARED:
    case MemoryState::REQUEST_REMOTE_READ_EXCLUSIVE:
#else
    case MemoryState::REQUEST_ACQUIRE_TOKEN_DATA:
#endif
        g_uMemoryAccessesL++;
        FunRead(req);
        break;
#ifndef TOKEN_COHERENCE
    case MemoryState::REQUEST_WRITE:
    case MemoryState::REQUEST_WRITE_REDIRECT:
    case MemoryState::REQUEST_WRITE_BACK:
#else
    case MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA:
#endif
        g_uMemoryAccessesS++;

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
            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOGN_HEAD_OUTPUT << "write back request terminated" << endl;
            LOG_VERBOSE_END

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


void DDRMemorySys::SendRequests()
{
    for (unsigned int i=0;i<m_nOpChannels;i++)
    {
        ST_request* req = m_pChannels[i]->GetOutputRequest();

        if (req != NULL)
        {
            // send reply transaction
            if (!channel_fifo_slave.nb_write(req))
            {
    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOGN_HEAD_OUTPUT << "feedback transaction sent failed, wait for the next cycle" << endl;
    LOG_VERBOSE_END
                continue;
            }

            m_pChannels[i]->PopOutputRequest();
        }
    }
}

void DDRMemorySys::Behavior()
{
    // check incoming request
    ST_request* req_incoming = (m_pfifoReqIn->num_available_fast() <= 0)?NULL:(m_pfifoReqIn->read());

    if (req_incoming != NULL)
    {
        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOGN_HEAD_OUTPUT << "request in " << FMT_ADDR(req_incoming->getreqaddress()) << endl;
            clog << "\t"; print_request(req_incoming);
        LOG_VERBOSE_END

        // dispatch them to the appropriate channel
        InsertRequest(req_incoming);

    }

    // execute cycle
    for (unsigned int i=0;i<m_nOpChannels;i++)
    {
        m_pChannels[i]->ExecuteCycle();
    }

    // get the reply request. and send them over network interface
    SendRequests();

    // collect finished requests and dispatch them.

}

}


