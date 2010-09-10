#include "cachel2tok.h"
#include "../simlink/linkmgs.h"

using namespace MemSim;

namespace MemSim
{
const unsigned int CacheL2TOK::EOQ = 0xffffffff;
const unsigned int CacheL2TOK::QueueBufferSize = 0x100;
      unsigned int CacheL2TOK::s_nGlobalFIFOUpperMargin = 1;    // UPPER Margin
const unsigned int CacheL2TOK::s_nGlobalFIFOLowerMargin = 0x100;    // LOWER Margin
}

// process an initiative request from processor side
void CacheL2TOK::ProcessInitiative()
{
    // handle request
    // the requests arrive here should be only local read and local write
    switch (m_pReqCurINI->type)
    {
    case MemoryState::REQUEST_READ:
        // $$$ optimization for victim buffer $$$
        // check the victim buffer first
        if (!OnLocalReadVictimBuffer(m_pReqCurINI))
        {
            OnLocalRead(m_pReqCurINI);
        }
        break;

    case MemoryState::REQUEST_WRITE:
        OnLocalWrite(m_pReqCurINI);
        // $$$ optimization for victim buffer $$$
        // remove the item from victim buffer on local write
        m_fabEvictedLine.RemoveBufferItem(m_pReqCurINI->getlineaddress());
        break;

    default:
        break;
    }
}

// $$$ optimization for victim buffer $$$
// Load from the victim buffer
bool CacheL2TOK::OnLocalReadVictimBuffer(ST_request* req)
{
    // CHKS: double check the validness of the victim buffer in relation to the cache content
    char *data = m_fabEvictedLine.FindBufferItem(req->getlineaddress());
    if (data != NULL)
    {
        // get the data
        // update request 
        memcpy(req->data, data, g_nCacheLineSize);
        req->type = MemoryState::REQUEST_READ_REPLY;

        // save request 
        InsertSlaveReturnRequest(true, req);

        // return the reply to the processor side
        return true;
    }

    return false;
}

bool CacheL2TOK::SendAsNodeINI(ST_request* req)
{
    // reset the queued property
    req->bqueued = false;

    // send request to memory
    SendRequest(req);

    // change the state to free
    return true;
}

bool CacheL2TOK::SendAsSlave(ST_request* req)
{
    // Send the feedback to all masters

    // First check if all masters can receive feedback
    for (set<ProcessorTOK*>::iterator iter = m_processors.begin(); iter != m_processors.end(); ++iter)
    {
        if (!(*iter)->CanSendFeedback())
        {
            return false;
        }
    }

    // Now send it
    req->refcount = m_processors.size();
    for (set<ProcessorTOK*>::iterator iter = m_processors.begin(); iter != m_processors.end(); ++iter)
    {
        if (!(*iter)->SendFeedback(req))
        {
            // This shouldn't fail since they said they could accept
            abort();
        }
    }
    return true;
}

bool CacheL2TOK::SendAsSlaveINI(ST_request* req)
{
    // reset the queued property
    req->bqueued = false;

    // send reply transaction
    SendAsSlave(req);

    // succeed in sending to slave interface
    if (req->type == REQUEST_READ_REPLY)
    {
        m_fabInvalidation.RemoveBufferItem(req->getlineaddress());
    }
    else if (req->type == REQUEST_WRITE_REPLY)
    {
        if (m_fabInvalidation.FindBufferItem(req->getlineaddress()) == NULL)
        {
        }
    }

    // change the state to free
    return true;
}

void CacheL2TOK::SendFromINI()
{
    if (m_pReqCurINIasSlaveX == NULL)
    {
        m_pReqCurINIasSlaveX = GetSlaveReturnRequest(true);
    }
    
    if (m_pReqCurINIasNodeDB != NULL)
    {
        if (SendAsNodeINI(m_pReqCurINIasNodeDB))
        {
            m_pReqCurINIasNodeDB = NULL;
        }
    }
    else if (m_pReqCurINIasNode != NULL)
    {
        if (SendAsNodeINI(m_pReqCurINIasNode))
        {
            m_pReqCurINIasNode = NULL;
        }
    }

    if (m_pReqCurINIasSlaveX != NULL)
    {
        if (SendAsSlaveINI(m_pReqCurINIasSlaveX))
        {
            m_pReqCurINIasSlaveX = NULL;
        }
    }

    m_nStateINI = (m_pReqCurINIasNodeDB == NULL && m_pReqCurINIasNode == NULL && m_pReqCurINIasSlaveX == NULL)
        ? STATE_INI_PROCESSING
        : STATE_INI_RETRY;
}

ST_request* CacheL2TOK::FetchRequestINIFromQueue()
{
    CheckThreshold4GlobalFIFO();

    if (!m_bBufferPriority)
    {
        // Check input queue
        if (!m_requests.empty())
        {
            ST_request* ret = m_requests.front();
            m_requests.pop();

            if (DoesFIFOSeekSameLine(ret))
            {
                InsertRequest2GlobalFIFO(ret);
                return NULL;
            }

            return ret;
        }
    }
    
    // Check FIFO buffer
    if (!GlobalFIFOEmpty())
    {
        return PopQueuedRequest();
    }
    return NULL;
}

