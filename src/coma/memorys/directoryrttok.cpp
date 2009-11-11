#include "directoryrttok.h"
using namespace MemSim;

void DirectoryRTTOK::InitializeDirLines()
{
    // allocate sets
    m_pSet = (dir_set_t*)malloc(m_nSet * sizeof(dir_set_t));

    char auxinistate = m_srqSusReqQ.GetAuxInitialState();

    // allocate lines
    for (unsigned int i=0;i<m_nSet;i++)
    {
    	m_pSet[i].lines = (dir_line_t*)malloc(m_nAssociativity * sizeof(dir_line_t));
    	for (unsigned int j=0;j<m_nAssociativity;j++)
    	{
    		m_pSet[i].lines[j].state = DLS_INVALID;
            m_pSet[i].lines[j].tokencount = 0;
            m_pSet[i].lines[j].tokengroup = 0;
            m_pSet[i].lines[j].counter = 0;
            //m_pSet[i].lines[j].aux = AUXSTATE_NONE;
            m_pSet[i].lines[j].aux = auxinistate;
            m_pSet[i].lines[j].queuehead = SuspendedRequestQueue::EOQ;
            m_pSet[i].lines[j].queuetail = SuspendedRequestQueue::EOQ;
            m_pSet[i].lines[j].breserved = false;
            m_pSet[i].lines[j].priority = false;

            m_pSet[i].lines[j].setid = i;
        }
    }

    // initialize queue
    // suspended queue is already initialized from the constructor

}

void DirectoryRTTOK::ProcessRequestNET()
{
    ST_request* req = m_pReqCurNET;

    // any previously queued requests should not reach here // JNEWXXX
    assert(req->bqueued == false);

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "process request " << FMT_ADDR(req->getreqaddress()) << endl;
        clog << "\t"; print_request(req);
    LOG_VERBOSE_END


    switch(req->type)
    {
    case MemoryState::REQUEST_ACQUIRE_TOKEN:
        OnNETAcquireToken(req);
        break;
    case MemoryState::REQUEST_ACQUIRE_TOKEN_DATA:
        OnNETAcquireTokenData(req);
        break;
    case MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA:
        OnNETDisseminateTokenData(req);
        break;

    case MemoryState::REQUEST_READ:
    case MemoryState::REQUEST_WRITE:
    case MemoryState::REQUEST_READ_REPLY:
    case MemoryState::REQUEST_WRITE_REPLY:
    case MemoryState::REQUEST_INVALIDATE_BR:
        // error
        cerr << ERR_HEAD_OUTPUT << "===================================== ERROR =====================================" << endl;
        assert(false);
        break;

    default:
        assert(false);
        break;
    }
}


// send net request to memory
bool DirectoryRTTOK::SendRequestNETtoBUS(ST_request* req)
{
    // send request
    if (!port_bus->request(req))
    {
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
                clog << LOG_HEAD_OUTPUT << "net transaction to memory sent failed, bus busy" << endl;
        LOG_VERBOSE_END

        return false;
    }

    req->bqueued = false;

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "network request sent to memory bus" << endl;
        clog << "\t"; print_request(req);
    LOG_VERBOSE_END

    return true;
}

// send net to next node
bool DirectoryRTTOK::SendRequestNETtoNET(ST_request* req)
{
    // send request 
    if (!RequestNetwork(req))
    {
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "net transaction to node sent failed, buffer full" << endl;
        LOG_VERBOSE_END

        return false;
    }

    req->bqueued = false;

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "network request sent to next network node" << endl;
        clog << "\t"; print_request(req);
    LOG_VERBOSE_END

    return true;
}


