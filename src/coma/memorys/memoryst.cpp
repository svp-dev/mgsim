#include "memoryst.h"
#include "../simlink/memstat.h"

using namespace MemSim;


void MemoryST::ProcessRequest()
{
    ST_request* req = m_pReqCur;
    MemoryState::REQUEST reqtype = req->type;

#ifdef MEM_MODULE_STATISTICS
    m_nStatReqNo++;
#endif

LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "process request " << endl;
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

#ifdef MEM_MODULE_STATISTICS
        if (m_pmmStatReqStore != NULL)
        {
            *m_pStatReqStore = *req;
        }
#endif

        FunWrite(req);
        break;
    default:
        break;
    }

#ifndef TOKEN_COHERENCE
    if (reqtype != MemoryState::REQUEST_WRITE_BACK)
#else
    if (reqtype != MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA)
#endif
    {
        // send reply transaction
        if (!channel_fifo_slave.nb_write(req))
        {
LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "feedback transaction sent failed, wait for the next cycle" << endl;
LOG_VERBOSE_END

            // retry next cycle
            m_nState = STATE_RETRY_BUS;
            return;
        }

LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "feedback transaction sent" << endl;
LOG_VERBOSE_END
    }
    else
    {
LOG_VERBOSE_BEGIN(VERBOSE_STATE)
        clog << LOG_HEAD_OUTPUT << "write back request detected" << endl;
LOG_VERBOSE_END

        // terminate and delete the eviction request
        delete req;
    }

    // change state to free
    m_nState = STATE_PROCESSING;

    m_pReqCur = NULL;
}

void MemoryST::ResendRequest()
{
    // send reply transaction
    if (!channel_fifo_slave.nb_write(m_pReqCur))
    {
        // retry next cycle
        m_nState = STATE_RETRY_BUS;
        return;
    }

LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
    clog << LOG_HEAD_OUTPUT << "feedback transaction sent" << endl;
LOG_VERBOSE_END

    // change state to free
    m_nState = STATE_PROCESSING;

    m_pReqCur = NULL;
}

//void MemoryST::Behavior()
//{
////    printf("state: %d\n", m_nState);
//    switch (m_nState)
//    {
//    // the memory module is free now
//    case STATE_IDLE:
// 
//        // check whether there are any request left
//        if (m_pfifoReqIn->num_available_fast() <= 0)
//        {
//            // no request available 
//            break;
//        }
//
//        m_pReqCur = m_pfifoReqIn->read();
//
//LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
//        clog << LOG_HEAD_OUTPUT << "request in " << FMT_ADDR(m_pReqCur->getreqaddress()) << endl;
//        clog << "\t"; print_request(m_pReqCur);
//LOG_VERBOSE_END
//
//        // change state
//        m_nState = STATE_WAIT;
//        m_nWaitCount = 0;
//        break;
//
//    // waiting for delay time 
//    case STATE_WAIT:
//        // increase the counter
//        m_nWaitCount++;
//
////LOG_VERBOSE_BEGIN(VERBOSE_MOST)
////        clog << LOG_HEAD_OUTPUT << m_nWaitCount << " " << m_nDelay << endl;
////LOG_VERBOSE_END
//
//        // change state if the delay is over
//        if (m_nWaitCount >= m_nDelay)
//            m_nState = STATE_PROCESSING;
//        break;
//
//    // processing request
//    case STATE_PROCESSING:
//        // delay time is up
//        // start processing request
//        ProcessRequest();
//        break;
//
//    // since bus might be busy(out of request buffer), the request has to be resent sometime
//    case STATE_RETRY_BUS:
//        ResendRequest();
//        break;
//    // default branch should not be reached
//    default:
//        break;
//    }
//}