void CacheL2TOK::CleansingPipelineINI(ST_request* req)
{
    //////////////////////////////////////////////////////////////////////////
    // two passes:
    // first pass, will process the pipeline registers reversely considering only the previously queued requests
    // they will be removed from the pipeline, and pushed reversely from the front into the queue
    // second pass, will process the pipeline registers check all the previously non-queued requests
    // they will be removed from the pipeline, and pushed directly from the back as the order they are in the pipeline

    __address_t lineaddr = req->getlineaddress();

    // temp request vector
    vector<ST_request*> vecreq;

    //////////////////////////////////////////////////////////////////////////
    // dump all the stuff to the vector
    // the checking is carried out from the back to front (tail to head)
    m_pPipelineINI.copy(vecreq);

    //////////////////////////////////////////////////////////////////////////
    // first pass

    // queued requests: reversely check every register
    for (int i=vecreq.size()-1;i>=0;i--)
    {
        ST_request* reqtemp = vecreq[i];

        if ((reqtemp != NULL)&&(reqtemp->bqueued)&&(reqtemp->getlineaddress() == lineaddr))
        {
            // reversely push back from the front side to the global queue 
            InsertRequest2GlobalFIFO(reqtemp);

            // fill the place for the register with NULL
            vecreq[i] = NULL;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // second pass
    // non-queued requests
    for (unsigned int i=0;i<vecreq.size();i++)
    {
        ST_request* reqtemp = vecreq[i];

        if ((reqtemp != NULL)&&(!reqtemp->bqueued)&&(reqtemp->getlineaddress() == lineaddr))
        {
            // this is unlikely to happen for now
            // CHKS: might need some other check since there are already requests put in to queue in the first pass ...

            // push back from the bottom to the global queue
            InsertRequest2GlobalFIFO(reqtemp);

            // fill the place for the regiser with NULL
            vecreq[i] = NULL;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // dump back to the pipeline 
    m_pPipelineINI.reset(vecreq);
}

// cleansing pipeline and insert request into queue
void CacheL2TOK::CleansingAndInsert(ST_request* req)
{
    // cleansing the pipeline
    CleansingPipelineINI(req);

    // put the current request in the global queue
    if (!InsertRequest2GlobalFIFO(req))
    {
	    abort();
    }
}


// react to the processors
void CacheL2TOK::BehaviorIni()
{
    switch (m_nStateINI)
    {
    case STATE_INI_PROCESSING:
        // initialize requests to be sent
        m_pReqCurINIasNodeDB = NULL;
        m_pReqCurINIasNode   = NULL;
        m_pReqCurINIasSlaveX = NULL;

        m_pReqCurINI = m_pPipelineINI.shift( FetchRequestINIFromQueue() );

        // if nothing to process, just skip this round
        if (m_pReqCurINI == NULL)
        {
            if (m_pReqCurINIasSlaveX == NULL)
            {
                m_pReqCurINIasSlaveX = GetSlaveReturnRequest(true);
            }
            
            if (m_pReqCurINIasSlaveX != NULL)
            {
                SendFromINI();
            }

            break;
        }

        // process request
        ProcessInitiative();

        SendFromINI();
        break;

    // try to send the request again to the network
    case STATE_INI_RETRY:
        if (m_pPipelineINI.top() == NULL)
        {
            m_pPipelineINI.shift( FetchRequestINIFromQueue() );
        }
        SendFromINI();
        break;

    default:
        abort();
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// passive handling
void CacheL2TOK::ProcessPassive()
{
    // JXXX maybe do the same with INI?
    if (m_pReqCurPAS != NULL && m_pReqCurINI != NULL && m_pReqCurPAS->getlineaddress() == m_pReqCurINI->getlineaddress())
    {
        assert(m_nStatePAS == STATE_PAS_PROCESSING || m_nStatePAS == STATE_PAS_POSTPONE);
        m_nStatePAS = STATE_PAS_POSTPONE;
        return;
    }
    
    m_nStatePAS = STATE_PAS_PROCESSING;
    ST_request* req = m_pReqCurPAS;
    switch(req->type)
    {
    case MemoryState::REQUEST_ACQUIRE_TOKEN:
		if (req->source == m_id)
			OnAcquireTokenRet(req);
		else
			OnAcquireTokenRem(req);
        break;

    case MemoryState::REQUEST_ACQUIRE_TOKEN_DATA:
		if (req->source == m_id)
			OnAcquireTokenDataRet(req);
		else
			OnAcquireTokenDataRem(req);
        break;

    case MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA:
        OnDisseminateTokenData(req);
        break;

    case Request_LOCALDIR_NOTIFICATION:
        OnDirNotification(req);
        break;

    default:
        break;
    }
}

bool CacheL2TOK::SendAsNodePAS(ST_request* req)
{
    // check colliding request
    if ((m_nStateINI == STATE_INI_RETRY && m_pReqCurINIasNode   != NULL && req->getlineaddress() == m_pReqCurINIasNode  ->getlineaddress()) ||
        (m_nStateINI == STATE_INI_RETRY && m_pReqCurINIasNodeDB != NULL && req->getlineaddress() == m_pReqCurINIasNodeDB->getlineaddress()))
    {
        // it's already retrying for the INI request on the same address, suspend the network request for this cycle, and send INI request instead.
        return false;
    }

    // send request to memory
    SendRequest(req);
    return true;
}


bool CacheL2TOK::SendAsSlavePAS(ST_request* req)
{
    // send reply transaction
    SendAsSlave(req);

    // succeed in sending to slave interface
    if (req->type == REQUEST_READ_REPLY)
    {
        m_fabInvalidation.RemoveBufferItem(req->getlineaddress());
    }
    else if (req->type == REQUEST_WRITE_REPLY)
    {
        if (m_fabInvalidation.FindBufferItem(req->getlineaddress()) == NULL)
        {
        }
    }

    // interface is free to process next req
    return true;
}


void CacheL2TOK::SendFromPAS()
{
    if (m_pReqCurPASasSlaveX == NULL)
    {
        m_pReqCurPASasSlaveX = GetSlaveReturnRequest(false);
    }
    
    if (m_pReqCurPASasNodeX == NULL)
    {
        m_pReqCurPASasNodeX = GetPASNodeRequest();
    }
    
    if (m_pReqCurPASasSlaveX == NULL && m_pReqCurPASasNodeX == NULL)
    {
        // assert the state
        // actually the state can only be processing
        assert(m_nStatePAS == STATE_PAS_PROCESSING || m_nStatePAS == STATE_PAS_POSTPONE);

        // maybe this is not necessary
        m_nStatePAS = STATE_PAS_PROCESSING;
        return;
    }

    if (m_pReqCurPASasSlaveX != NULL)
    {
        if (SendAsSlavePAS(m_pReqCurPASasSlaveX))
        {
            m_pReqCurPASasSlaveX = NULL;
        }
    }

    if (m_pReqCurPASasNodeX != NULL)
    {
        if (SendAsNodePAS(m_pReqCurPASasNodeX))
        {
            m_pReqCurPASasNodeX = NULL;
        }
    }

    m_nStatePAS = (m_pReqCurPASasNodeX == NULL && m_pReqCurPASasSlaveX == NULL && m_queReqPASasNode.empty())
        ? STATE_PAS_PROCESSING
        : STATE_PAS_RETRY;
}

// react to the network request/feedback
void CacheL2TOK::BehaviorNet()
{
    // At the end of every cycle, check the MGSim/SystemC interface
    for (set<ProcessorTOK*>::iterator iter = m_processors.begin(); iter != m_processors.end(); ++iter)
    {
        (*iter)->OnCycleStart();
    }

    ST_request* req_incoming;

    switch (m_nStatePAS)
    {
    case STATE_PAS_PROCESSING:
        req_incoming  = ReceiveRequest();

        // shift pipeline and get request from pipeline
        m_pReqCurPAS = m_pPipelinePAS.shift(req_incoming);

        // reset combined pointer
        m_pReqCurPASasSlaveX = NULL;
        m_pReqCurPASasNodeX  = NULL;

        if (m_pReqCurPAS == NULL)
        {
            m_pReqCurPASasSlaveX = GetSlaveReturnRequest(false);            
            m_pReqCurPASasNodeX  = GetPASNodeRequest();
            
            if (m_pReqCurPASasSlaveX != NULL)
                SendFromPAS();

            break;
        }

        // processing the reply request
        ProcessPassive();

        if (m_nStatePAS != STATE_PAS_POSTPONE)
        {
            SendFromPAS();
        }
        break;

    case STATE_PAS_POSTPONE:
        ProcessPassive();

        if (m_nStatePAS == STATE_PAS_POSTPONE)
            break;

        SendFromPAS();

        break;

    case STATE_PAS_RETRY:
        if (m_pPipelinePAS.top() == NULL)
        {
            req_incoming = ReceiveRequest();

            // shift pipeline and get request from pipeline
            m_pPipelinePAS.shift(req_incoming);
        }

        SendFromPAS();
        break;

    default:
        abort();
        break;
    }
}

cache_line_t* CacheL2TOK::LocateLine(__address_t address)
{
    cache_line_t* line;
    unsigned int index = CacheIndex(address);
    uint64 tag = CacheTag(address);

    line = &(m_sets[index].lines[0]);

    for (unsigned int i=0; i<m_assoc;i++, line++)
    {
        if ((line->state != CLS_INVALID) && (line->tag == tag))
            return line;
    }

    // miss
    return NULL;
}

cache_line_t* CacheL2TOK::GetEmptyLine(__address_t address)
{
    cache_line_t* line = &(m_sets[CacheIndex(address)].lines[0]);
 
    for (unsigned int i=0; i<m_assoc; i++, line++)
    {
        // return the first found empty one
        if (line->state == CLS_INVALID)
            return line;
    }

    return NULL;
}

void CacheL2TOK::UpdateRequest(ST_request* req, cache_line_t* line, MemoryState::REQUEST requesttype, __address_t address, bool bdataavailable, bool bpriority, bool btransient, unsigned int ntokenacquired, REQUEST_UPDATE_METHOD rum)
{
    req->type = requesttype;
    req->tokenacquired = ntokenacquired;
    req->dataavailable = bdataavailable;
    req->bpriority = bpriority;
    req->btransient = btransient;

    if (req->bpriority)
        assert(!req->btransient);
    if (ntokenacquired == 0)
        assert(!bpriority);
    else if (ntokenacquired == GetTotalTokenNum())
        assert(bpriority);

    if (requesttype == REQUEST_READ || requesttype == REQUEST_WRITE || requesttype == REQUEST_READ_REPLY || requesttype == REQUEST_WRITE_REPLY || requesttype == REQUEST_INVALIDATE_BR)
    {
        assert(bpriority == false);
        assert(ntokenacquired == 0xffff);
    }

    assert(line != NULL);
    assert(line->state != CLS_INVALID);

    // if nothing to be updated
    if (rum == RUM_NONE)
        return;

    if (rum == RUM_ALL)
        req->dataavailable = true;

    bool blinecomplete = line->IsLineAtCompleteState();
    
    // check the whole line with mask bits and update the request according to the cacheline
    for (unsigned int i = 0; i < CACHE_BIT_MASK_WIDTH; i++)
    {
        if (line->bitmask[i] || blinecomplete)
   		if (rum != RUM_MASK     ||  req->bitmask[i])
		if (rum != RUM_NON_MASK || !req->bitmask[i])
		{
            req->data[i] = line->data[i];

            // update read request bitmaks, write request bitmask will not be updated. 
            if (requesttype == REQUEST_ACQUIRE_TOKEN_DATA && req->tokenrequested != GetTotalTokenNum())
            {
                req->bitmask[i] = true;
            }
        }
    }
}

bool CacheL2TOK::InsertOutstandingRequest(ST_request* req)
{
	__address_t alignedaddr = AlignAddress4Cacheline(req->getlineaddress());
	return m_mapPendingRequests.insert(pair<__address_t, ST_request*>(alignedaddr, req)).second;
}

ST_request* CacheL2TOK::RemoveOutstandingRequest(__address_t address)
{
	__address_t alignedaddr = AlignAddress4Cacheline(address);

	map<__address_t, ST_request*>::iterator iter = m_mapPendingRequests.find(alignedaddr);
	if (iter == m_mapPendingRequests.end())
		return NULL;

	ST_request* ret = iter->second;
	m_mapPendingRequests.erase(iter);

	return ret;
}

// ntoken is the token required
void CacheL2TOK::Modify2AcquireTokenRequest(ST_request* req, unsigned int ntoken)
{
    req->source = m_id;
    req->tokenrequested = ntoken;
    req->dataavailable = false;
}

void CacheL2TOK::Modify2AcquireTokenRequestRead(ST_request* req)
{
    assert((req->type != MemoryState::REQUEST_ACQUIRE_TOKEN)&&(req->type != MemoryState::REQUEST_ACQUIRE_TOKEN_DATA));

    req->type = MemoryState::REQUEST_ACQUIRE_TOKEN_DATA;
    // clear request bitmask
    std::fill(req->bitmask, req->bitmask + CACHE_BIT_MASK_WIDTH, false);

    Modify2AcquireTokenRequest(req, 1);
}

void CacheL2TOK::Modify2AcquireTokenRequestWrite(ST_request* req, bool reqdata)
{
    assert((req->type != MemoryState::REQUEST_ACQUIRE_TOKEN)&&(req->type != MemoryState::REQUEST_ACQUIRE_TOKEN_DATA));

    if (reqdata)
        req->type = MemoryState::REQUEST_ACQUIRE_TOKEN_DATA;
    else
        req->type = MemoryState::REQUEST_ACQUIRE_TOKEN;

    Modify2AcquireTokenRequest(req, GetTotalTokenNum());
}

void CacheL2TOK::Modify2AcquireTokenRequestWrite(ST_request* req, cache_line_t* line, bool reqdata)
{
    assert((req->type != MemoryState::REQUEST_ACQUIRE_TOKEN)&&(req->type != MemoryState::REQUEST_ACQUIRE_TOKEN_DATA));

    if (reqdata)
        req->type = MemoryState::REQUEST_ACQUIRE_TOKEN_DATA;
    else
        req->type = MemoryState::REQUEST_ACQUIRE_TOKEN;

    Modify2AcquireTokenRequest(req, GetTotalTokenNum());
}

// disseminate a number of tokens
ST_request* CacheL2TOK::NewDisseminateTokenRequest(ST_request* req, cache_line_t* line)
{
    assert(line->state != CLS_INVALID);
    
    unsigned int set = CacheIndex(req->getlineaddress());
    __address_t address = (line->tag * m_nSets + set) * g_nCacheLineSize;
     
    ST_request* reqdt = new ST_request();
    reqdt->type = MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA;
    reqdt->addresspre = address / g_nCacheLineSize;
    
    reqdt->offset = 0;
    reqdt->nsize = g_nCacheLineSize;
    reqdt->Conform2BitVecFormat();
    reqdt->dataavailable = true;
    reqdt->bpriority = line->priority;
    reqdt->tokenrequested = (line->state == CLS_SHARER) ? 0 : GetTotalTokenNum();
    reqdt->tokenacquired = line->tokencount;
    memcpy(reqdt->data, line->data, g_nCacheLineSize);
    reqdt->source = m_id;

    line->priority = false;
    line->tokencount = 0;

    PostDisseminateTokenRequest(line, reqdt);
    return reqdt;
}

void CacheL2TOK::PostDisseminateTokenRequest(cache_line_t* line, ST_request* pdisseminatetokenreq)
{
    if (line->tokencount == 0)
    {
        line->state = CLS_INVALID;
        line->pending = false;
        line->invalidated = false;
        line->priority = false;

        // $$$ optimization for victim line buffer $$$
        m_fabEvictedLine.InsertItem2Buffer(pdisseminatetokenreq->getlineaddress(), pdisseminatetokenreq->data, g_nCacheLineSize);
    }
}

void CacheL2TOK::InsertSlaveReturnRequest(bool ini, ST_request *req)
{
    assert(req != NULL);
    if (ini)
    {
        m_queReqINIasSlave.push(req);
        if (m_pReqCurINIasSlaveX == NULL)
        {
            m_pReqCurINIasSlaveX = m_queReqINIasSlave.front();
            m_queReqINIasSlave.pop();
        }
    }
    else
    {
        m_queReqPASasSlave.push(req);
        if (m_pReqCurPASasSlaveX == NULL)
        {
            m_pReqCurPASasSlaveX = m_queReqPASasSlave.front();
            m_queReqPASasSlave.pop();
        }
    }
}

void CacheL2TOK::InsertPASNodeRequest(ST_request* req)
{
    m_queReqPASasNode.push(req);
    if (m_pReqCurPASasNodeX == NULL)
    {
        m_pReqCurPASasNodeX = m_queReqPASasNode.front();
        m_queReqPASasNode.pop();
    }
}

//////////////////////////////////////////////////////////////////////////
// initiative request handling
//////////////////////////////////////////////////////////////////////////
    
// write almost always has the priority, 
// in case the RS/SR passes a W,P,U state, 
// 1. RS/SR passes W, P states, the line will always keeps all the permanent tokens.
// 2. RS/SR passes U state, if either req or the line has the priority token, 
// the line will transform it's locked token to unlocked state and acquire all the tokens request has
// if none of the has the priority token, still the line will take all the tokens, 
// but this time the request should have no tokens at all...

// ***
// be careful about the incre_update and incre_overwrite 
// only the one has priority token should do overwrite otherwise update

// Local Read 
void CacheL2TOK::OnLocalRead(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate certain set
    cache_line_t* line = LocateLine(address);

    // check whether it's a hit on the MSB
    bool hitonmsb = m_msbModule.IsAddressPresent(address);

    // check whether the line is already locked
    bool lockonmsb = hitonmsb?m_msbModule.IsSlotLocked(address, req):false;

    // handling INVALID state
    if (line == NULL)	// invalid
    {
        assert(!hitonmsb);
        // find an appropriate line for replacement,
        // change states to LNWRITEPENDINGI or LWWRITEPENDINGM
        // broadcast invalidate or RE request
        // may need to suspend the request in to the global fifo queue,
        // since there's a possibility that all the lines might be occupied by the locked states

        // get the line for data
        line = GetReplacementLine(address);
        if (line == NULL)
        {
            // All the cachelines are locked for now.
            // Cleansing the pipeline and insert the request to the global FIFO.
            CleansingAndInsert(req);
            return;
        }

        if (line->state == CLS_INVALID)
        {
            // check the size of the write request
            assert(req->nsize + req->offset <= m_lineSize);

            // update line info
            line->tag         = CacheTag(req->getlineaddress());
            line->time        = sc_time_stamp();
            line->state       = CLS_SHARER;
            line->tokencount  = 0;
            line->pending     = true;
            line->invalidated = false;
            line->priority    = false;
            line->tlock       = false;
            
            // modify request
            Modify2AcquireTokenRequestRead(req);

            // save the current request
            m_pReqCurINIasNode = req;
            // try to send the request to network           
        }
        else
        {
            assert(!line->pending);    // non-pending lines

            // disseminate line tokens
            ST_request* reqdd = NewDisseminateTokenRequest(req, line);

            // save the request for double node sending
            m_pReqCurINIasNodeDB = reqdd;

            // remove mask
            std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, false);

            // update cacheline
            // erase the previous values and store the new data with LNWRITEPENDINGI state
            line->tag = CacheTag(req->getlineaddress());
            line->time = sc_time_stamp();
            line->state = CLS_SHARER;
            line->tokencount = 0;
            line->pending = true;
            line->invalidated = false;
            line->priority = false;
            line->tlock = false;

            // modify request
            Modify2AcquireTokenRequestRead(req);

            // save this as a normal request
            m_pReqCurINIasNode = req;

            // the normal request will be sent out in later cycles
        }
        return;
    }

    // the line state must be something here
    assert(line->state != CLS_INVALID);

    if ((line->tokencount > 0)&&(!lockonmsb)&&(line->IsLineAtCompleteState()))   // have valid data available    // WPE with valid data might have some token
    {
        // update time for REPLACE_POLICY_LRU
        line->tag = CacheTag(req->getlineaddress());
        line->time = sc_time_stamp();
         
        // write data back
        UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY, address, true);

        if (hitonmsb)
        {
            m_msbModule.LoadBuffer(req, line);
        }

        // save request
        InsertSlaveReturnRequest(true, req);
    }
    else   // data is not available yet
    {
        assert(line->pending);

        if (lockonmsb)
        {
            // cleansing the pipeline and insert the request to the global FIFO
            CleansingAndInsert(req);
            // pipeline done
        }
        else
        {
            // X-Token protocol ======
            if (hitonmsb)
            {
                if (m_msbModule.LoadBuffer(req, line))
                {
                    // succeed
                    // save request 
                    InsertSlaveReturnRequest(true, req);
                    return;
                }
                else
                {
                    assert(line->pending);

                    // cleansing the pipeline and insert the request to the global FIFO
                    CleansingAndInsert(req);
                    // pipeline done
                }
            }
            else if (line->tokencount > 0 && line->IsLineAtCompleteState())
            {
                // update time for REPLACE_POLICY_LRU
                line->tag = CacheTag(req->getlineaddress());
                line->time = sc_time_stamp();

                // write data back
                UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY, address, true);

                // save request 
                InsertSlaveReturnRequest(true, req);
            }
            else
            {
                // cleansing the pipeline and insert the request to the global FIFO
                CleansingAndInsert(req);
            }
        }
    }
}

// local Write
void CacheL2TOK::OnLocalWrite(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate certain set
    cache_line_t* line = LocateLine(address);
    // __address_t value = req->data[0];

    // check whether it's a hit on the MSB
    bool hitonmsb = m_msbModule.IsAddressPresent(address);

    // check whether the line is already locked
    bool lockonmsb = hitonmsb?m_msbModule.IsSlotLocked(address, req):false;

    // handling INVALID state
    if (line == NULL)	// invalid
    {
        assert(!hitonmsb);
        // find an appropriate line for replacement,
        // change states to LNWRITEPENDINGI or LWWRITEPENDINGM
        // broadcast invalidate or RE request
        // may need to suspend the request in to the global fifo queue,
        // since there's a possibility that all the lines might be occupied by the locked states

        // get the line for data
        line = GetReplacementLine(address);

        // if all the cachelines are locked for now
        if (line == NULL)
        {// put the current request in the global queue

            // cleansing the pipeline and insert the request to the global FIFO
            CleansingAndInsert(req);

            // pipeline done
            return;
        }

        if (line->state == CLS_INVALID)
        {
            // check the size of the write request
            assert(req->offset + req->nsize <= m_lineSize);

            // update line info
            if (!line->pending)
            {
                // clear the bitmask
                std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, false);
            }

            line->tag = CacheTag(req->getlineaddress());
            line->time = sc_time_stamp();
            line->state = CLS_OWNER;
            line->tokencount = 0;
            line->pending = true;
            line->invalidated = false;
            line->priority = false;
            line->tlock = false;
             
            for (unsigned int i = 0; i < g_nCacheLineSize; i++)
            {
                if (req->IsRequestWithCompleteData() || req->bitmask[i])
                {
                    line->bitmask[i] = true;
                    line->data[i] = req->data[i];
                }
            }

            // modify request
            Modify2AcquireTokenRequestWrite(req, true);

            // save the current request
            m_pReqCurINIasNode = req;
        }
        else 
        {
            // non-pending lines
            assert(!line->pending);

            // disseminate line tokens
            ST_request *reqdd = NewDisseminateTokenRequest(req, line);

            // save the request for double node sending
            m_pReqCurINIasNodeDB = reqdd;

			// remove the bit mask!
			std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, false);

            // update cacheline 
            line->tag = CacheTag(req->getlineaddress());
            line->time = sc_time_stamp();
            line->state = CLS_OWNER;
            line->tokencount = 0;
            line->pending = true;
            line->invalidated = false;
            line->priority = false;
            line->tlock = false;

            for (unsigned int i = 0; i < g_nCacheLineSize; i++)
            {
                if (req->IsRequestWithCompleteData() || req->bitmask[i])
                {
                    line->bitmask[i] = true;
                    line->data[i] = req->data[i];
                }
            }

            // modify request
            Modify2AcquireTokenRequestWrite(req, true);

            // save this as a normal request
            m_pReqCurINIasNode = req;
        }
    }
    else if (!line->pending)   // non-pending state
    {
        assert(!line->invalidated);
        assert(!line->tlock);

        if (line->gettokenglobalvisible() < GetTotalTokenNum())  // non-exclusive
        {
            // data is non-exclusive
            // 1. change states to Pending, and it will later change to modified
            // 2. acquire the rest of tokens

            // update line
            // clear the bitmask
            std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, false);

            line->tag = CacheTag(req->getlineaddress());
            line->time = sc_time_stamp();
            line->state = CLS_OWNER;
            line->pending = true;

            for (unsigned int i = 0; i < g_nCacheLineSize; i++)
            {
                if (req->IsRequestWithCompleteData() || req->bitmask[i])
                {
                    line->bitmask[i] = true;
                    line->data[i] = req->data[i];
                }
            }

            assert(!hitonmsb);

            // modify request
            Modify2AcquireTokenRequestWrite(req, line, false);

            // REVIST
            if (line->priority)
            {
                assert(line->tokencount > 0);

                // make request priority, if possible
                req->bpriority = line->priority;
                req->tokenacquired += 1;

                // update line
                line->tag = CacheTag(req->getlineaddress());
                line->time = sc_time_stamp();
                line->state = CLS_OWNER;
                line->tokencount = line->tokencount - 1;
                line->pending = true;
                line->priority = false;
                    
                for (unsigned int i = 0; i < g_nCacheLineSize; i++)
                {
                    if (req->IsRequestWithCompleteData() || req->bitmask[i])
                    {
                        line->bitmask[i] = true;
                        line->data[i] = req->data[i];
                    }
                }
            }

            // save the current request
            m_pReqCurINIasNode = req;
        }
        else    // exclusive
        {
            // can write directly at exclusive or modified lines

            // update time for REPLACE_POLICY_LRU
            // clear the bitmask
            std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, false);

            line->tag = CacheTag(req->getlineaddress());
            line->time = sc_time_stamp();
            line->state = CLS_OWNER;
            line->pending = false;

            for (unsigned int i = 0; i < g_nCacheLineSize; i++)
            {
                if (req->IsRequestWithCompleteData() || req->bitmask[i])
                {
                    line->bitmask[i] = true;
                    line->data[i] = req->data[i];
                }
            }

            // change reply
            UpdateRequest(req, line, MemoryState::REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

            // save request 
            InsertSlaveReturnRequest(true, req);
            // return the reply to the processor side
        }
    }
    else    // pending request
    {
        if (lockonmsb)
        {
            // always suspend on them   // JXXX maybe treat exclusive/available data differently
            // cleansing the pipeline and insert the request to the global FIFO
            CleansingAndInsert(req);
        }
        else
        {
            // X-Token MSB implementation
            if (line->state == CLS_SHARER && line->priority)
            {
                // Read pending with priority token
                assert(line->tokencount > 0);
                assert(!line->tlock);
                assert(!line->invalidated);

                // write directly to the line and change line state to W
                line->tag = CacheTag(req->getlineaddress());
                line->time = sc_time_stamp();
                line->state = CLS_OWNER;

                for (unsigned int i = 0; i < g_nCacheLineSize; i++)
                {
                    if (req->IsRequestWithCompleteData() || req->bitmask[i])
                    {
                        line->bitmask[i] = true;
                        line->data[i] = req->data[i];
                    }
                }

                // modify request
                Modify2AcquireTokenRequestWrite(req, line, false);

                // save the current request
                m_pReqCurINIasNode = req;
            }
            else
            {
                // try to write to the buffer
                if (!m_msbModule.WriteBuffer(req))
                {
                    return;
                }

                // always suspend on them   // JXXX maybe treat exclusive/available data differently
                // cleansing the pipeline and insert the request to the global FIFO
                CleansingAndInsert(req);
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// passive request handling
//////////////////////////////////////////////////////////////////////////

// network remote request to acquire token - invalidates/IV
void CacheL2TOK::OnAcquireTokenRem(ST_request* req)
{
	__address_t address = req->getreqaddress();

	cache_line_t* line = LocateLine(address);
    if (line == NULL)
    {
		// save the current request
        InsertPASNodeRequest(req);
		return;
    }

    // This is the very first plan, by providing tokens as much as requested, if possible
    if (!line->pending) // non-pending states   // S, E, O, M
    {
        // data is already available but needs to give token to the request

        assert(line->tokencount > 0);
        assert(req->tokenrequested > 0);
        assert(line->tlock == false);

        assert(line->gettokenglobalvisible() <= req->tokenrequested - req->tokenacquired);   // will need to clean up line

        // just_to_check
        assert(req->btransient == false);

        // update request
        UpdateRequest(req, line, req->type, address, true, req->bpriority||line->priority, req->btransient, req->tokenacquired+line->gettokenglobalvisible(), RUM_NON_MASK);    // ??? JONYXXX data availabe ?= true

        // update line
        line->tag = CacheTag(req->getlineaddress());
        line->time = sc_time_stamp();
        line->state = CLS_INVALID;
        line->tokencount = 0;
        line->pending = false;
        line->invalidated = false;
        line->priority = false;
        line->tlock = false;
        std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, false);

        // save the current request
        InsertPASNodeRequest(req);
    }
    else    // pending reqeust      // R, T, P, M, U
    {
        //assert(req->tokenrequested == GetTotalTokenNum());

        // the line must have less token than required, since request require all the tokens
        assert(line->tokencount <= req->tokenrequested - req->tokenacquired);

        if (line->state == CLS_SHARER)  // reading, before  // R, T
        {
            // get tokens if any. set invalidated flag
            // if the line already has the priority token then take the priority token as well.
            // no matter what, the line will be invalidated

            // make sure that when AT arrives with transient tokens, no tokens are in the line
            if (req->btransient)
                assert(line->tokencount == 0);

            // update request
            UpdateRequest(req, line, req->type, address, true, req->bpriority||line->priority, req->btransient, req->tokenacquired+line->gettokenglobalvisible());

            // update line  ??? no update?
            line->tag = CacheTag(req->getlineaddress());
            line->time = sc_time_stamp();
            line->tokencount = 0;
            line->invalidated = true;
            line->priority = false;
            line->tlock = false;

            for (unsigned int i = 0 ; i < g_nCacheLineSize; ++i)
            {
                if (req->IsRequestWithCompleteData() || req->bitmask[i])
                {
                    line->bitmask[i] = true;
                    line->data[i] = req->data[i];
                }
            }
        }
        else    // writing, before      // P, M, U
        {
            // 1. req has pt
            // req will get all the tokens
            if (req->bpriority)
            {
                assert(req->btransient == false);   // check the paper
                assert(line->priority == false); 

                unsigned int newtokenline=0;
                unsigned int newtokenreq=0;

                if (line->tlock)
                {
                    assert(line->invalidated);
                    // locked tokens are unlocked and released to the request
                    newtokenreq = req->tokenacquired + line->tokencount;
                    newtokenline = 0;
                    line->tlock = false;
                }
                else if (line->invalidated)
                {
                    newtokenreq = req->tokenacquired;
                    newtokenline = line->tokencount;
                }
                else
                {
                    newtokenreq = req->tokenacquired + line->gettokenglobalvisible();
                    newtokenline = 0;
                }

                // update request
                UpdateRequest(req, line, req->type, address, true, req->bpriority, req->btransient, newtokenreq);

                // update line, change the invalidated flag, but keep the token available
                //IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, true, line->priority, line->tlock, LUM_RAC_FEEDBACK_UPDATE);
                bool bupdatealldata = (line->state == CLS_OWNER && line->pending && line->tokencount == 0 && newtokenline > 0);

                line->time = sc_time_stamp();
                line->tokencount = newtokenline;
                line->invalidated = true;

                // update the cacheline with the dirty data in the request
                // start from the offset and update the size from the offset
                for (unsigned int i = 0; i < g_nCacheLineSize; i++)
                {
                    // if not all the data needs to be updated. then skip the unnecessary parts, only update if required
                    if (!req->bitmask[i])
                    {
                        if (!line->bitmask[i] && bupdatealldata)
                        {
                            line->data[i] = req->data[i];
                        }
                    }
                    // if the mask is already on there in the cache line then dont update
                    // otherwise update
                    else if (!line->bitmask[i] || bupdatealldata)
                    {
                        line->data[i] = req->data[i];
                        line->bitmask[i] = true;
                    }
                    //// we dont care about the cacheline bit mask, we just write the updated data.
                }
            }
            else if (line->priority)
            {
                assert(line->invalidated == false);
                assert(line->tlock == false);
                assert(req->bpriority == false);

                // transient tokens will be changed to permanent tokens
                req->btransient = false;

                unsigned int newtokenline = line->tokencount + req->tokenacquired;
                unsigned int newtokenreq = 0;

                // update request, rip the available token off to the request
                UpdateRequest(req, line, req->type, address, true, req->bpriority, req->btransient, newtokenreq, RUM_NON_MASK);

                // update line, and keep the state, and 
                //IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, line->invalidated, line->priority, line->tlock, LUM_RAC_FEEDBACK_UPDATE);
                bool bupdatealldata = (line->state == CLS_OWNER && line->pending && line->tokencount == 0 && newtokenline > 0);

                line->time = sc_time_stamp();
                line->tokencount = newtokenline;

                // update the cacheline with the dirty data in the request
                // start from the offset and update the size from the offset
                for (unsigned int i = 0; i < g_nCacheLineSize; i++)
                {
                    // if not all the data needs to be updated. then skip the unnecessary parts, only update if required
                    if (!req->bitmask[i])
                    {
                        if (!line->bitmask[i] && bupdatealldata)
                        {
                            line->data[i] = req->data[i];
                        }
                    }
                    // if the mask is already on there in the cache line then dont update
                    // otherwise update
                    else if (!line->bitmask[i] || bupdatealldata)
                    {
                        line->data[i] = req->data[i];
                        line->bitmask[i] = true;
                    }
                    //// we dont care about the cacheline bit mask, we just write the updated data.
                }
            }
            else
            {
                // both will get the same number of tokens, req will be at transient situation
                // and line will have the tokens locked
                // all of them are only visible locally, cannot be transfered.
                // transient tokens can later be transformed into permanent tokens by priority tokens in the line
                // locked tokesn can be unlocked by priority tokens
                // permament tokens can later by transfered or used remotely.
 
                assert(!req->bpriority);
                assert(!line->priority);

                unsigned int newtokenline = req->tokenacquired + line->tokencount;
                unsigned int newtokenreq = req->tokenacquired + line->gettokenglobalvisible();

                // update request, rip the available token off to the request
                UpdateRequest(req, line, req->type, address, true, req->bpriority, true, newtokenreq, RUM_NON_MASK);


                // update line, and keep the state, and 
                //IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, true, line->priority, true, LUM_RAC_FEEDBACK_UPDATE);
                bool bupdatealldata = (line->state == CLS_OWNER && line->pending && line->tokencount == 0 && newtokenline > 0);

                line->time = sc_time_stamp();
                line->tokencount = newtokenline;
                line->invalidated = true;
                line->tlock = true;

                // update the cacheline with the dirty data in the request
                // start from the offset and update the size from the offset
                for (unsigned int i = 0; i < g_nCacheLineSize; i++)
                {
                    // if not all the data needs to be updated. then skip the unnecessary parts, only update if required
                    if (!req->bitmask[i])
                    {
                        if (!line->bitmask[i] && bupdatealldata)
                        {
                            line->data[i] = req->data[i];
                        }
                    }
                    // if the mask is already on there in the cache line then dont update
                    // otherwise update
                    else if (!line->bitmask[i] || bupdatealldata)
                    {
                        line->data[i] = req->data[i];
                        line->bitmask[i] = true;
                    }
                    //// we dont care about the cacheline bit mask, we just write the updated data.
                }
            }
        }

        // save the current request
        InsertPASNodeRequest(req);
    }
}

// network return request to acquire token - IV return or DE
void CacheL2TOK::OnAcquireTokenRet(ST_request* req)
{
    __address_t address = req->getreqaddress();

    cache_line_t* line = LocateLine(address);
    assert(line != NULL);
    assert(line->pending);
    assert(line->state != CLS_SHARER);

    // check whether the line is already invalidated or not
    // or say, false sharing or races situation
    if (line->invalidated)
    {
        assert(!line->priority);
        if (req->bpriority)
        {
            // all locked tokens are unclocked
            assert(!req->btransient);
            assert(!line->priority);

            line->tlock = false;
            line->invalidated = false;
        }

        unsigned int newtokenline = line->gettokenglobalvisible() + req->gettokenpermanent();    // this can be zero
        unsigned int newtokenreq = 0;

        // continue_here
        UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

        if (newtokenline == 0)
        {
            // extra line invalidation method
            LineInvalidationExtra(req, false);
            // CHKS: assume no needs for the same update again. this might leave the value from another write
            line->tag = CacheTag(req->getlineaddress());
            line->time = sc_time_stamp();
            line->state = CLS_INVALID;
            line->tokencount = 0;
            line->pending = false;
            line->invalidated = false;
            line->priority = false;
            line->tlock = false;
            std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, false);
        }
        else
        {
            assert(newtokenline == GetTotalTokenNum());

            line->tag = CacheTag(req->getlineaddress());
            line->time = sc_time_stamp();
            line->state = CLS_OWNER;
            line->tokencount = newtokenline;
            line->pending = false;
            line->invalidated = false;
            line->priority = true;
            line->tlock = false;
        }

        // save reply request
        InsertSlaveReturnRequest(false, req);
    }
    else
    {
        // CHKS: double check the request and the line get all the tokens
        unsigned int tokennotify = (req->btransient) ? req->tokenacquired : 0;

        // the request can have transient request, 
        // in case during the false-sharing the current line has the priority token
        if (req->btransient)
        {
            assert(line->priority);

            // transfer the transient tokens
            req->btransient = false;

            if (tokennotify > 0)
            {
                ST_request *reqnotify = new ST_request(req);
                reqnotify->source = m_id;
                reqnotify->type = Request_LOCALDIR_NOTIFICATION;
                reqnotify->tokenacquired = tokennotify;

                InsertPASNodeRequest(reqnotify); 
            }
        }
        // resolve evicted lines short of data problem in directory configuration
        // need to resend the request again
        // REVISIT JXXX, maybe better solutions
        // JOYING or maybe just delay a little bit
        else if (line->tokencount + req->tokenacquired < GetTotalTokenNum())
        {
            req->bprocessed = true;

            // just send it again
            InsertPASNodeRequest(req);
            return;
        }

        req->bprocessed = false;

        assert((line->tokencount + req->gettokenpermanent()) == GetTotalTokenNum());
        assert(line->tlock == false);

        // CHKS: assume no needs for the same update again. this might leave the value from another write
        // update TAG
        line->tag = CacheTag(req->getlineaddress());
        line->time = sc_time_stamp();
        line->tokencount = line->gettokenglobalvisible() + req->gettokenpermanent();
        line->pending = false;
        line->invalidated = false;
        line->priority = true;
        line->tlock = false;

        UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

        // save reply request
        InsertSlaveReturnRequest(false, req);
    }

    OnPostAcquirePriorityToken(line, req);
}

// network remote request to acquire token and data     // RE, RS, SR, ER
void CacheL2TOK::OnAcquireTokenDataRem(ST_request* req)
{
	__address_t address = req->getreqaddress();

    assert(req->tokenrequested <= GetTotalTokenNum());

	// locate certain set
	cache_line_t* line = LocateLine(address);
    if (line == NULL)
    {
        InsertPASNodeRequest(req);
		return;
    }

    // this is the very first plan, by providing tokens as much as requested, if possible
    if (!line->pending) // non-pending states   // S, E, O, M
    {
        // data is already available but needs to give token to the request
        assert(line->tokencount > 0);
        assert(req->tokenrequested > 0);
        assert(line->tlock == false);
        assert(line->invalidated == false);

        if (line->gettokenglobalvisible() <= (req->tokenrequested - req->tokenacquired))   // line tokens are not enough; will need to clean up line
        {
            if (!req->btransient)
            {
                // if the request is read which requires only one token and the line has only one token, 
                // this may work only when total token number == cache number
                if ((req->tokenrequested == 1)&&(req->tokenacquired == 0))
                {
                    UpdateRequest(req, line, req->type, address, true, req->bpriority, req->btransient, req->tokenacquired);

                    // JOYING distinguish about modified data and normal data
                }
                else
                {
                    // extra line invalidation method
                    // JONY ALSERT
                    //LineInvalidationExtra(req, true);

                    // update request
                    UpdateRequest(req, line, req->type, address, true, req->bpriority||line->priority, req->btransient, req->tokenacquired+line->gettokenglobalvisible(), RUM_NON_MASK);

                    // update line
                    line->tag = CacheTag(req->getlineaddress());
                    line->time = sc_time_stamp();
                    line->state = CLS_INVALID;
                    line->tokencount = 0;
                    line->pending = false;
                    line->invalidated = false;
                    line->priority = false;
                    line->tlock = false;
                    std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, false);
                }
            }
            else
            {
                assert(line->priority == true);

                // extra line invalidation method
                //LineInvalidationExtra(req, true);
                // JONY ALSERT

                if (line->priority == true)
                {
                    req->btransient = false;
                }

                // update request
                UpdateRequest(req, line, req->type, address, true, req->bpriority||line->priority, req->btransient, req->tokenacquired+line->gettokenglobalvisible());

                // update line
                line->tag = CacheTag(req->getlineaddress());
                line->time = sc_time_stamp();
                line->state = CLS_INVALID;
                line->tokencount = 0;
                line->pending = false;
                line->invalidated = false;
                line->priority = false;
                line->tlock = false;
                std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, false);
            }
        }
        else    // only give out some tokens 
        {
            assert(req->btransient == false);

            unsigned int newlinetoken = line->gettokenglobalvisible() - req->tokenrequested + req->tokenacquired;

            // update request
            UpdateRequest(req, line, req->type, address, true, req->bpriority, req->btransient, req->tokenrequested);

            // update line  ??
            // check the update request and line data about the consistency !!!! XXXX JXXX !!!???
            line->tag = CacheTag(req->getlineaddress());
            line->time = sc_time_stamp();
            line->tokencount = newlinetoken;
        }

        // save the current request
        InsertPASNodeRequest(req);
    }
    else    // pending reqeust      // R, T, P, W, U
    {
        if (req->tokenrequested < GetTotalTokenNum())  // read  // RS, SR
        {
            if (line->state == CLS_SHARER)  // reading, before  // R, T
            {
                if (line->invalidated)  // T 
                {
                    assert(line->priority == false);
                    assert(line->tlock == false);
                    assert(req->btransient == false);
                    // the line has only ghost token for local use not anytoken can be acquired or invalidated

                    // get the data if available, and token if possible. otherwise go on

                    // CHKS: ALERT: some policy needs to be made to accelerate the process                 
                    unsigned int ntokenlinenew = line->tokencount;
                    unsigned int ntokentoacq = 0;

                    // update request
                    UpdateRequest(req, line, req->type, address, line->IsLineAtCompleteState() || req->dataavailable, req->bpriority, req->btransient, req->tokenacquired+ntokentoacq);

                    // update line  ??? no update?
                    line->tag = CacheTag(req->getlineaddress());
                    line->time = sc_time_stamp();
                    line->tokencount = ntokenlinenew;
                    
                    if (req->dataavailable && !line->IsLineAtCompleteState())
                    {
                        for (unsigned int i = 0; i < CACHE_BIT_MASK_WIDTH; ++i)
                        {
                            if (!line->bitmask[i])
                            {
                                line->data[i] = req->data[i];
                                line->bitmask[i] = true;
                            }
                        }
                    }
                }
                else    // R
                {
                    // get the data if available, and token if possible. otherwise go on
                    assert(req->btransient == false);
                    assert(line->tlock == false);

                    int ntokentoacqmore = 0;
                    unsigned int ntokenlinenew = 0;

                    // CHKS: ALERT: JONYX some policy needs to be made to accelerate the process
                    assert(req->tokenrequested > 0);
                    if (req->tokenacquired > req->tokenrequested)
                    {
                        ntokentoacqmore = (line->tokencount>0)?0:-1;
                        ntokenlinenew = (line->tokencount>0)?line->tokencount:1;
                    }
                    else if (req->tokenacquired == req->tokenrequested)
                    {
                        ntokentoacqmore = 0;
                        ntokenlinenew = line->tokencount;
                    }
                    else
                    {
                        ntokentoacqmore = (line->tokencount>1)?1:0;
                        ntokenlinenew = (line->tokencount>1)?(line->tokencount-1):line->tokencount;
                    }

                    // update request
                    UpdateRequest(req, line, req->type, address, (req->dataavailable||(line->IsLineAtCompleteState()?true:false)), req->bpriority, req->btransient, req->tokenacquired+ntokentoacqmore, (line->IsLineAtCompleteState()?RUM_ALL:RUM_NONE));

                    line->tag = CacheTag(req->getlineaddress());
                    line->time = sc_time_stamp();
                    line->tokencount = ntokenlinenew;

                    if (req->dataavailable && !line->IsLineAtCompleteState())
                    {
                        for (unsigned int i = 0; i < CACHE_BIT_MASK_WIDTH; ++i)
                        {
                            if (!line->bitmask[i])
                            {
                                line->data[i] = req->data[i];
                                line->bitmask[i] = true;
                            }
                        }
                    }
                }
            }
            else    // writing, before, // P, W, U
            {
                unsigned int newtokenline=0;
                unsigned int newtokenreq=0;

                assert(req->btransient == false);

                bool acquiredp = false;
                bool oldpriority = line->priority;

                if (line->invalidated) // cool stuff here
                {
                    assert(line->priority == false);

                    // the reqeust might have the priority, 
                    // in this case all locked tokens in the line will be unlocked
                    // the line should be un-invalidated and get all the tokens

                    if (req->bpriority)
                    {
                        // mkae the req transfer the priority token to the line
                        // get rid of the invalidated flag
                        // no lines are locked
                        line->priority = true;
                        req->bpriority = false;

                        newtokenline = req->gettokenpermanent();
                        if (line->tlock)
                        {
                            newtokenline += line->tokencount;
                        }
                        newtokenreq = 0;

                        line->invalidated = false;
                        line->tlock = false;
                        acquiredp = true;
                    }
                    else
                    {
                        // there willl be nothing to lose in this case
                        assert(req->tokenacquired == 0);        // label_tokenacquired_always_zero
                        assert(req->btransient == false);
                        newtokenline = req->tokenacquired + line->tokencount;
                        newtokenreq = 0;
                    }
                }
                else
                {
                    // the line will get all the tokens anyway
                    
                    newtokenline = line->tokencount + req->gettokenpermanent();
                    newtokenreq = 0;
                    line->priority = line->priority||req->bpriority;
                    req->bpriority = false;
                }

                if (!oldpriority && line->priority)
                    acquiredp = true;

                // get the data if available, and no token will be granted.
                // update request, get more token or get own tokens ripped off
                UpdateRequest(req, line, req->type, address, (req->dataavailable||(line->IsLineAtCompleteState())?true:false), req->bpriority, req->btransient, newtokenreq, (line->IsLineAtCompleteState()?RUM_ALL:RUM_NONE));
                assert(req->bpriority == false);
                assert(req->btransient == false);

                // update line  ??? no update?
                if (line->tlock)
                    assert(newtokenline == line->tokencount);

                line->tag = CacheTag(req->getlineaddress());
                line->time = sc_time_stamp();
                line->tokencount = newtokenline;

                if (req->dataavailable && !line->IsLineAtCompleteState())
                {
                    for (unsigned int i = 0; i < CACHE_BIT_MASK_WIDTH; ++i)
                    {
                        if (!line->bitmask[i])
                        {
                            line->data[i] = req->data[i];
                            line->bitmask[i] = true;
                        }
                    }
                }

                if (acquiredp)
                    OnPostAcquirePriorityToken(line, req);
            }

            // save the current request
            InsertPASNodeRequest(req);
        }
        else    // write        // RE, ER
        {
            unsigned int newtokenline=0;
            unsigned int newtokenreq = 0;

            // the line must have less token than required, since request require all the thokens
            assert(line->tokencount <= (req->tokenrequested - req->tokenacquired));

            if (line->state == CLS_SHARER)  // reading, before  // R, T
            {
                assert(!line->tlock);

                // get tokens if any. set invalidated flag
                if (req->btransient)
                {
                    // in this case the line must be already invalidated, 
                    // so the tokens can be acquired here is absolutely 0
                    assert(line->invalidated);
                }

                // the gettokenpermanent in the case above will return 0
                newtokenreq = req->gettokenpermanent() + line->tokencount;
                newtokenline = 0;
                //newtokenline = line->tokencount;
                line->invalidated = true;

                // update request
                UpdateRequest(req, line, req->type, address, true, req->bpriority||line->priority, req->btransient, newtokenreq, (line->IsLineAtCompleteState()?RUM_ALL:RUM_NON_MASK));

                // update line
                line->tag = CacheTag(req->getlineaddress());
                line->time = sc_time_stamp();
                line->tokencount = newtokenline;
                line->invalidated = true;
                line->priority = false;

                for (unsigned int i = 0 ; i < g_nCacheLineSize; ++i)
                {
                    if (req->IsRequestWithCompleteData() || req->bitmask[i])
                    {
                        line->bitmask[i] = true;
                        line->data[i] = req->data[i];
                    }
                }
            }
            else    // writing, before      // P, W, U
            {
                // 1. req has priority token
                // req will get all the tokens
                if (req->bpriority)
                {
                    assert(req->btransient == false);
                    assert(line->priority == false);

                    if (line->tlock)
                    {
                        assert(line->invalidated);

                        // locked tokens are unlocked and transfered to request
                        newtokenreq = req->tokenacquired + line->tokencount;
                        newtokenline = 0;
                        line->tlock = false;
                    }
                    else if (line->invalidated)
                    {
                        assert(line->gettokenglobalvisible() == 0);
                        newtokenreq = req->tokenacquired + line->gettokenglobalvisible();
                        newtokenline = 0;
                    }
                    else
                    {
                        newtokenreq = req->tokenacquired + line->gettokenglobalvisible();
                        newtokenline = 0;
                    }

                    // Update request
                    UpdateRequest(req, line, req->type, address, true, req->bpriority, req->btransient, newtokenreq, RUM_NON_MASK);

                    // updateline
                    //IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, true, line->priority, line->tlock, LUM_RAC_FEEDBACK_UPDATE);
                    bool bupdatealldata = (line->state == CLS_OWNER && line->pending && line->tokencount == 0 && newtokenline > 0);

                    line->time = sc_time_stamp();
                    line->tokencount = newtokenline;
                    line->invalidated = true;

                    // update the cacheline with the dirty data in the request
                    // start from the offset and update the size from the offset
                    for (unsigned int i = 0; i < g_nCacheLineSize; i++)
                    {
                        // if not all the data needs to be updated. then skip the unnecessary parts, only update if required
                        if (!req->bitmask[i])
                        {
                            if (!line->bitmask[i] && bupdatealldata)
                            {
                                line->data[i] = req->data[i];
                            }
                        }
                        // if the mask is already on there in the cache line then dont update
                        // otherwise update
                        else if (!line->bitmask[i] || bupdatealldata)
                        {
                            line->data[i] = req->data[i];
                            line->bitmask[i] = true;
                        }
                        //// we dont care about the cacheline bit mask, we just write the updated data.
                    }
                }
                // 2. line has the priority, then the line will take all 
                else if (line->priority)
                {
                    assert(line->invalidated == false);
                    assert(line->tlock == false);
                    assert(req->bpriority == false);

                    // transient tokens will be changed to permanent tokens
                    req->btransient = false;

                    newtokenline = line->tokencount + req->tokenacquired;
                    newtokenreq = 0;

                    // update request, rip the available token off to the request
                    UpdateRequest(req, line, req->type, address, true, req->bpriority, req->btransient, newtokenreq, RUM_NON_MASK);

                    // update line, and keep the state, and 
                    //IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, line->invalidated, line->priority, line->tlock, LUM_RAC_FEEDBACK_UPDATE);
                    bool bupdatealldata = (line->state == CLS_OWNER && line->pending && line->tokencount == 0 && newtokenline > 0);

                    line->time = sc_time_stamp();
                    line->tokencount = newtokenline;

                    // update the cacheline with the dirty data in the request
                    // start from the offset and update the size from the offset
                    for (unsigned int i = 0; i < g_nCacheLineSize; i++)
                    {
                        // if not all the data needs to be updated. then skip the unnecessary parts, only update if required
                        if (!req->bitmask[i])
                        {
                            if (!line->bitmask[i] && bupdatealldata)
                            {
                                line->data[i] = req->data[i];
                            }
                        }
                        // if the mask is already on there in the cache line then dont update
                        // otherwise update
                        else if (!line->bitmask[i] || bupdatealldata)
                        {
                            line->data[i] = req->data[i];
                            line->bitmask[i] = true;
                        }
                        //// we dont care about the cacheline bit mask, we just write the updated data.
                    }
                }
                else
                {
                    // both will get the same number of tokens, req will be at transient situation
                    // and line will have the tokens locked
                    // all of them are only visible locally, cannot be transfered.
                    // transient tokens can later be transformed into permanent tokens by priority tokens in the line
                    // locked tokesn can be unlocked by priority tokens
                    // permament tokens can later by transfered or used remotely.
     
                    assert(req->bpriority == false);
                    assert(line->priority == false);
                    newtokenline = req->tokenacquired + line->tokencount;
                    newtokenreq = req->tokenacquired + line->tokencount;

                    // update request, rip the available token off to the request
                    UpdateRequest(req, line, req->type, address, true, req->bpriority, true, newtokenreq, RUM_NON_MASK);

                    // update line, and keep the state, and 
                    //IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, true, line->priority, true, LUM_RAC_FEEDBACK_UPDATE);
                    bool bupdatealldata = (line->state == CLS_OWNER && line->pending && line->tokencount == 0 && newtokenline > 0);

                    line->time = sc_time_stamp();
                    line->tokencount = newtokenline;
                    line->invalidated = true;
                    line->tlock = true;

                    // update the cacheline with the dirty data in the request
                    // start from the offset and update the size from the offset
                    for (unsigned int i = 0; i < g_nCacheLineSize; i++)
                    {
                        // if not all the data needs to be updated. then skip the unnecessary parts, only update if required
                        if (!req->bitmask[i])
                        {
                            if (!line->bitmask[i] && bupdatealldata)
                            {
                                line->data[i] = req->data[i];
                            }
                        }
                        // if the mask is already on there in the cache line then dont update
                        // otherwise update
                        else if (!line->bitmask[i] || bupdatealldata)
                        {
                            line->data[i] = req->data[i];
                            line->bitmask[i] = true;
                        }
                        //// we dont care about the cacheline bit mask, we just write the updated data.
                    }
                }

            }

            // save the current request
            InsertPASNodeRequest(req);
            //m_pReqCurPASasNode = req;
        }
    }
}