void DirectoryRTTOK::SendRequestFromNET()
{
    // Make sure about the sequence 
    // process the requests from the incoming buffer first
    // then process the previously queued requests

    // 1. no prefetch only incoming, only one interface will be busy
    if (m_pPrefetchDeferredReq == NULL)
    {
        assert( (m_pReqCurNET2Net != NULL) || (m_pReqCurNET2Bus != NULL) );

        bool nofail = true;

        if (m_pReqCurNET2Net != NULL)
        {
            if (SendRequestNETtoNET(m_pReqCurNET2Net))
            {
                // only send from this interface, reset the state
//                LOG_VERBOSE_BEGIN(VERBOSE_MOST)
//                    clog << LOG_HEAD_OUTPUT << "(NET)Cycle Done, pipeline ready again" << endl;
//                LOG_VERBOSE_END
                m_pReqCurNET2Net = NULL;
            }
            else
            {
                nofail = false;
                m_nStateNET = STATE_NET_RETRY;
            }
        }

        if(m_pReqCurNET2Bus != NULL)
        {
            if (SendRequestNETtoBUS(m_pReqCurNET2Bus))
            {
                // only send from this interface, reset the state
//                LOG_VERBOSE_BEGIN(VERBOSE_MOST)
//                    clog << LOG_HEAD_OUTPUT << "(NET)Cycle Done, pipeline ready again" << endl;
//                LOG_VERBOSE_END
                m_pReqCurNET2Bus = NULL;
            }
            else
            {
                nofail = false;
                m_nStateNET = STATE_NET_RETRY;
            }
        }

        if (nofail)
        {
             LOG_VERBOSE_BEGIN(VERBOSE_MOST)
                 clog << LOG_HEAD_OUTPUT << "(NET)Cycle Done, pipeline ready again" << endl;
             LOG_VERBOSE_END
             m_nStateNET = STATE_NET_PROCESSING;
        }

//        if (m_pReqCurNET2Net != NULL)
//        {
//            print_request(m_pReqCurNET2Net);
//            if (m_pReqCurNET2Bus != NULL)
//                print_request(m_pReqCurNET2Bus);
//            assert(m_pReqCurNET2Bus == NULL);
//
//            if (SendRequestNETtoNET(m_pReqCurNET2Net))
//            {
//                // only send from this interface, reset the state
//                m_nStateNET = STATE_NET_PROCESSING;
//
//                LOG_VERBOSE_BEGIN(VERBOSE_MOST)
//                    clog << LOG_HEAD_OUTPUT << "(NET)Cycle Done, pipeline ready again" << endl;
//                LOG_VERBOSE_END
//            }
//            else
//            {
//                m_nStateNET = STATE_NET_RETRY;
//            }
//        }
//        else if(m_pReqCurNET2Bus != NULL)
//        {
//            assert(m_pReqCurNET2Net == NULL);
//
//            if (SendRequestNETtoBUS(m_pReqCurNET2Bus))
//            {
//                // only send from this interface, reset the state
//                m_nStateNET = STATE_NET_PROCESSING;
//
//                LOG_VERBOSE_BEGIN(VERBOSE_MOST)
//                    clog << LOG_HEAD_OUTPUT << "(NET)Cycle Done, pipeline ready again" << endl;
//                LOG_VERBOSE_END
//            }
//            else
//            {
//                m_nStateNET = STATE_NET_RETRY;
//            }
//        }
//        else    // shouldn't reach this function at all
//            assert(false);

        return;
    }


    // 2. prefetch exists, incoming may exist or not
    if (m_pPrefetchDeferredReq != NULL)
    {
        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "prefetched request " << endl;
            print_request(m_pPrefetchDeferredReq);
        LOG_VERBOSE_END

        // alert !!!
        assert(m_pPrefetchDeferredReq->bqueued);

        bool nofail = true;

        if (m_pReqCurNET2Net == NULL)   // try prefetch request when this interface is free for trying
        {
            assert(m_pPrefetchDeferredReq->bnewline == false);

            if (SendRequestNETtoNET(m_pPrefetchDeferredReq))
            {
                // pop deferred request from the queue
                ST_request* tmp = PopDeferredRequest();
                assert(tmp == m_pPrefetchDeferredReq);
//                m_pPrefetchDeferredReq = NULL;  // JXXXDEBUG
            }
        }
        else    // try to send the original requests instead of trying prefetch deferred requests
        {
//            assert(m_pReqCurNET2Bus == NULL);
            // no matter succeeded in sending prefetched request, the state can be reset when this request is sent
            if (SendRequestNETtoNET(m_pReqCurNET2Net))
            {
                //m_nStateNET = STATE_NET_PROCESSING; 
                m_pReqCurNET2Net = NULL;
            }
            else
            {
                nofail = false;
                m_nStateNET = STATE_NET_RETRY;
            }
        }

        if (m_pReqCurNET2Bus == NULL)   // try prefetch request when this interface is free for this cycle
        {
            assert(m_pPrefetchDeferredReq->bnewline == false);
        }
        else    // try to send the original requests instead of trying prefetch deferred requests
        {
 //           assert(m_pReqCurNET2Net == NULL);

            // no matter succeeded in sending prefetched request, the state can be reset when this request is sent 
            if (SendRequestNETtoBUS(m_pReqCurNET2Bus))
            {
                //m_nStateNET = STATE_NET_PROCESSING;
                m_pReqCurNET2Bus = NULL;
            }
            else
            {
                nofail = false;
                m_nStateNET = STATE_NET_RETRY;
            }
        }

        if (nofail)
        {
            LOG_VERBOSE_BEGIN(VERBOSE_MOST)
                clog << LOG_HEAD_OUTPUT << "(NET)Cycle Done, pipeline ready again" << endl;
            LOG_VERBOSE_END

            m_nStateNET = STATE_NET_PROCESSING;
        }
    }
}

