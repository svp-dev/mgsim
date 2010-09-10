#include "directoryrttok.h"
#include "../simlink/linkmgs.h"

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
            m_pSet[i].lines[j].aux = auxinistate;
            m_pSet[i].lines[j].queuehead = SuspendedRequestQueue::EOQ;
            m_pSet[i].lines[j].queuetail = SuspendedRequestQueue::EOQ;
            m_pSet[i].lines[j].breserved = false;
            m_pSet[i].lines[j].priority = false;

            m_pSet[i].lines[j].setid = i;
        }
    }
}

void DirectoryRTTOK::ProcessRequestNET()
{
    ST_request* req = m_pReqCurNET;

    // any previously queued requests should not reach here // JNEWXXX
    assert(req->bqueued == false);
    switch (req->type)
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

    default:
        // Error
        abort();
        break;
    }
}

// send net request to memory
bool DirectoryRTTOK::SendRequestNETtoBUS(ST_request* req)
{
    m_pfifoMemory.push(req);
    req->bqueued = false;
    return true;
}

// send net to next node
bool DirectoryRTTOK::SendRequestNETtoNET(ST_request* req)
{
    SendRequest(req);
    req->bqueued = false;
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
             m_nStateNET = STATE_NET_PROCESSING;
        }

        return;
    }


    // 2. prefetch exists, incoming may exist or not
    if (m_pPrefetchDeferredReq != NULL)
    {
        // alert !!!
        assert(m_pPrefetchDeferredReq->bqueued);

        bool nofail = true;

        if (m_pReqCurNET2Net == NULL)   // try prefetch request when this interface is free for trying
        {
            if (SendRequestNETtoNET(m_pPrefetchDeferredReq))
            {
                // pop deferred request from the queue
	            ST_request* tmp = PopDeferredRequest();
	            (void)tmp;
	            assert(tmp == m_pPrefetchDeferredReq);
            }
        }
        else
        {
            // Try to send the original requests instead of trying prefetch deferred requests

            // no matter succeeded in sending prefetched request, the state can be reset when this request is sent
            if (SendRequestNETtoNET(m_pReqCurNET2Net))
            {
                m_pReqCurNET2Net = NULL;
            }
            else
            {
                nofail = false;
                m_nStateNET = STATE_NET_RETRY;
            }
        }

        if (m_pReqCurNET2Bus != NULL)
        {
            // Try to send the original requests instead of trying prefetch deferred requests

            // no matter succeeded in sending prefetched request, the state can be reset when this request is sent 
            if (SendRequestNETtoBUS(m_pReqCurNET2Bus))
            {
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
            m_nStateNET = STATE_NET_PROCESSING;
        }
    }
}

void DirectoryRTTOK::BehaviorNET()
{
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
        req_incoming = ReceiveRequest();
        m_pReqCurNET = m_pPipelineNET.shift(req_incoming);

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
        if (m_pPipelineNET.top() == NULL)
        {
            // fetch request from incoming buffer or from the deferred queue
            req_incoming = ReceiveRequest();
            
            // only shift
            m_pPipelineNET.shift(req_incoming);
        }

        assert( (m_pReqCurNET2Net != NULL)||(m_pReqCurNET2Bus != NULL) );
        SendRequestFromNET();
        break;

    default:
      abort();
        break;
    }
}

void DirectoryRTTOK::ProcessRequestBUS()
{
    ST_request* req = m_pReqCurBUS;

    // locate the line
    __address_t address = req->getreqaddress();

    dir_line_t* line = LocateLine(address);

    // update info ?
    line->time = sc_time_stamp();

    bool lineactivated = m_srqSusReqQ.ReactivateLine(line);
    (void)lineactivated;
    assert(lineactivated);

    m_pReqCurBUS2Net = req;

    // try sending the request
    SendRequestBUStoNet();
}

void DirectoryRTTOK::SendRequestBUStoNet()
{
    // try send the bus transaction to network
    SendRequest(m_pReqCurBUS2Net);

    // cycle done
    m_nStateBUS = STATE_BUS_PROCESSING;
}

// CHKS: optimization can be added, add the location or at least part of 
// the location (if the rest can be resolved from the request address)
// then the line can be appended without going through the pipeline stages
void DirectoryRTTOK::BehaviorBUS()
{
    ST_request* req_incoming = NULL;

    switch (m_nStateBUS)
    {
    case STATE_BUS_PROCESSING:
        // check whether any request available
        if (!m_pfifoFeedback.empty())
        {
            req_incoming = m_pfifoFeedback.front();
            m_pfifoFeedback.pop();
        }

        // get request from the pipeline
        m_pReqCurBUS = m_pPipelineBUS.shift(req_incoming);

        // if nothing, then skip
        if (m_pReqCurBUS != NULL)
            ProcessRequestBUS();

        break;

    case STATE_BUS_RETRY_TO_NET:
        if (m_pPipelineBUS.top() == NULL)
        {
            // check whether any request available
            if (!m_pfifoFeedback.empty())
            {
                req_incoming = m_pfifoFeedback.front();
                m_pfifoFeedback.pop();
            }
            m_pPipelineBUS.shift(req_incoming);
        }

        SendRequestBUStoNet();
        break;

    default:
        break;
    }
}