void MemoryST::Behavior()
{
    ST_request* req_incoming;

#ifdef MEM_MODULE_STATISTICS
    if (m_pmmStatReqStore != NULL)
        m_pStatReqStore->clear();
#endif

    switch(m_nState)
    {
    case STATE_PROCESSING:
        // get the request from FIFO if any
        req_incoming = (m_pfifoReqIn->num_available_fast() <= 0)?NULL:(m_pfifoReqIn->read()); 

        if (req_incoming != NULL)
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "request in " << FMT_ADDR(req_incoming->getreqaddress()) << endl;
                clog << "\t"; print_request(req_incoming);
            LOG_VERBOSE_END
        }

        if (!m_pPipeLine->getlst().empty())
        {
            // get request from pipeline
            m_pReqCur = m_pPipeLine->shift(req_incoming);
        }
        else
        {
            m_pReqCur = req_incoming;
        }


#ifdef MEM_MODULE_STATISTICS
        // statistics
        m_pStatCurReq = m_pReqCur;
#endif

        // if nothing to process, just skip this round
        if (m_pReqCur == NULL)
            break;

        // process request
        ProcessRequest();

        break;

    case STATE_RETRY_BUS:
      if ( (!m_pPipeLine->getlst().empty())&&(m_pPipeLine->top() == NULL) )    // write request can be processed, since only write and output required
        {
            req_incoming = (m_pfifoReqIn->num_available_fast() <= 0)?NULL:(m_pfifoReqIn->read()); 

            if (req_incoming != NULL)
            {
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "request in " << FMT_ADDR(req_incoming->getreqaddress()) << endl;
                    clog << "\t"; print_request(req_incoming);
                LOG_VERBOSE_END
            }

            // only shifting, since no request in the queue top
            m_pPipeLine->shift(req_incoming);
        }

#ifdef MEM_MODULE_STATISTICS
        // statistics
        m_pStatCurReq = NULL;
#endif

        // try to resend the request
        ResendRequest();

        break;

    default:
      abort();
        break;
    }
}

void MemoryST::FunRead(ST_request* req)
{
#if defined(TEMP_BASIC_DEBUG) && ((TEMP_BASIC_DEBUG & TEMP_BASIC_DEBUG_MM) == TEMP_BASIC_DEBUG_MM)
    cout << "memory container [r] " << hex << (unsigned int)m_pMemoryDataContainer << " @" << req->getlineaddress() << " # " << req->nsize << dec << endl;
#endif

#ifdef SIMULATE_DATA_TRANSACTION
    // always fetch the whole line size
    unsigned int linesize = g_nCacheLineSize;
    char * tempdata = (char*)malloc(g_nCacheLineSize);
    m_pMemoryDataContainer->Fetch(req->getlineaddress(), linesize, (char*)tempdata);

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
        clog << LOG_HEAD_OUTPUT << "read done address@" << FMT_ADDR(req->getreqaddress()) << ", " << FMT_DTA(req->data[0]) << " returned, have now totally " << req->tokenacquired << " tokens."  << endl;
    LOG_VERBOSE_END
}

void MemoryST::FunWrite(ST_request* req)
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
    clog << LOG_HEAD_OUTPUT << "write done address " << FMT_ADDR(req->getreqaddress()) << " with " << FMT_DTA(req->data[0]) << endl;
LOG_VERBOSE_END
}

__address_t MemoryST::StartAddress() const {return m_nStartAddress;}
__address_t MemoryST::EndAddress() const {return m_nEndAddress;}