void DirectoryRTTOK::BehaviorNET()
{
#ifndef NDEBUG
    DebugCheckCounterConsistency();
#endif

    ST_request* req_incoming=NULL;
    // prefetch deferred request
    m_pPrefetchDeferredReq = PrefetchDeferredRequest();

    switch (m_nStateNET)
    {
    case STATE_NET_PROCESSING:
        // the following buffer could only be set in the ProcessRequestNET function
        m_pReqCurNET2Net = NULL;
        m_pReqCurNET2Bus = NULL;

        // fetch request from incoming buffer or from the deferred queue
        req_incoming = FetchRequestNet();

        if (req_incoming != NULL)
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "request to get in pipeline : " << FMT_ADDR(req_incoming->getlineaddress()) << endl;
                clog << "\t"; print_request(req_incoming);
            LOG_VERBOSE_END
        }

        m_pReqCurNET = m_pPipelineNET->shift(req_incoming);

        // processrequest should never send requests, but only store them in net2bus or net2net buffer
        if (m_pReqCurNET != NULL)
            ProcessRequestNET();

        if (!m_lstReqNET2Net.empty())
        {
            m_pReqCurNET2Net = m_lstReqNET2Net.front();
            m_lstReqNET2Net.pop_front();
        }

        if ( (m_pPrefetchDeferredReq != NULL)||(m_pReqCurNET2Net != NULL)||(m_pReqCurNET2Bus != NULL) )
            SendRequestFromNET();

        break;

    // resend the request to bus
    case STATE_NET_RETRY:
        if (m_pPipelineNET->top() == NULL)
        {
            // fetch request from incoming buffer or from the deferred queue
            req_incoming = FetchRequestNet();

            if (req_incoming != NULL)
            {
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "request to get in pipeline : " << FMT_ADDR(req_incoming->getlineaddress()) << endl;
                    clog << "\t"; print_request(req_incoming);
                LOG_VERBOSE_END
            }
            
            // only shift
            m_pPipelineNET->shift(req_incoming);
        }

        assert( (m_pReqCurNET2Net != NULL)||(m_pReqCurNET2Bus != NULL) );
        SendRequestFromNET();
        break;

    default:
        assert(false);
        break;
    }
}

void DirectoryRTTOK::ProcessRequestBUS()
{
    ST_request* req = m_pReqCurBUS;

LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
    clog << LOG_HEAD_OUTPUT << "bus feedback is being processed@address:" << FMT_ADDR(req->getreqaddress())<< endl;
    print_request(req);
LOG_VERBOSE_END

    // locate the line
    __address_t address = req->getreqaddress();

    dir_line_t* line = LocateLine(address);

    // update info ?
    line->time = sc_time_stamp();

    if (!m_srqSusReqQ.ReactivateLine(line))
        assert(false);

    m_pReqCurBUS2Net = req;

    // try sending the request
    SendRequestBUStoNet();

    // maybe delete the finished req alert

    // send reply transaction
    // channel_fifo_slave.write(request);

    // send reply to network
    // RequestNetwork(request);
}