dir_line_t* DirectoryRTTOK::LocateLine(__address_t address)
{
    const unsigned int index = DirIndex(address);
    const uint64       tag   = DirTag(address);

    dir_line_t* set = &(m_pSet[index].lines[0]);
    for (unsigned int i=0; i<m_nAssociativity;i++)
    {
    	if (set[i].state != DLS_INVALID && set[i].tag == tag)
    	{
    		return &set[i];
        }
    }

    return NULL;
}

// replace only invalid lines otherwise NULL
dir_line_t* DirectoryRTTOK::GetReplacementLine(__address_t address)
{
    unsigned int index = DirIndex(address);

    dir_line_t* set = &(m_pSet[index].lines[0]);
    for (unsigned int i = 0; i < m_nAssociativity; i++)
    {
    	// return the first found empty one
    	if (set[i].state == DLS_INVALID)
    	{
    		return &set[i];
        }
    }

    return NULL;
}

unsigned int DirectoryRTTOK::DirIndex(__address_t address)
{
    return (address / m_nLineSize) % m_nSet;
}

uint64 DirectoryRTTOK::DirTag(__address_t address)
{
    return (address / m_nLineSize) / m_nSet;
}

void DirectoryRTTOK::FixDirLine(dir_line_t* line)
{
    if (line->tokencount <= GetTotalTokenNum())
    {
        if (line->tokencount == GetTotalTokenNum())
        {
            // All the tokens are collected by the directory now
            line->state      = DLS_INVALID;
            line->tokencount = 0;
            line->tokengroup = 0;
            line->counter    = 0;
            line->priority   = false;
        }

        // It's normal that token count is zero
        m_srqSusReqQ.NormalizeLineAux(line);
    }
}

ST_request* DirectoryRTTOK::PrefetchDeferredRequest()
{
    return m_srqSusReqQ.GetTopActiveRequest();
}

// popdeferredrequest will set the AUXSTATE to NONE, which could be incorrect, since sometimes it should be loading
// the auxstate should always be modified after calling pop
// when bRem == true, the top line in the line queue should be removed from the line queue
ST_request* DirectoryRTTOK::PopDeferredRequest()
{
    return m_srqSusReqQ.PopTopActiveRequest();
}

//////////////////////////////////////////////////////////////////////////
// protocol handling: protocol with intermediate states.

//////////////////////////////////////////////////////////////////////////

// network request handler
void DirectoryRTTOK::OnNETAcquireTokenData(ST_request* req)
{
    // correct the counter and state before the 

    __address_t address = req->getreqaddress();
    
    // locate certain set
    dir_line_t* line = LocateLine(address);

    if (req->tokenacquired > 0)
        assert(line != NULL);

    if ((req->tokenacquired == 0) && (line == NULL))
    {
        // need to fetch a line off the chip

        // allocate a space
        line = GetReplacementLine(req->getreqaddress());

        // must return an invalid line
        assert(line != NULL);

        // update line info
        line->tag = DirTag(req->getreqaddress());
        line->time = sc_time_stamp();
        line->state = DLS_CACHED;
        line->tokengroup = GetTotalTokenNum();


        if (m_srqSusReqQ.HasOutstandingRequest(line))
        {
            // redirect the request to the main memory
            // prepare the request but without sending

            // append the request to the queue
            if (!m_srqSusReqQ.AppendRequest2Line(req, line))
	        {
		        abort();
	        }
        }
        else
        {
            m_srqSusReqQ.StartLoading(line);
            
            // JOYING revisit, this is to resolve the reload bug
            req->bprocessed = false;

            // redirect the request to the main memory

            // save the request
            m_pReqCurNET2Bus = req;
        }

        return;

    }
    else
    {
        // line can be found in the group, just pass the request

        // line must be at cached state
        // update info
        line->time = sc_time_stamp();

        if (line->tokencount > 0)
        {
            // transfer tokens to the request only when the request is not an request with transient tokens
            if (req->btransient)
            {
                // no transfer
                assert(req->tokenrequested == GetTotalTokenNum());  // RE/ER/IV
                assert(req->bpriority == false);
            }
            else
            {
                req->tokenacquired += line->tokencount;
                req->bpriority = req->bpriority || line->priority;
                line->tokencount = 0;
                line->priority = false;

                assert(req->tokenacquired <= GetTotalTokenNum());
            }
        }

        // REVISIT, will this cause too much additional traffic?
        if ( (req->gettokenpermanent() == GetTotalTokenNum() && !req->dataavailable) ||
             (req->bprocessed && !req->dataavailable) )
        {
            // update line info
            line->tag = DirTag(req->getreqaddress());
            line->time = sc_time_stamp();
            line->state = DLS_CACHED;
            line->tokengroup = GetTotalTokenNum();


            if (m_srqSusReqQ.HasOutstandingRequest(line))
            {
                // just alert, to check whether this really happen
                assert(((req->gettokenpermanent() == GetTotalTokenNum())&&(!req->dataavailable)) == false);
            }
            else
            {
                m_srqSusReqQ.StartLoading(line);

                // redirect the request to the main memory

                // save the request
                m_pReqCurNET2Bus = req;

                return;
            }
        }
    }


    if (m_srqSusReqQ.HasOutstandingRequest(line))
    {
        // append request
        if (!m_srqSusReqQ.AppendRequest2Line(req, line))
        {
	        abort();
        }
    }
    else
    {
        // save the request
        m_lstReqNET2Net.push_back(req);
        // m_pReqCurNET2Net = req;
    }
}