// network request return, with token and data  // RS, SR, RE, ER
void CacheL2TOK::OnAcquireTokenDataRet(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate certain set
    cache_line_t* line = LocateLine(address);
    assert(line != NULL);
    assert(line->state != CLS_INVALID);
    assert(line->pending);     // non-pending states   // S, E, O, M

        // pending states       // R, T, P, U, W
    {
        if (req->tokenrequested < GetTotalTokenNum())   // read, // RS, SR
        {
            if (line->state == CLS_SHARER)  // reading, before  // R, T
            {
                // resolve evicted lines short of data problem in directory configuration
                // need to resend the request again
                // REVISIT JXXX, maybe better solutions
                if (!req->dataavailable && !line->IsLineAtCompleteState())
                {
                    req->bprocessed = true;

                    // just send it again
                    InsertPASNodeRequest(req);
                    return;
                }

                req->bprocessed = false;

                assert(req->btransient == false);
                unsigned int tokenlinenew = line->tokencount + req->tokenacquired;
                bool newlinepriority = line->priority || req->bpriority;

                bool evictthereturnedtokens = 0;
                if (line->invalidated && !req->btransient && req->tokenacquired > 0)
                {
                    evictthereturnedtokens = req->tokenacquired;
                }

                // instead of updating the cache line, the reqeust should be updated first
                // update request from the line
                //UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY_X, address);
                UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY, address, true, false, false, 0xffff, (line->IsLineAtCompleteState()?RUM_ALL:RUM_NONE));

                // update time and state
                if ((line->invalidated)||(tokenlinenew == 0))
                {
                    //assert(tokenlinenew == 0);
                    line->tag = CacheTag(req->getlineaddress());
                    line->time = sc_time_stamp();
                    line->state = CLS_INVALID;
                    line->tokencount = 0;
                    line->pending = false;
                    line->invalidated = false;
                    line->priority = false;
                    line->tlock = false;
                    // TOFIX JXXX backward invalidate L1 caches.

                    // in multi-level configuration, there's a chance that an AD request could travel twice(first time reload), and return at invalidated state with non-transient tokens. 
                    // in this case, the non-transient tokens should be dispatched again with disseminate request
                    if (evictthereturnedtokens > 0)
                    {
                        ST_request *reqevresend = new ST_request(req);
                        reqevresend->type = REQUEST_DISSEMINATE_TOKEN_DATA;
                        reqevresend->addresspre = req->addresspre;
                        reqevresend->offset = 0;
                        reqevresend->nsize = g_nCacheLineSize;
                        reqevresend->Conform2BitVecFormat();
                        reqevresend->dataavailable = true;
                        memcpy(reqevresend->data, req->data, g_nCacheLineSize);
                        reqevresend->source = m_id;

                        reqevresend->tokenacquired = evictthereturnedtokens;
                        if (evictthereturnedtokens==GetTotalTokenNum())
                        {
                            reqevresend->tokenrequested = GetTotalTokenNum();
                            reqevresend->bpriority = true;
                        }
                        else
                        {
                            reqevresend->tokenrequested = 0;
                            reqevresend->bpriority = false;
                        }

                        InsertPASNodeRequest(reqevresend); 
                    }
                }
                else if (line->tokencount == 0 || !line->IsLineAtCompleteState())
                {
                    assert(!line->breserved);   // just alert, may not be error
                    line->tag = CacheTag(req->getlineaddress());
                    line->time = sc_time_stamp();
                    line->tokencount = tokenlinenew;
                    line->pending = false;
                    line->invalidated = false;
                    line->priority = newlinepriority;
                    line->tlock = false;

                    for (unsigned int i = 0 ; i < g_nCacheLineSize; ++i)
                    {
                        if (req->IsRequestWithCompleteData() || req->bitmask[i])
                        {
                            line->bitmask[i] = true;
                            line->data[i] = req->data[i];
                        }
                    }
                }
                else
                {
                    line->tag = CacheTag(req->getlineaddress());
                    line->time = sc_time_stamp();
                    if (line->breserved)
                    {
                        line->state = CLS_OWNER;
                    }
                    line->tokencount = tokenlinenew;
                    line->pending = false;
                    line->invalidated = false;
                    line->priority = newlinepriority;
                    line->tlock = false;
                }

                line->breserved = false;

                OnPostAcquirePriorityToken(line, req);

                // save reply request
                InsertSlaveReturnRequest(false, req);
                //m_pReqCurPASasSlave = req;

            }
            else    // writing, before  // P, U, W
            {
                // just collect the tokens, 
                // it must be because an EV request dispatched a owner-ev to the R,T line
                assert(line->priority);
                assert(line->IsLineAtCompleteState());
                assert(req->btransient == false);
                assert(!req->bpriority);

                unsigned int tokenlinenew = line->tokencount + req->tokenacquired;
                bool newlinepriority = line->priority || req->bpriority;


                line->tag = CacheTag(req->getlineaddress());
                line->time = sc_time_stamp();
                line->tokencount = tokenlinenew;
                line->priority = newlinepriority;
                line->tlock = false;
                                                        
                // instead of updating the cache line, the reqeust should be updated first
                // update request from the line
                //UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY_X, address);
                UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY, address, true, false, false, 0xffff, (line->IsLineAtCompleteState()?RUM_ALL:RUM_NONE));

                line->breserved = false;

                //OnPostAcquirePriorityToken(line, req);

                // save reply request
                InsertSlaveReturnRequest(false, req);
                //m_pReqCurPASasSlave = req;

            }
        }
        else    // write, // RE, ER, (or maybe RS, SR when GetTotalTokenNum() == 1)
        {
	  
	        if (line->state == CLS_SHARER)  // actually reading, before  // R, T
            {
	            assert(GetTotalTokenNum() == 1);
                {
                    // line is shared but also exclusive (not dirty not owner)
                   
                    assert(!line->invalidated); 
                    assert(line->tlock == false);
                    assert(req->btransient == false);

                    unsigned int tokenlinenew = line->tokencount + req->tokenacquired;

                    // update time and state
		            assert (line->tokencount == 0);
                    line->tag = CacheTag(req->getlineaddress());
                    line->time = sc_time_stamp();
                    line->state = line->state;
                    line->tokencount = tokenlinenew;
                    line->pending = false;
                    line->invalidated = false;
                    line->priority = true;
                    line->tlock = false;

                    for (unsigned int i = 0 ; i < g_nCacheLineSize; ++i)
                    {
                        if (req->IsRequestWithCompleteData() || req->bitmask[i])
                        {
                            line->bitmask[i] = true;
                            line->data[i] = req->data[i];
                        }
                    }

                    // instead of updating the cache line, the reqeust should be updated first
                    // update request from the line
                    //UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY_X, address);
                    UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY, address, true, false, false, 0xffff);       // ALERT

                    // save reply request
                    InsertSlaveReturnRequest(false, req);
                    //m_pReqCurPASasSlave = req;

                    OnPostAcquirePriorityToken(line, req);
                }
            }
            else    // writing, before  // P, U, W
            {
                unsigned int newtokenline=0;
                unsigned int newtokenreq=0;

                unsigned int tokennotify = (req->btransient) ? req->tokenacquired : 0;

                 // check whether the line is already invalidated or not
                // or say, false sharing or races situation
                if (line->invalidated) // U state
                {
                    assert(!line->priority);
                    if (req->bpriority)
                    {
                        // all locked tokens are unclocked
                        assert(!req->btransient);
                        assert(!line->priority);

                        line->tlock = false;
                        line->invalidated = false;
                    }

                    newtokenline = line->gettokenglobalvisible() + req->gettokenpermanent();    // this can be zero
                    newtokenreq = 0;


                    // continue_here
                    if (newtokenline == 0)
                    {
                        UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

                        // extra line invalidation method
                        LineInvalidationExtra(req, false);
                        // CHKS: assume no needs for the same update again. this might leave the value from another write
                        line->tag = CacheTag(req->getlineaddress());
                        line->time = sc_time_stamp();
                        line->state = CLS_INVALID;;
                        line->tokencount = 0;
                        line->pending = false;
                        line->invalidated = false;
                        line->priority = false;
                        line->tlock = false;
                    }
                    else
                    {
                        assert(newtokenline == GetTotalTokenNum());
                        
                        if (line->tokencount == 0)
                        {
                            for (unsigned int i = 0 ; i < g_nCacheLineSize; ++i)
                            {
                                if (req->IsRequestWithCompleteData() || req->bitmask[i])
                                {
                                    line->bitmask[i] = true;
                                    line->data[i] = req->data[i];
                                }
                            }
                        }

                        line->tag = CacheTag(req->getlineaddress());
                        line->time = sc_time_stamp();
                        line->state = CLS_OWNER;
                        line->tokencount = newtokenline;
                        line->pending = false;
                        line->invalidated = false;
                        line->priority = true;
                        line->tlock = false;
                        
                        UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);
                     }

                    // save reply request
                    if (req->bmerged)
                    {
                        assert(req->msbcopy != NULL);
                        vector<ST_request*>* pvec = req->msbcopy;
                        for (unsigned int i=0;i<pvec->size();i++)
                        {
                            InsertSlaveReturnRequest(false, (*pvec)[i]);
                        }

                        pvec->clear();
                        delete pvec;

                        OnPostAcquirePriorityToken(line, req);

                        delete req;
                    }
                    else
                    {
                        InsertSlaveReturnRequest(false, req);

                        OnPostAcquirePriorityToken(line, req);
                    }
                    //m_pReqCurPASasSlave = req;
                }
                else
                {
                    // resolve evicted lines short of data problem in directory configuration
                    // need to resend the request again
                    // REVISIT JXXX, maybe better solutions
                    // JOYING or maybe just delay a little bit
                    if (line->tokencount + req->tokenacquired < GetTotalTokenNum())
                    {
                        req->bprocessed = true;

                        // just send it again
                        InsertPASNodeRequest(req);
                        return;
                    }

                    req->bprocessed = false;

                    // double check the request and the line get all the tokens
                    assert((line->tokencount + req->tokenacquired) == GetTotalTokenNum());
                    assert(line->tlock == false);
                    if (req->btransient)
                        assert(line->priority);

                    if (line->tokencount == 0)
                    {
                        for (unsigned int i = 0 ; i < g_nCacheLineSize; ++i)
                        {
                            if (req->IsRequestWithCompleteData() || req->bitmask[i])
                            {
                                line->bitmask[i] = true;
                                line->data[i] = req->data[i];
                            }
                        }
                    }
                    
                    line->tag = CacheTag(req->getlineaddress());
                    line->time = sc_time_stamp();
                    line->tokencount += req->tokenacquired;
                    line->pending = false;
                    line->invalidated = false;
                    line->priority = true;
                    line->tlock = false;

                    // save reply request
                    UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

                    req->type = REQUEST_WRITE_REPLY;

                    if (req->bmerged)
                    {
                        assert(req->msbcopy != NULL);
                        vector<ST_request*>* pvec = req->msbcopy;
                        for (unsigned int i=0;i<pvec->size();i++)
                        {
                            InsertSlaveReturnRequest(false, (*pvec)[i]);
                        }

                        pvec->clear();
                        delete pvec;

                        OnPostAcquirePriorityToken(line, req);

                        delete req;
                    }
                    else
                    {
                        InsertSlaveReturnRequest(false, req);

                        OnPostAcquirePriorityToken(line, req);
                    }
                }
            }
        }
    }
}