void DirectoryRTTOK::SendRequestBUStoNet()
{
    // try send the bus transaction to network
    if (!RequestNetwork(m_pReqCurBUS2Net))
    {
LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "bus feedback transaction to network sent failed, buffer full" << endl;
LOG_VERBOSE_END

        m_nStateBUS = STATE_BUS_RETRY_TO_NET;
        return;
    }

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "bus feedback transaction to network sent " << endl;
        print_request(m_pReqCurBUS2Net);
    LOG_VERBOSE_END

    // cycle done
    FinishCycleBUS();
}


// CHKS: optimization can be added, add the location or at least part of 
// the location (if the rest can be resolved from the request address)
// then the line can be appended without going through the pipeline stages
void DirectoryRTTOK::BehaviorBUS()
{
    ST_request* req_incoming=NULL;

    switch (m_nStateBUS)
    {
    case STATE_BUS_PROCESSING:
        // check whether any request available
        if (m_pfifoFeedback->num_available_fast() > 0)
        {
            if (!m_pfifoFeedback->nb_read(req_incoming))
            {
                cerr << ERR_HEAD_OUTPUT << "should not get failed in this nb_read request" << endl;
                assert(false);
                return;
            }

            LOG_VERBOSE_BEGIN(VERBOSE_MOST)
                clog << LOG_HEAD_OUTPUT << "memory feedback" << endl;
                clog << "\t"; print_request(req_incoming);
            LOG_VERBOSE_END
        }
        else
            req_incoming = NULL;

        // get request from the pipeline
        m_pReqCurBUS = m_pPipelineBUS->shift(req_incoming);

        // if nothing, then skip
        if (m_pReqCurBUS != NULL)
            ProcessRequestBUS();

        break;

    case STATE_BUS_RETRY_TO_NET:
        if (m_pPipelineBUS->top() == NULL)
        {
            // check whether any request available
            if (m_pfifoFeedback->num_available_fast() > 0)
            {
                if (!m_pfifoFeedback->nb_read(req_incoming))
                {
                    cerr << ERR_HEAD_OUTPUT << "should not get failed in this nb_read request" << endl;
                    assert(false);
                    return;
                }

                LOG_VERBOSE_BEGIN(VERBOSE_MOST)
                    clog << LOG_HEAD_OUTPUT << "memory feedback" << endl;
                    clog << "\t"; print_request(req_incoming);
                LOG_VERBOSE_END
            }
            else
                req_incoming = NULL;

            m_pPipelineBUS->shift(req_incoming);
        }

        SendRequestBUStoNet();
        break;

    default:
        break;
    }
}


//////////////////////////////////////////////////////////////////////////
// common handlers
bool DirectoryRTTOK::DirectForward(ST_request* req)
{
    if (m_nSplitDirBits != 0)
    {
        __address_t addr = req->getlineaddress();
        if (m_nSplitDirID == (unsigned int) (( addr >> (g_nCacheLineWidth+m_nSetBits) ) & ((1 << m_nSplitDirBits) - 1)))
            return false;
        else
            return true;
    }
    // other situation false
    return false;
}

bool DirectoryRTTOK::MayHandle(ST_request* req)
{

    return true;
}

bool DirectoryRTTOK::CanHandleNetRequest(ST_request* request)
{
    // 
    return true;

}


dir_line_t* DirectoryRTTOK::LocateLine(__address_t address)
{
    dir_line_t* line;
    unsigned int index = DirIndex(address);
    uint64 tag = DirTag(address);

    line = &(m_pSet[index].lines[0]);

    for (unsigned int i=0; i<m_nAssociativity;i++, line++)
    	if ((line->state != DLS_INVALID) && line->tag == tag)
    		return line;

    // miss
    return NULL;
}

// replace only invalid lines otherwise NULL
dir_line_t* DirectoryRTTOK::GetReplacementLine(__address_t address)
{
    dir_line_t *line;
    unsigned int index = DirIndex(address);

    line = &(m_pSet[index].lines[0]);
    for (unsigned int i=0; i<m_nAssociativity; i++, line++)
    {
    	// return the first found empty one
    	if (line->state == DLS_INVALID)
    		return line;
    }

    // maybe some debug printout

    return NULL;
}

//dir_line_t* DirectoryRTTOK::GetEmptyLine(__address_t address)
//{
//    dir_line_t* line = &(m_pSet[DirIndex(address)].lines[0]);
// 
//    for (unsigned int i=0; i<m_nAssociativity; i++, line++)
//    {
//        // return the first found empty one
//        if (line->state == DLS_INVALID)
//            return line;
//    }
//
//    return NULL;
//}