#ifdef MEM_MODULE_STATISTICS
void MemoryST::InitializeStatistics(unsigned int components)
{
    switch(components)
    {
    case STAT_MEMORY_COMP_REQUEST_NO:
        if (m_pmmStatRequestNo == NULL)
            m_pmmStatRequestNo = new map<double, stat_stru_size_t>();
        break;

    case STAT_MEMORY_COMP_PROCESSING:
        if (m_pmmStatProcessing == NULL)
            m_pmmStatProcessing  = new map<double, stat_stru_request_t>();
        break;

    case STAT_MEMORY_COMP_STORE:
        if (m_pmmStatReqStore == NULL)
        {
            m_pmmStatReqStore = new map<double, stat_stru_request_t>();
            m_pStatReqStore = new ST_request();
            free(m_pStatReqStore->data);
        }
        break;

    case STAT_MEMORY_COMP_PIPELINE:
        if (m_pmmStatPipeline == NULL)
            m_pmmStatPipeline = new map<double, stat_stru_request_list_t>();
        break;

    case STAT_MEMORY_COMP_INCOMING_FIFO:
        if (m_pmmStatIncomingFIFO == NULL)
            m_pmmStatIncomingFIFO = new map<double, stat_stru_size_t>();
        break;


    case STAT_MEMORY_COMP_ALL:
        if (m_pmmStatRequestNo == NULL)
            m_pmmStatRequestNo = new map<double, stat_stru_size_t>();
        if (m_pmmStatProcessing == NULL)
            m_pmmStatProcessing  = new map<double, stat_stru_request_t>();
        if (m_pmmStatPipeline == NULL)
            m_pmmStatPipeline = new map<double, stat_stru_request_list_t>();
        if (m_pmmStatIncomingFIFO == NULL)
            m_pmmStatIncomingFIFO = new map<double, stat_stru_size_t>();
        break;

    default:
        // ignore
        break;
    }
}

void MemoryST::Statistics(STAT_LEVEL lev)
{
    if (m_pmmStatRequestNo != NULL)
    {
        m_pmmStatRequestNo->insert(pair<double,stat_stru_size_t>(sc_time_stamp().to_seconds(),m_nStatReqNo));
    }

    if (m_pmmStatProcessing != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        stat_stru_request_t req;
        if (m_pStatCurReq == NULL)
        {
            req.valid = false;
        }
        else
        {
            req.valid = true;
            req.address = m_pStatCurReq->getlineaddress();
            req.offset = m_pStatCurReq->offset;
            req.size = m_pStatCurReq->nsize;
            req.type = m_pStatCurReq->type;

            m_pmmStatProcessing->insert(pair<double,stat_stru_request_t>(sc_time_stamp().to_seconds(),req));
        }
    }

    if (m_pmmStatReqStore != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        stat_stru_request_t req;
        if (m_pStatReqStore->type == REQUEST_DISSEMINATE_TOKEN_DATA)
        {
            req.valid = true;
            req.address = m_pStatReqStore->getlineaddress();
            req.offset = m_pStatReqStore->offset;
            req.size = m_pStatReqStore->nsize;
            req.type = m_pStatReqStore->type;

            m_pmmStatReqStore->insert(pair<double,stat_stru_request_t>(sc_time_stamp().to_seconds(),req));
        }
    }

    if (m_pmmStatPipeline != NULL)
    {

    }

    if (m_pmmStatIncomingFIFO != NULL)
    {

    }
}

void MemoryST::DumpStatistics(ofstream& outfile, unsigned int component, unsigned int type)
{
    if (m_pmmStatRequestNo != NULL)
    {
        map<double, stat_stru_size_t>::iterator iter;

        for (iter=m_pmmStatRequestNo->begin();iter!=m_pmmStatRequestNo->end();iter++)
        {
            outfile << setw(10) << (*iter).first << "\t" << name() << "\t" << dec << (*iter).second << endl;
        }
    }

    if (m_pmmStatProcessing != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        for (iter=m_pmmStatProcessing->begin();iter!=m_pmmStatProcessing->end();iter++)
        {
            stat_stru_request_t req = (*iter).second;
            if (req.valid)
                outfile << setw(10) << (*iter).first << "\t" << dec << req.address << "\t" << dec << req.type << "\t" << req.size << endl;
        }
    }

    if (m_pmmStatReqStore != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        for (iter=m_pmmStatReqStore->begin();iter!=m_pmmStatReqStore->end();iter++)
        {
            stat_stru_request_t req = (*iter).second;
            if (req.valid)
                outfile << setw(10) << (*iter).first << "\t" << dec << req.address << "\t" << dec << req.type << "\t" << req.size << endl;
        }
    }

    if (m_pmmStatPipeline != NULL)
    {

    }

    if (m_pmmStatIncomingFIFO != NULL)
    {

    }    
}
#endif