// network disseminate token and data, EV, WB, include IJ
void CacheL2TOK::OnDisseminateTokenData(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate line
    cache_line_t* line = LocateLine(address);

    // handle INVALID state
    if (line == NULL)
    {
        switch (m_nInjectionPolicy)
        {
        case IP_NONE:
            // pass the transaction down 
            // this can change according to different strategies
            // JXXX

            // save the current request
            InsertPASNodeRequest(req);

            // request will be passed to the next node
            break;
            
        case IP_EMPTY_1EJ:
            line = GetEmptyLine(address);
            if (line == NULL)
            {
                // save the current request
                InsertPASNodeRequest(req);
            }
            else
            {
                // update line info

                line->tag = CacheTag(req->getlineaddress());
                line->time = sc_time_stamp();
                line->state = (req->tokenrequested == GetTotalTokenNum()) ? CLS_OWNER : CLS_SHARER;
                line->tokencount = req->tokenacquired;
                line->pending = false;
                line->invalidated = false;
                line->priority = req->bpriority;
                line->tlock = false;

                for (unsigned int i = 0; i < CACHE_BIT_MASK_WIDTH; i++)
                {
                    if (!line->bitmask[i])
                    {
                        line->data[i] = req->data[i];
                    }
                }
                std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, true);

                // terminate request
                delete req;
            }
            break;
            
        default:
	        // invalid case
	        assert(false);
	        break;
        }
        return;
    }

    // handling other states
    assert(line->state != CLS_INVALID);

    if (!line->pending)     // non-pending states   // S, E, O, M
    {
        // give the token of the request to the line 
        // new policy possible JXXX
        //unsigned int tokenlinenew = line->tokencount + req->tokenacquired;

        // JXXX policy maybe changed 
        bool acquiredp = false;
        if ((req->bpriority)&&(!line->priority))
            acquiredp = true;

        assert(line->tlock == false);
        line->tag = CacheTag(req->getlineaddress());
        line->time = sc_time_stamp();
        if (req->tokenrequested == GetTotalTokenNum())
        {
            line->state = CLS_OWNER;
        }
        line->tokencount += req->tokenacquired;
        if (req->bpriority)
        {
            line->priority = true;
        }

        if (acquiredp)
            OnPostAcquirePriorityToken(line, req);

        // terminate the request
        delete req;

        // do not send anything away anymore
    }
    else    // pending states       // R, T, P, U, W
    {
        if (line->invalidated)      // T, U
        {
            // do not give the tokens to the T line, but U will decide whether the line should stay
            // the situation will never happen, check the label : label_tokenacquired_always_zero
            //
            // [the original line sent out the DD should have already had been invalidated if the line is U]
            // or [the DD will met a non-invalidated line first, as P, W]
 
	        assert (line->state != CLS_OWNER);

            InsertPASNodeRequest(req);
        }
        else                        // R, P, W
        {
            if (line->state == CLS_SHARER)  // reading, before  // R
            {
                // give all the tokens of the request to the line
                unsigned int tokenlinenew = line->tokencount + req->tokenacquired;

                assert(tokenlinenew <= GetTotalTokenNum());

                assert(line->tlock == false);

                bool acquiredp = false;
                if ((req->bpriority)&&(!line->priority))
                    acquiredp = true;

                if (line->tokencount == 0)
                {
                    for (unsigned int i = 0; i < CACHE_BIT_MASK_WIDTH; i++)
                    {
                        if (!line->bitmask[i])
                        {
                            line->data[i] = req->data[i];
                        }
                    }
                    std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, true);
                }

                line->tag = CacheTag(req->getlineaddress());
                line->time = sc_time_stamp();
                line->tokencount = tokenlinenew;
                if (req->bpriority)
                {
                    line->priority = true;
                }

                // special case to reserve the line to be trasfered to owner line after reply received.
                // RESERVED
                if (req->tokenrequested == GetTotalTokenNum())
                    line->breserved = true;

                if (acquiredp)
                    OnPostAcquirePriorityToken(line, req);

                // JXXX policy maybe changed 

                // terminate the request
                delete req;

                // do not send anything away anymore
            }
            else    // writing, before  // P, W
            {
                assert(line->tlock == false);
                // give all the tokens of the request to the line
                unsigned int tokenlinenew = line->tokencount + req->tokenacquired;

                assert(tokenlinenew<=GetTotalTokenNum());

                bool acquiredp = false;
                if ((req->bpriority)&&(!line->priority))
                    acquiredp = true;

                if (line->tokencount == 0)
                {
                    for (unsigned int i = 0; i < CACHE_BIT_MASK_WIDTH; i++)
                    {
                        if (!line->bitmask[i])
                        {
                            line->data[i] = req->data[i];
                        }
                    }
                    std::fill(line->bitmask, line->bitmask + CACHE_BIT_MASK_WIDTH, true);
                }

                line->tag = CacheTag(req->getlineaddress());
                line->time = sc_time_stamp();
                line->tokencount = tokenlinenew;
                if (req->bpriority)
                {
                    line->priority = true;
                }

                if (acquiredp)
                    OnPostAcquirePriorityToken(line, req);

                // JXXX policy maybe changed 
                // terminate the request
                delete req;

                // do not send anything away anymore
            }
        }
    }
}