unsigned int DirectoryRTTOK::DirIndex(__address_t address)
{
    uint64 addr = address;
    return (unsigned int)( (addr>>s_nLineBits) & (m_nSet-1) );
}

uint64 DirectoryRTTOK::DirTag(__address_t address)
{
    uint64 addr = address;
    return (uint64)((addr) >> (s_nLineBits + m_nSetBits));
}


ST_request* DirectoryRTTOK::PickRequest(ST_request* req)
{
    assert(false);
    return NULL;
}

bool DirectoryRTTOK::FixDirLine(dir_line_t* line)
{
    bool berr = false;

    if ((line->tokencount < 0) || (line->tokencount > GetTotalTokenNum()))
    {
        cerr << ERR_HEAD_OUTPUT << "the tokencount is invalid" << endl;
        berr = true;
    }
    else
    {
        if (line->tokencount == GetTotalTokenNum()) // all the tokens are collected by the directory now
        {
            line->state = DLS_INVALID;
            line->tokencount = 0;
            line->tokengroup = 0;
            line->counter = 0;
            line->priority = false;
            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "all tokens are collected by the directory, line reset to empty" << endl;
            LOG_VERBOSE_END

        }
        // it's normal that token count is zero


        m_srqSusReqQ.NormalizeLineAux(line);
    }

    return !berr;
}

bool DirectoryRTTOK::CheckDirLines(dir_line_t* line)
{
    assert(false);
    bool berr = false;

    if ((line->tokencount < 0) || (line->tokencount > GetTotalTokenNum()))
    {
        // error
        cerr << ERR_HEAD_OUTPUT << "token couting error" << endl;
        berr = true;

        return berr;
    }

    if ((line->tokengroup < 0) || (line->tokencount > GetTotalTokenNum()))
    {
        // error
        cerr << ERR_HEAD_OUTPUT << "token couting error" << endl;
        berr = true;

        return berr;
    }

    // tokencount == 0 then ....
    // tokencount == total number then ....

    return !berr;
}

ST_request* DirectoryRTTOK::FetchRequestNet()
{
    //////////////////////////////////////////////////////////////////////////
    // network request always has priority
    // it can be handled in a different manner though by giving a slack 
    // maybe a check where to fetch function could help define where to fetch the line      // JXXX

    ST_request* req_incoming = NULL;

    //////////////////////////////////////////////////////////////////////////
    // fetch request from the input buffer if any
    if (m_fifoinNetwork.num_available_fast() > 0)
    {
        if (!m_fifoinNetwork.nb_read(req_incoming))
        {
            cerr << ERR_HEAD_OUTPUT << "should not get failed in this nb_read request" << endl;
            assert(false);
            return NULL;
        }

        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "fetch incoming request " << FMT_ADDR(req_incoming->getreqaddress()) << endl;
            clog << "\t"; print_request(req_incoming);
        LOG_VERBOSE_END


        if (DirectForward(req_incoming))
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "req " << FMT_ADDR(req_incoming->getreqaddress()) << " is forwarded directly." <<  endl;
            LOG_VERBOSE_END

            m_lstReqNET2Net.push_back(req_incoming);
            req_incoming = NULL;
        }
        // CHKS: possible optimization by inserting a small matching buffer to judge whether the request is at deferred lines
        // and if the maching succeed, the put it directly in to the queue and acquire another request from the active queue buffer
        // return req_incoming; // JNEWXXX
    }

    return req_incoming;
}


ST_request* DirectoryRTTOK::PrefetchDeferredRequest()
{

//    ST_request* activereq = m_pReqQueueBuffer[activeline->queuehead].request;
    ST_request* activereq = m_srqSusReqQ.GetTopActiveRequest();

    if (activereq == NULL)
        return NULL;

    LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "request prefetched " << FMT_ADDR(activereq->getreqaddress()) << endl;
        clog << "\t"; print_request(activereq);
    LOG_VERBOSE_END
    return activereq;
}