void DirectoryRTTOK::OnNETAcquireToken(ST_request* req)
{
    __address_t address = req->getreqaddress();

    // locate certain set
    dir_line_t* line = LocateLine(address);

    assert( ((line != NULL)&&(!req->bqueued)&&(!m_srqSusReqQ.IsRequestQueueEmpty(line))) == false );


    if (line == NULL)    // invalid
    {
      abort();
        dir_line_t* pline = GetReplacementLine(address);

        // a local write
        pline->state = DLS_CACHED;
        pline->time = sc_time_stamp();
        pline->tag = DirTag(address);

        // counter should be set to 1
        line->counter = 1;
    }
    else 
    {
        assert(line->state == DLS_CACHED);
    
        // update info
        line->time = sc_time_stamp();

        // transfer tokens to the request only when the request is not an request with transient tokens
        if (req->btransient)
        {
            // no transfer
            assert(req->tokenrequested == GetTotalTokenNum());  // RE/ER/IV
            assert(req->bpriority == false);
        }
        else
        {
            req->tokenacquired += line->tokencount;
            line->tokencount = 0;
            req->bpriority = req->bpriority || line->priority;
            line->priority = false;
            line->tokencount = 0;

            if (req->bpriority)
            {
                req->btransient = false;
            }

            assert(req->gettokenpermanent() <= GetTotalTokenNum());
        }

        line->tokencount = 0;
    }

    if (m_srqSusReqQ.HasOutstandingRequest(line))
    {
        // append request
        if (!m_srqSusReqQ.AppendRequest2Line(req, line))
        {
	        abort();
        }
    }
    else
    {
        // save the request
        m_lstReqNET2Net.push_back(req);
    }
}


void DirectoryRTTOK::OnNETDisseminateTokenData(ST_request* req)
{
    // EV request will always terminate at directory
    __address_t address = req->getreqaddress();

    assert(req->tokenacquired > 0);

    // locate certain set
    dir_line_t* line = LocateLine(address);
    assert(line != NULL);
    assert(line->state == DLS_CACHED);
    
        if (req->tokenrequested == 0)    // EV
        {
            if (m_srqSusReqQ.HasOutstandingRequest(line))
            {     
                // append request
                if (!m_srqSusReqQ.AppendRequest2Line(req, line))
                {
		            abort();
                }
            }
            else
            {
                assert(req->btransient == false);
                line->tokencount += req->tokenacquired;
                line->priority = line->priority || req->bpriority;

                if (line->tokencount == GetTotalTokenNum())
                {
                    // fix the line
                    FixDirLine(line); 
                }

                // request is eliminated too
                delete req;
                
            }
        }
        else 
        {
            assert(req->tokenrequested == GetTotalTokenNum());  // WB

            if (m_srqSusReqQ.HasOutstandingRequest(line))
            {     
                // append request
                if (!m_srqSusReqQ.AppendRequest2Line(req, line))
                {
		            abort();
                }
            }
            else
            {
                line->tokencount += req->tokenacquired;
                line->priority    = line->priority || req->bpriority;

                // fix the line 
                FixDirLine(line);

                if (m_srqSusReqQ.HasOutstandingRequest(line))
                {
                    // append request
                    if (!m_srqSusReqQ.AppendRequest2Line(req, line))
                    {
		                abort();
                    }
                }
                else
                {
                    // redirect the request to the main memory

                    // save the request
                    m_pReqCurNET2Bus = req;
                }
            }
        }
}