void CacheL2TOK::OnPostAcquirePriorityToken(cache_line_t* line, ST_request* req)
{
    __address_t address = req->getreqaddress();
    
    if (!m_msbModule.IsAddressPresent(address))
    {
        return;
    }
    
    ///////////////////////////
    // deal with merged request 
    ST_request* mergedreq = m_msbModule.GetMergedRequest(address);
    ST_request* merge2send = NULL;

    // queued request list
    vector<ST_request*>* queuedrequests = m_msbModule.GetQueuedRequestVector(address);

    assert(line->invalidated == false);

    // if the merged request can be handled directly. 
    if (line->state == CLS_OWNER)
    {
        assert(line->tokencount != 0);
        if (!line->pending)
        {
            assert(line->tokencount == GetTotalTokenNum());

            // then carry out the request 
            line->tag = CacheTag(mergedreq->getlineaddress());
            line->time = sc_time_stamp();
            for (unsigned int i = 0; i < g_nCacheLineSize; i++)
            {
                if (mergedreq->IsRequestWithCompleteData() || mergedreq->bitmask[i])
                {
                    line->bitmask[i] = true;
                    line->data[i] = mergedreq->data[i];
                }
            }

            // and append all the queued request on the msb slot to a return buffer
            for (unsigned int i = 0; i < queuedrequests->size(); i++)
            {
                InsertSlaveReturnRequest(false, (*queuedrequests)[i]);
            }

            // remove the merged request, remove the msb slot
            m_msbModule.CleanSlot(address);
        }
        else
        {
            // CHKS, alert
            // the line might not be able to write directly if they are from differnt families. 
            line->tag = CacheTag(mergedreq->getlineaddress());
            line->time = sc_time_stamp();
            for (unsigned int i = 0; i < g_nCacheLineSize; i++)
            {
                if (mergedreq->IsRequestWithCompleteData() || mergedreq->bitmask[i])
                {
                    line->bitmask[i] = true;
                    line->data[i] = mergedreq->data[i];
                }
            }

            // and append all the queued request on the msb slot to a return buffer
            for (unsigned int i = 0; i < queuedrequests->size(); i++)
            {
                InsertSlaveReturnRequest(false, (*queuedrequests)[i]);
            }

            // remove the merged request, remove the msb slot
            m_msbModule.CleanSlot(address);
        }
    }
    else if (line->state == CLS_SHARER)
    {
        if (line->tokencount != GetTotalTokenNum())
        {
            assert(!line->pending);
            assert(!line->invalidated);
            assert(!line->tlock);

            // now read reply already received, go out and acquire the rest of the tokens
            line->tag = CacheTag(mergedreq->getlineaddress());
            line->time = sc_time_stamp();
            line->state = CLS_OWNER;
            line->pending = true;
            line->invalidated = false;
            line->tlock = false;
            for (unsigned int i = 0; i < g_nCacheLineSize; i++)
            {
                if (mergedreq->IsRequestWithCompleteData() || mergedreq->bitmask[i])
                {
                    line->bitmask[i] = true;
                    line->data[i] = mergedreq->data[i];
                }
            }

            merge2send = new ST_request(mergedreq);
            merge2send->source = m_id;

            // associate the merged request with a request queue
            // dumplicate request queue
            m_msbModule.DuplicateRequestQueue(address, merge2send);

            // remove the msb slot
            m_msbModule.CleanSlot(address);


        }
        else
        {
            assert(line->pending == false);

            // in case the merged request cannot be handled directly, 
            line->tag = CacheTag(mergedreq->getlineaddress());
            line->time = sc_time_stamp();
            line->state = CLS_OWNER;
            line->pending = true;
            for (unsigned int i = 0; i < g_nCacheLineSize; i++)
            {
                if (mergedreq->IsRequestWithCompleteData() || mergedreq->bitmask[i])
                {
                    line->bitmask[i] = true;
                    line->data[i] = mergedreq->data[i];
                }
            }

            // then send it from the network interface
            // Duplicate the merged request

            merge2send = new ST_request(mergedreq);
            merge2send->source = m_id;

            // associate the merged request with a request queue
            // dumplicate request queue
            m_msbModule.DuplicateRequestQueue(address, merge2send);

            // remove the msb slot
            m_msbModule.CleanSlot(address);
        }

    }
    else
    {
        assert(line->tokencount == 0);

        line->tag = CacheTag(mergedreq->getlineaddress());
        line->time = sc_time_stamp();
        line->state = CLS_OWNER;
        line->pending = true;
        line->invalidated = false;
        line->priority = false;
        line->tlock = false;
        for (unsigned int i = 0; i < g_nCacheLineSize; i++)
        {
            if (mergedreq->IsRequestWithCompleteData() || mergedreq->bitmask[i])
            {
                line->bitmask[i] = true;
                line->data[i] = mergedreq->data[i];
            }
        }

        merge2send = new ST_request(mergedreq);
        merge2send->source = m_id;

        // associate the merged request with a request queue
        // dumplicate request queue
        m_msbModule.DuplicateRequestQueue(address, merge2send);

        // remove the msb slot
        m_msbModule.CleanSlot(address);
    }

    // send request if any
    if (merge2send != NULL)
    {
        InsertPASNodeRequest(merge2send);
    }
    // when the merged request comes back. the queue will be added to the return buffer
    // in the middle other reuqest can still be queued in msb or queue to the line
}

void CacheL2TOK::OnDirNotification(ST_request* req)
{
    // request meant for directory, shouldn't receive it again
    assert (req->source != m_id);
    // just pass it by
    InsertPASNodeRequest(req);
    //m_pReqCurPASasNode = req;
}

// function for find replacement line
cache_line_t* CacheL2TOK::GetReplacementLine(__address_t address)
{
    cache_line_t *linelruw = NULL; // replacement line for write-back request
    cache_line_t *linelrue = NULL; // replacement line for eviction request
    unsigned int index = CacheIndex(address);

    for (unsigned int i = 0; i < m_assoc; i++)
    {
        cache_line_t& line = m_sets[index].lines[i];
        
        // Return the first found empty one
        if (line.state == CLS_INVALID)
            return &line;

        // Pending lines don't count as normal replacable lines
        if (!line.pending)
        {
            if (line.state == CLS_SHARER)
            {
                if (linelrue == NULL || line.time < linelrue->time)
                    linelrue = &line;
            }
            else if (line.state == CLS_OWNER)
            {
                if (linelruw == NULL || line.time < linelruw->time)
                    linelruw = &line;
            }
        }
    }
    return (linelrue != NULL) ? linelrue : linelruw;
}