// popdeferredrequest will set the AUXSTATE to NONE, which could be incorrect, since sometimes it should be loading
// the auxstate should always be modified after calling pop
// when bRem == true, the top line in the line queue should be removed from the line queue
//ST_request* DirectoryRTTOK::PopDeferredRequest(bool bRem)
ST_request* DirectoryRTTOK::PopDeferredRequest()
{

    return m_srqSusReqQ.PopTopActiveRequest();
}

void DirectoryRTTOK::CleansingPipelineAndAppendNet(ST_request* req, dir_line_t* line, bool bappendreq)
{
    //////////////////////////////////////////////////////////////////////////
    // two passes:
    // first pass, will process the pipeline registers reversely considering only the previously queued requests
    // they will be removed from the pipeline, and pushed reversely from the front into the queue
    // second pass, will process the pipeline registers check all the previously non-queued requests
    // they will be removed from the pipeline, and pushed directly from the back as the order they are in the pipeline
    // IN THE END, reversely push the request into the line req queue

    __address_t lineaddr = req->getlineaddress();

    // temp request vector
    vector<ST_request*> vecreq;

        //////////////////////////////////////////////////////////////////////////
    // dump all the stuff to the vector
    // the checking is carried out from the back to front (tail to head)
    m_pPipelineNET->copy(vecreq);

    //////////////////////////////////////////////////////////////////////////
    // first pass

    // reversely check every register
    for (int i=vecreq.size()-1;i>=0;i--)
    {
        ST_request* reqtemp = vecreq[i];

        if ((reqtemp != NULL)&&(reqtemp->bqueued)&&(reqtemp->getlineaddress() == lineaddr))
        {
            // reversely push back from the front side to the global queue 
            // append request to line queue
            if (!m_srqSusReqQ.ReverselyAppendRequest2Line(reqtemp, line))
            {
                // this could happen when the queue is totally full
                assert(false);
            }

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "request " << FMT_ADDR(reqtemp->getreqaddress()) << "is REVERSELY appended to the queue" << endl;
                clog << "\t"; print_request(reqtemp);
            LOG_VERBOSE_END

            // fill the place for the register with NULL
            vecreq[i] = NULL;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // second pass
    for (unsigned int i=0;i<vecreq.size();i++)
    {
        ST_request* reqtemp = vecreq[i];

        if ((reqtemp != NULL)&&(!reqtemp->bqueued)&&(reqtemp->getlineaddress() == lineaddr))
        {
            // this is unlikely to happen for now
            // JXXX add some check here maybe 
            //assert(!DoesFIFOSeekSameLine(reqtemp));

            // append request to line queue
            if (!m_srqSusReqQ.AppendRequest2Line(reqtemp, line))
            {
                // this could happen when the queue is totally full
                assert(false);
            }

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "request " << FMT_ADDR(reqtemp->getreqaddress()) << "is appended to the queue" << endl;
                clog << "\t"; print_request(reqtemp);
            LOG_VERBOSE_END

            // fill the place for the regiser with NULL
            vecreq[i] = NULL;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // dump back to the pipeline 
    m_pPipelineNET->reset(vecreq);


    //////////////////////////////////////////////////////////////////////////
    // append the request reversely always, as it is absolutely the one should be put in front
    if (bappendreq)
    {
        if (!m_srqSusReqQ.ReverselyAppendRequest2Line(req, line))
            assert(false);
    }
}


void DirectoryRTTOK::FinishCycleNET()
{
    // finishing this pipeline cycle
    m_nStateNET = STATE_NET_PROCESSING;

    LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "(NET)Cycle Done, pipeline ready again" << endl;
    LOG_VERBOSE_END
}

void DirectoryRTTOK::FinishCycleBUS()
{
    // finishing this pipeline cycle
    m_nStateBUS = STATE_BUS_PROCESSING;

    LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "(BUS)Cycle Done, pipeline ready again" << endl;
    LOG_VERBOSE_END
}

bool DirectoryRTTOK::PreFill(__address_t lineaddr)
{
//    dir_line_t* line = LocateLine(lineaddr);
//
//    if (line == NULL)   // INVALID
//    {
//        // allocate and assign
//        line = GetReplacementLine(lineaddr);
//
//        // must be able to find a line
//        assert(line!=NULL);
//
//        assert(line->counter == 0);
//        line->state = DRSHARED;
//        line->tag = DirTag(lineaddr);
//        line->counter++;
//    }
//    else if (line->state == DRSHARED)
//    {
//        line->counter++;
//    }
//    else
//    {
//        // shouldn't happen, at least for now
//        assert(false);
//        return false;
//    }

    return true;
}

#ifdef MEM_MODULE_STATISTICS
void DirectoryRTTOK::InitializeStatistics(unsigned int components)
{
    switch(components)
    {

    case STAT_RTDIR_COMP_SET_TRACE:
        if (m_pmmStatSetTrace == NULL)
            m_pmmStatSetTrace = new map<double, stat_stru_set_t>();
            //m_pmmStatTest = new map<double, stat_stru_request_t>();
            //m_pmmStatTest = new map<double, stat_stru_request_list_t>();
        break;

    default:
        cout << "warning: specified statistics not matching with the cache" << endl;
        break;
    }
}

void DirectoryRTTOK::Statistics(STAT_LEVEL lev)
{
    if (m_pmmStatRequestNo != NULL)
    {
        m_pmmStatRequestNo->insert(pair<double,stat_stru_size_t>(sc_time_stamp().to_seconds(),m_nStatReqNo));
    }

    if (m_pmmStatProcessingNET != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        stat_stru_request_t req;
        if (m_pStatCurReqNET == NULL)
        {
            req.valid = false;
        }
        else
        {
            req.valid = true;
            req.address = m_pStatCurReqNET->getlineaddress();
            req.offset = m_pStatCurReqNET->offset;
            req.size = m_pStatCurReqNET->nsize;
            req.type = m_pStatCurReqNET->type;
            req.ptr = m_pStatCurReqNET;

            m_pmmStatProcessingNET->insert(pair<double,stat_stru_request_t>(sc_time_stamp().to_seconds(),req));
        }
    }

    if (m_pmmStatSetTrace != NULL)
    {
        stat_stru_set_t set;
        set.index = s_nStatSetTraceNo;
        set.lines = (stat_stru_line_t*)malloc(sizeof(stat_stru_line_t)*m_nAssociativity);

        //unsigned int nLinesAvailable = 0;
        // check the lines available

        dir_set_t &dirset = m_pSet[s_nStatSetTraceNo];
        for (unsigned int j=0;j<m_nAssociativity;j++)
        {
            set.lines[j].state = dirset.lines[j].state;
            set.lines[j].index = j;
            set.lines[j].address = dirset.lines[j].getlineaddress(s_nStatSetTraceNo, m_nSetBits);
            set.lines[j].counter = dirset.lines[j].counter;
        }

        m_pmmStatSetTrace->insert(pair<double,stat_stru_set_t>(sc_time_stamp().to_seconds(),set));
    }
}

void DirectoryRTTOK::DumpStatistics(ofstream& outfile, unsigned int component, unsigned int type)
{
    if (m_pmmStatRequestNo != NULL)
    {
        map<double, stat_stru_size_t>::iterator iter;

        for (iter=m_pmmStatRequestNo->begin();iter!=m_pmmStatRequestNo->end();iter++)
        {
            outfile << setw(10) << (*iter).first << "\t" << name() << "\t" << dec << (*iter).second << endl;
        }
    }

    if (m_pmmStatProcessingNET != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        for (iter=m_pmmStatProcessingNET->begin();iter!=m_pmmStatProcessingNET->end();iter++)
        {
            stat_stru_request_t req = (*iter).second;
            if (req.valid)
                outfile << "PrN\t" << setw(10) << (*iter).first << "\t" << req.ptr << "\t" << dec << req.address << "\t" << dec << req.type << "\t" << req.size << endl;
        }
    }

    if (m_pmmStatSetTrace != NULL)
    {
        map<double, stat_stru_set_t>::iterator iter;

        for (iter=m_pmmStatSetTrace->begin();iter!=m_pmmStatSetTrace->end();iter++)
        {
            stat_stru_set_t set = (*iter).second;
            outfile << "Tes\t" << setw(10) << (*iter).first << "\t" ;
            
            for (unsigned int i=0;i<m_nAssociativity;i++)
            {
                outfile << hex << set.lines[i].address << "\t" << set.lines[i].state << "\t" << set.lines[i].counter << "\t" ;
            }

            outfile << endl;
        }
    }

}

#endif
