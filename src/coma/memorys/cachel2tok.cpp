#include "cachel2tok.h"
#include "../simlink/linkmgs.h"
using namespace std;


namespace MemSim
{
      unsigned int CacheL2TOK::s_nGlobalFIFOUpperMargin = 1;    // UPPER Margin
const unsigned int CacheL2TOK::s_nGlobalFIFOLowerMargin = 0x100;    // LOWER Margin

// process an initiative request from processor side
void CacheL2TOK::ProcessInitiative()
{
    // handle request
    // the requests arrive here should be only local read and local write
    switch (m_pReqCurINI->type)
    {
    case Message::READ:
        OnLocalRead(m_pReqCurINI);
        break;
    
    case Message::WRITE:
        OnLocalWrite(m_pReqCurINI);
        break;

    default:
        break;
    }
}

bool CacheL2TOK::SendAsSlave(Message* req)
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

void CacheL2TOK::SendFromINI()
{
    if (m_pReqCurINIasSlaveX == NULL)
    {
        m_pReqCurINIasSlaveX = GetSlaveReturnRequest(true);
    }
    
    if (m_pReqCurINIasNodeDB != NULL)
    {
        // reset the queued property
        m_pReqCurINIasNodeDB->bqueued = false;

        // send request to memory
        if (SendRequest(m_pReqCurINIasNodeDB))
        {
            m_pReqCurINIasNodeDB = NULL;
        }
    }
    else if (m_pReqCurINIasNode != NULL)
    {
        // reset the queued property
        m_pReqCurINIasNode->bqueued = false;

        // send request to memory
        if (SendRequest(m_pReqCurINIasNode))
        {
            m_pReqCurINIasNode = NULL;
        }
    }

    if (m_pReqCurINIasSlaveX != NULL)
    {
        // reset the queued property
        m_pReqCurINIasSlaveX->bqueued = false;

        // send reply transaction
        if (SendAsSlave(m_pReqCurINIasSlaveX))
        {
            m_pReqCurINIasSlaveX = NULL;
        }
    }

    m_nStateINI = (m_pReqCurINIasNodeDB == NULL && m_pReqCurINIasNode == NULL && m_pReqCurINIasSlaveX == NULL)
        ? STATE_INI_PROCESSING
        : STATE_INI_RETRY;
}

Message* CacheL2TOK::FetchRequestINIFromQueue()
{
    assert(m_pGlobalFIFOLIST.size() < m_nGlobalFIFOSize);

    if (m_nGlobalFIFOSize - m_pGlobalFIFOLIST.size() <= s_nGlobalFIFOUpperMargin)
    {
        m_bBufferPriority = true;
    }
    else if (m_nGlobalFIFOSize - m_pGlobalFIFOLIST.size() >= s_nGlobalFIFOLowerMargin)
    {
        m_bBufferPriority = false;
    }

    if (!m_bBufferPriority)
    {
        // Check input queue
        if (!m_requests.empty())
        {
            Message* ret = m_requests.front();
            m_requests.pop();

            // Check whether the request can be found in the queue
            for (list<Message*>::const_iterator iter = m_pGlobalFIFOLIST.begin(); iter != m_pGlobalFIFOLIST.end(); ++iter)
            {
                if ((*iter)->address / m_lineSize == ret->address / m_lineSize)
                {
                    // Same line
                    InsertRequest2GlobalFIFO(ret);
                    return NULL;
                }
            }
    
            return ret;
        }
    }
    
    // Check FIFO buffer
    assert(m_pGlobalFIFOLIST.size() < m_nGlobalFIFOSize);
    if (!m_pGlobalFIFOLIST.empty())
    {
        Message* ret = m_pGlobalFIFOLIST.front();
        m_pGlobalFIFOLIST.pop_front();
        return ret;
    }
    return NULL;
}

void CacheL2TOK::CleansingAndInsert(Message* req)
{
    //////////////////////////////////////////////////////////////////////////
    // two passes:
    // first pass, will process the pipeline registers reversely considering only the previously queued requests
    // they will be removed from the pipeline, and pushed reversely from the front into the queue
    // second pass, will process the pipeline registers check all the previously non-queued requests
    // they will be removed from the pipeline, and pushed directly from the back as the order they are in the pipeline

    // temp request vector
    vector<Message*> vecreq;

    //////////////////////////////////////////////////////////////////////////
    // dump all the stuff to the vector
    // the checking is carried out from the back to front (tail to head)
    m_pPipelineINI.copy(vecreq);

    //////////////////////////////////////////////////////////////////////////
    // first pass

    // queued requests: reversely check every register
    for (int i=vecreq.size()-1;i>=0;i--)
    {
        Message* reqtemp = vecreq[i];

        if (reqtemp != NULL && reqtemp->bqueued && reqtemp->address / m_lineSize == req->address / m_lineSize)
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
        Message* reqtemp = vecreq[i];

        if (reqtemp != NULL && !reqtemp->bqueued && reqtemp->address / m_lineSize == req->address / m_lineSize)
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
    if (m_pReqCurPAS != NULL && m_pReqCurINI != NULL && m_pReqCurPAS->address / m_lineSize == m_pReqCurINI->address / m_lineSize)
    {
        assert(m_nStatePAS == STATE_PAS_PROCESSING || m_nStatePAS == STATE_PAS_POSTPONE);
        m_nStatePAS = STATE_PAS_POSTPONE;
        return;
    }
    
    m_nStatePAS = STATE_PAS_PROCESSING;
    Message* req = m_pReqCurPAS;
    switch(req->type)
    {
    case Message::ACQUIRE_TOKEN:
		if (req->source == m_id)
			OnAcquireTokenRet(req);
		else
			OnAcquireTokenRem(req);
        break;

    case Message::ACQUIRE_TOKEN_DATA:
		if (req->source == m_id)
			OnAcquireTokenDataRet(req);
		else
			OnAcquireTokenDataRem(req);
        break;

    case Message::DISSEMINATE_TOKEN_DATA:
        OnDisseminateTokenData(req);
        break;

    case Message::LOCALDIR_NOTIFICATION:
        // Request is meant for directory, we shouldn't receive it again
        assert(req->source != m_id);
    
        // Just pass it on
        InsertPASNodeRequest(req);
        break;

    default:
        break;
    }
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
        if (SendAsSlave(m_pReqCurPASasSlaveX))
        {
            m_pReqCurPASasSlaveX = NULL;
        }
    }

    if (m_pReqCurPASasNodeX != NULL)
    {
        // check colliding request
        if (!((m_nStateINI == STATE_INI_RETRY && m_pReqCurINIasNode   != NULL && m_pReqCurPASasNodeX->address / m_lineSize == m_pReqCurINIasNode  ->address / m_lineSize) ||
              (m_nStateINI == STATE_INI_RETRY && m_pReqCurINIasNodeDB != NULL && m_pReqCurPASasNodeX->address / m_lineSize == m_pReqCurINIasNodeDB->address / m_lineSize)))
        {
            // send request to memory
            if (SendRequest(m_pReqCurPASasNodeX))
            {
                m_pReqCurPASasNodeX = NULL;
            }
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

    Message* req_incoming;

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
        // Fall-through

    case STATE_PAS_POSTPONE:
        // Processing the reply request
        ProcessPassive();
        
        if (m_nStatePAS != STATE_PAS_POSTPONE)
        {
            SendFromPAS();
        }
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

cache_line_t* CacheL2TOK::LocateLine(MemAddr address)
{
    const MemAddr tag  = (address / m_lineSize) / m_nSets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_nSets) * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        cache_line_t& line = m_lines[set + i];
        if (line.valid && line.tag == tag)
        {
            // The wanted line was in the cache
            return &line;
        }
    }
    return NULL;
}

cache_line_t* CacheL2TOK::GetEmptyLine(MemAddr address)
{
    const size_t set = (size_t)((address / m_lineSize) % m_nSets) * m_assoc;

    // Return the first found empty line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        cache_line_t& line = m_lines[set + i];
        if (!line.valid)
        {
            return &line;
        }
    }
    return NULL;
}

// function for find replacement line
cache_line_t* CacheL2TOK::GetReplacementLine(MemAddr address)
{
    cache_line_t *linelruw = NULL; // replacement line for write-back request
    cache_line_t *linelrue = NULL; // replacement line for eviction request

    const size_t set  = (size_t)((address / m_lineSize) % m_nSets) * m_assoc;
    for (unsigned int i = 0; i < m_assoc; i++)
    {
        cache_line_t& line = m_lines[set + i];
        
        // Return the first found empty one
        if (!line.valid)
            return &line;

        // Pending lines don't count as normal replacable lines
        if (!line.pending)
        {
            if (!line.dirty)
            {
                if (linelrue == NULL || line.time < linelrue->time)
                    linelrue = &line;
            }
            else
            {
                if (linelruw == NULL || line.time < linelruw->time)
                    linelruw = &line;
            }
        }
    }
    return (linelrue != NULL) ? linelrue : linelruw;
}

// Disseminate a number of tokens
void CacheL2TOK::EvictLine(cache_line_t* line)
{
    assert(line->valid);
    
    size_t  set     = (line - &m_lines[0]) / m_assoc;
    MemAddr address = (line->tag * m_nSets + set) * m_lineSize;
     
    Message* reqdt = new Message();
    reqdt->type    = Message::DISSEMINATE_TOKEN_DATA;
    reqdt->address = address;
    reqdt->size    = m_lineSize;
    
    std::fill(reqdt->bitmask, reqdt->bitmask + m_lineSize, true);
    reqdt->dataavailable = true;
    reqdt->bpriority = line->priority;
    reqdt->tokenrequested = (line->dirty ? GetTotalTokenNum() : 0);
    reqdt->tokenacquired = line->tokencount;
    memcpy(reqdt->data, line->data, m_lineSize);
    reqdt->source = m_id;
    
    m_pReqCurINIasNodeDB = reqdt;
}

void CacheL2TOK::InsertSlaveReturnRequest(bool ini, Message *req)
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

void CacheL2TOK::InsertPASNodeRequest(Message* req)
{
    m_queReqPASasNode.push(req);
    if (m_pReqCurPASasNodeX == NULL)
    {
        m_pReqCurPASasNodeX = m_queReqPASasNode.front();
        m_queReqPASasNode.pop();
    }
}

// Local Read from a memory client on the bus
void CacheL2TOK::OnLocalRead(Message* req)
{
    cache_line_t* line = LocateLine(req->address);
    if (line == NULL)
    {
        // We don't have the line; allocate a line and fetch the data
        line = GetReplacementLine(req->address);
        if (line == NULL)
        {
            // No cache line available; suspend request
            CleansingAndInsert(req);
            return;
        }

        if (line->valid)
        {
            // Line is already in use; evict it
            EvictLine(line);
            
            line->valid = false;
        }

        // Reset the cache-line
        line->tag         = (req->address / m_lineSize) / m_nSets;
        line->time        = sc_time_stamp();
        line->valid       = true;
        line->dirty       = false;
        line->tokencount  = 0;
        line->pending     = true;
        line->invalidated = false;
        line->priority    = false;
        line->tlock       = false;
        std::fill(line->bitmask, line->bitmask + m_lineSize, false);

        // Send request for data and a token
        req->type = Message::ACQUIRE_TOKEN_DATA;
        req->source = m_id;
        req->tokenrequested = 1;
        req->dataavailable = false;
        std::fill(req->bitmask, req->bitmask + m_lineSize, false);

        m_pReqCurINIasNode = req;
        return;
    }

    // check whether the line is already locked
    if (m_msbModule.IsSlotLocked(req->address))
    {
        // Suspend the request
        CleansingAndInsert(req);
        return;
    }

    if (line->tokencount > 0 && line->IsLineAtCompleteState())
    {
        // have valid data available
        line->time = sc_time_stamp();
         
        // Send data to clients
        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            req->data[i] = line->data[i];
        }

        // Merge with made writes, if any
        m_msbModule.LoadBuffer(req, *line);
    }
    else
    {
        // The data is not available in the cache-line
        assert(line->pending);
        
        // See if we can grab the data from the MSB instead
        if (!m_msbModule.LoadBuffer(req, *line))
        {
            // No, suspend the request
            CleansingAndInsert(req);
            return;
        }
    }
    
    req->type = Message::READ_REPLY;
    
    // Return reply to memory clients
    InsertSlaveReturnRequest(true, req);
}

// Local Write from a memory client on the bus
void CacheL2TOK::OnLocalWrite(Message* req)
{
    cache_line_t* line = LocateLine(req->address);
    if (line == NULL)
    {
        // We don't have the line; allocate a line
        line = GetReplacementLine(req->address);
        if (line == NULL)
        {
            // No line available; suspend request
            CleansingAndInsert(req);
            return;
        }

        if (line->valid)
        {
            // Line is already in use; evict it
            EvictLine(line);
            
			line->valid = false;
        }
        
        // Initialize cacheline 
        line->tag         = (req->address / m_lineSize) / m_nSets;
        line->time        = sc_time_stamp();
        line->valid       = true;
        line->dirty       = true;
        line->tokencount  = 0;
        line->pending     = true;
        line->invalidated = false;
        line->priority    = false;
        line->tlock       = false;
        std::fill(line->bitmask, line->bitmask + m_lineSize, false);
        
        // Write data into the line
        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            if (req->bitmask[i])
            {
                line->bitmask[i] = true;
                line->data[i] = req->data[i];
            }
        }
        
        // Send a request for all tokens and data
        req->type = Message::ACQUIRE_TOKEN_DATA;
        req->source = m_id;
        req->tokenrequested = GetTotalTokenNum();
        req->dataavailable = false;

        m_pReqCurINIasNode = req;
        return;
    }

    if (!line->pending)   // non-pending state
    {
        assert(!line->invalidated);
        assert(!line->tlock);
        assert(line->tokencount > 0);

        // Update LRU time
        line->time = sc_time_stamp();
        
        // Write data into the line
        line->dirty = true;
        
        std::fill(line->bitmask, line->bitmask + m_lineSize, false);
        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            if (req->bitmask[i])
            {
                line->bitmask[i] = true;
                line->data[i] = req->data[i];
            }
        }

        if (line->tokencount < GetTotalTokenNum())
        {
            // Non-exclusive data; acquire the rest of the tokens
            line->pending = true;

            // Send a request for all tokens
            req->type = Message::ACQUIRE_TOKEN;
            req->source = m_id;
            req->tokenrequested = GetTotalTokenNum();
            req->dataavailable = false;

            // If the line has the priority token, give it to the request
            if (line->priority)
            {
                req->bpriority      = line->priority;
                req->tokenacquired += 1;

                line->tokencount = line->tokencount - 1;
                line->priority   = false;
            }

            m_pReqCurINIasNode = req;
        }
        else
        {
            // Exclusive; can write directly at exclusive or modified lines

            // Send write acknowledgement
            req->type = Message::WRITE_REPLY;
            InsertSlaveReturnRequest(true, req);
        }
    }
    // Line with pending requests
    else if (m_msbModule.IsSlotLocked(req->address))
    {
        // MSB is locked; suspend request
        CleansingAndInsert(req);
    }
    // X-Token MSB implementation
    else if (!line->dirty && line->priority)
    {
        // Read pending with priority token
        assert(line->tokencount > 0);
        assert(!line->tlock);
        assert(!line->invalidated);

        // Update LRU time
        line->time = sc_time_stamp();
        line->dirty = true;

        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            if (req->bitmask[i])
            {
                line->bitmask[i] = true;
                line->data[i] = req->data[i];
            }
        }

        // Send request to acquire all tokens
        req->type = Message::ACQUIRE_TOKEN;
        req->source = m_id;
        req->tokenrequested = GetTotalTokenNum();
        req->dataavailable = false;

        m_pReqCurINIasNode = req;
    }
    else
    {
        // Try to write to the buffer
        if (!m_msbModule.WriteBuffer(req))
        {
            return;
        }

        // Suspend request
        CleansingAndInsert(req);
    }
}

//////////////////////////////////////////////////////////////////////////
// passive request handling
//////////////////////////////////////////////////////////////////////////

// Network remote request to acquire all tokens - invalidates/IV
void CacheL2TOK::OnAcquireTokenRem(Message* req)
{
	cache_line_t* line = LocateLine(req->address);
    if (line == NULL)
    {
        // We don't have the line, forward message
        InsertPASNodeRequest(req);
		return;
    }

    // This is the very first plan, by providing tokens as much as requested, if possible
    if (!line->pending) // non-pending states   // S, E, O, M
    {
        // data is already available but needs to give token to the request
        assert(line->tokencount > 0);
        assert(line->tlock == false);
        assert(req->tokenrequested > 0);
        assert(req->btransient == false);

        // will need to clean up line
        assert(line->gettokenglobalvisible() <= req->tokenrequested - req->tokenacquired);

        // Give line's tokens to the request
        req->tokenacquired += line->gettokenglobalvisible();
        req->dataavailable  = true;
        req->bpriority      = req->bpriority || line->priority;

        // check the whole line with mask bits and update the request according to the cacheline
        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            if (!req->bitmask[i])
            {
                req->data[i] = line->data[i];
            }
        }

        // Clear line
        line->valid = false;
    }
    else    // pending request      // R, T, P, M, U
    {
        // the line must have less token than required, since request require all the tokens
        assert(line->tokencount <= req->tokenrequested - req->tokenacquired);

        if (!line->dirty)  // reading, before  // R, T
        {
            // Get tokens from the line, if any, and set invalidated flag.
            // If the line already has the priority token then take the priority token as well.
            // No matter what, the line will be invalidated.

            // Make sure that when if a request arrives with transient tokens, there are no tokens in the line.
            if (req->btransient)
                assert(line->tokencount == 0);

            // Transfer line tokens to request
            req->tokenacquired += line->gettokenglobalvisible();
            req->bpriority = req->bpriority || line->priority;
    
            line->tokencount = 0;
            line->priority = false;
            
            // check the whole line with mask bits and update the request according to the cacheline
            req->dataavailable = true;
            for (unsigned int i = 0; i < m_lineSize; i++)
            {
                if (line->bitmask[i])
                {
                    req->data[i] = line->data[i];
                }
            }

            line->time = sc_time_stamp();
            line->invalidated = true;
            line->tlock = false;

            for (unsigned int i = 0; i < m_lineSize; ++i)
            {
                if (req->bitmask[i])
                {
                    line->bitmask[i] = true;
                    line->data[i] = req->data[i];
                }
            }
        }
        else    // writing, before      // P, M, U
        {
            int newtokenline;
            
            // 1. req has pt
            // req will get all the tokens
            if (req->bpriority)
            {
                assert(req->btransient == false);   // check the paper
                assert(line->priority == false); 

                if (line->tlock)
                {
                    assert(line->invalidated);
                    // locked tokens are unlocked and released to the request
                    req->tokenacquired += line->tokencount;
                    newtokenline = 0;
                    line->tlock = false;
                }
                else if (line->invalidated)
                {
                    newtokenline = line->tokencount;
                }
                else
                {
                    req->tokenacquired += line->gettokenglobalvisible();
                    newtokenline = 0;
                }

                req->dataavailable = true;

                line->invalidated = true;

                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i] || line->tokencount != 0)
                    {
                        req->data[i] = line->data[i];
                    }
                }
            }
            else if (line->priority)
            {
                assert(line->invalidated == false);
                assert(line->tlock == false);
                assert(req->bpriority == false);

                // transient tokens will be changed to permanent tokens
                req->btransient = false;

                newtokenline = line->tokencount + req->tokenacquired;

                // update request, rip the available token off the request
                req->tokenacquired = 0;
                req->dataavailable = true;

                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i] || line->tokencount != 0)
                    if (!req->bitmask[i])
                    {
                        req->data[i] = line->data[i];
                    }
                }
            }
            else
            {
                // Both the request and the line will get the same number of tokens, but the request
                // will have its tokens transient and line will have the tokens locked.
                // Transient tokens can be transformed into permanent tokens by priority tokens
                // in the line. Locked tokens can be unlocked by priority tokens.
                newtokenline = req->tokenacquired + line->tokencount;

                req->tokenacquired = req->tokenacquired + line->tokencount;
                req->dataavailable = true;
                req->btransient = true;

                line->invalidated = true;
                line->tlock = true;

                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i] || line->tokencount != 0)
                    if (!req->bitmask[i])
                    {
                        req->data[i] = line->data[i];
                    }
                }
            }

            bool bupdatealldata = (line->tokencount == 0 && newtokenline > 0);

            line->time = sc_time_stamp();
            line->tokencount = newtokenline;

            // update the cacheline with the dirty data in the request
            // start from the offset and update the size from the offset
            for (unsigned int i = 0; i < m_lineSize; i++)
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

    // Forward message
    InsertPASNodeRequest(req);
}

// network return request to acquire token - IV return or DE
void CacheL2TOK::OnAcquireTokenRet(Message* req)
{
    cache_line_t* line = LocateLine(req->address);
    assert(line != NULL);
    assert(line->pending);
    assert(line->dirty);

    // check whether the line is already invalidated or not
    // or say, false sharing or races situation
    if (line->invalidated)
    {
        assert(!line->priority);
        
        if (!req->bpriority)
        {
            // We did not got the priority token, clear the locked tokens
            line->tokencount = 0;
        }

        line->tokencount += req->gettokenpermanent();
        line->time = sc_time_stamp();
        line->pending = false;
        line->invalidated = false;
        line->priority = true;
        line->tlock = false;
        
        if (line->tokencount == 0) {
            // We have no tokens left; clear line
            line->valid = false;
        } else {
            // Otherwise, we should have all tokens now
            assert(line->tokencount == GetTotalTokenNum());
        }
    }
    else
    {
        // the request can have transient request, 
        // in case during the false-sharing the current line has the priority token
        if (req->btransient)
        {
            assert(line->priority);

            // transfer the transient tokens
            req->btransient = false;

            if (req->tokenacquired > 0)
            {
                Message *reqnotify = new Message(req);
                reqnotify->source = m_id;
                reqnotify->type = Message::LOCALDIR_NOTIFICATION;
                reqnotify->tokenacquired = req->tokenacquired;

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

        assert(line->tokencount + req->tokenacquired == GetTotalTokenNum());
        assert(line->tlock == false);

        line->time = sc_time_stamp();
        line->tokencount += req->tokenacquired;
        line->pending = false;
        line->invalidated = false;
        line->priority = true;
    }

    // Acknowledge the write
    req->type = Message::WRITE_REPLY;
    InsertSlaveReturnRequest(false, req);

    OnPostAcquirePriorityToken(line, req);
}

// network remote request to acquire token and data     // RE, RS, SR, ER
void CacheL2TOK::OnAcquireTokenDataRem(Message* req)
{
    assert(req->tokenrequested <= GetTotalTokenNum());

	cache_line_t* line = LocateLine(req->address);
    if (line == NULL)
    {
        // We do not have the line, forward message
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

        if (line->gettokenglobalvisible() <= (req->tokenrequested - req->tokenacquired))
        {
            // line tokens are not enough; will need to clean up line
            if (!req->btransient)
            {
                // if the request is read which requires only one token and the line has only one token, 
                // this may work only when total token number == cache number
                if (req->tokenrequested == 1 && req->tokenacquired == 0)
                {
                    req->dataavailable = true;
                    for (unsigned int i = 0; i < m_lineSize; i++)
                    {
                        req->data[i] = line->data[i];
                        req->bitmask[i] = true;
                    }
                    // JOYING distinguish about modified data and normal data
                }
                else
                {
                    // Update request
                    req->tokenacquired += line->gettokenglobalvisible();
                    req->dataavailable = true;
                    req->bpriority = req->bpriority || line->priority;

                    for (unsigned int i = 0; i < m_lineSize; i++)
                    {
                        if (!req->bitmask[i])
                        {
                            req->data[i] = line->data[i];

                            // update read request bitmaks, write request bitmask will not be updated.
                            if (req->tokenrequested != GetTotalTokenNum())
                            {
                                req->bitmask[i] = true;
                            }
                        }
                    }

                    // Clear line
                    line->valid = false;
                }
            }
            else
            {
                assert(line->priority == true);

                // update request
                req->tokenacquired += line->gettokenglobalvisible();
                req->dataavailable = true;
                req->btransient = false;
                req->bpriority = req->bpriority || line->priority;

                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i])
                    {
                        req->data[i] = line->data[i];

                        // update read request bitmaks, write request bitmask will not be updated.
                        if (req->tokenrequested != GetTotalTokenNum())
                        {
                            req->bitmask[i] = true;
                        }
                    }
                }

                // Clear line
                line->valid = false;
            }
        }
        else    // only give out some tokens 
        {
            assert(req->btransient == false);

            unsigned int newlinetoken = line->gettokenglobalvisible() - req->tokenrequested + req->tokenacquired;

            // update request
            req->tokenacquired = req->tokenrequested;
            req->dataavailable = true;

            for (unsigned int i = 0; i < m_lineSize; i++)
            {
                if (line->bitmask[i])
                {
                    req->data[i] = line->data[i];
    
                    // update read request bitmaks, write request bitmask will not be updated.
                    if (req->tokenrequested != GetTotalTokenNum())
                    {
                        req->bitmask[i] = true;
                    }
                }
            }

            // update line  ??
            // check the update request and line data about the consistency !!!! XXXX JXXX !!!???
            line->time = sc_time_stamp();
            line->tokencount = newlinetoken;
        }

        // save the current request
        InsertPASNodeRequest(req);
    }
    // pending request      // R, T, P, W, U
    else if (req->tokenrequested < GetTotalTokenNum())
    {
        // read  // RS, SR
        assert(req->btransient == false);

        if (!line->dirty)  // reading, before  // R, T
        {
            assert(line->tlock == false);
            
            if (line->invalidated)  // T 
            {
                assert(line->priority == false);
                
                // the line has only ghost token for local use not anytoken can be acquired or invalidated
                // get the data if available, and token if possible. otherwise go on
                // CHKS: ALERT: some policy needs to be made to accelerate the process                 

                // update request
                req->dataavailable = true;

                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i])
                    {
                        req->data[i] = line->data[i];
                        req->bitmask[i] = true;
                    }
                    else
                    {
                        line->data[i] = req->data[i];
                        line->bitmask[i] = true;
                    }
                }

                // update line  ??? no update?
                line->time = sc_time_stamp();
            }
            else    // R
            {
                // get the data if available, and token if possible. otherwise go on
                assert(req->tokenrequested > 0);

                if (req->tokenacquired > req->tokenrequested && line->tokencount == 0)
                {
                    // Request has more than enough tokens, give one to the line
                    req->tokenacquired--;
                    line->tokencount++;
                }
                else if (req->tokenacquired < req->tokenrequested && line->tokencount > 1)
                {
                    // Line has more than enough tokens, give one to the request
                    req->tokenacquired++;
                    line->tokencount--;
                }

                if (!contains(line->bitmask, line->bitmask + m_lineSize, false))
                {
                    req->dataavailable = true;

                    for (unsigned int i = 0; i < m_lineSize; i++)
                    {
                        req->data[i] = line->data[i];
                        req->bitmask[i] = true;
                    }
                }
                else if (req->dataavailable)
                {
                    for (unsigned int i = 0; i < m_lineSize; ++i)
                    {
                        if (!line->bitmask[i])
                        {
                            line->data[i] = req->data[i];
                            line->bitmask[i] = true;
                        }
                    }
                }

                line->time = sc_time_stamp();
            }
        }
        else    // writing, before, // P, W, U
        {
            unsigned int newtokenline;

            bool oldpriority = line->priority;

            if (line->invalidated)
            {
                assert(line->priority == false);

                // the request might have the priority, 
                // in this case all locked tokens in the line will be unlocked
                // the line should be un-invalidated and get all the tokens

                if (req->bpriority)
                {
                    // mkae the req transfer the priority token to the line
                    // get rid of the invalidated flag
                    // no lines are locked
                    newtokenline = req->tokenacquired;
                    if (line->tlock)
                    {
                        newtokenline += line->tokencount;
                    }

                    line->invalidated = false;
                    line->tlock = false;
                }
                else
                {
                    // there willl be nothing to lose in this case
                    assert(req->tokenacquired == 0);
                    newtokenline = line->tokencount;
                }
            }
            else
            {
                // the line will get all the tokens anyway
                newtokenline = line->tokencount + req->tokenacquired;
            }
            
            line->priority = line->priority || req->bpriority;
            req->bpriority = false;

            bool acquiredp = (!oldpriority && line->priority);

            // get the data if available, and no token will be granted.
            // update request, get more token or get own tokens ripped off
            req->tokenacquired = 0;

            // if nothing to be updated
            if (line->tokencount != 0)
            {
                req->dataavailable = true;

                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i])
                    {
                        req->data[i] = line->data[i];
                        req->bitmask[i] = true;
                    }
                }
            }

            // update line  ??? no update?
            if (line->tlock)
                assert(newtokenline == line->tokencount);

            line->tokencount = newtokenline;

            if (req->dataavailable && line->tokencount != 0)
            {
                for (unsigned int i = 0; i < m_lineSize; ++i)
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
                
            line->time = sc_time_stamp();
        }

        // save the current request
        InsertPASNodeRequest(req);
    }
    else    // write        // RE, ER
    {
        unsigned int newtokenline=0;
        unsigned int newtokenreq = 0;

        // the line must have less token than required, since request require all the thokens
        assert(line->tokencount <= req->tokenrequested - req->tokenacquired);

        if (!line->dirty)  // reading, before  // R, T
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
            req->tokenacquired = newtokenreq;
            req->dataavailable = true;
            req->bpriority = req->bpriority || line->priority;

            if (line->IsLineAtCompleteState())
            {
                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i])
                    {
                        req->data[i] = line->data[i];
                    }
                }
            }

            // update line
            line->time = sc_time_stamp();
            line->tokencount = newtokenline;
            line->invalidated = true;
            line->priority = false;

            for (unsigned int i = 0 ; i < m_lineSize; ++i)
            {
                if (req->tokenacquired > 0 || req->bitmask[i])
                {
                    line->bitmask[i] = true;
                    line->data[i] = req->data[i];
                }
            }
        }
        else    // writing, before      // P, W, U
        {
            bool bupdatealldata;
            bool blinecomplete = (line->tokencount != 0);
            
            line->time = sc_time_stamp();

            // 1. req has priority token
            // req will get all the tokens
            if (req->bpriority)
            {
                bupdatealldata = false;
                
                assert(req->btransient == false);
                assert(line->priority == false);

                if (line->tlock || !line->invalidated)
                {
                    // Tokens are transfered to request and unlocked, if necessary
                    req->tokenacquired += line->tokencount;
                    line->tlock = false;
                }

                req->dataavailable = true;

                line->tokencount = 0;
                line->invalidated = true;
            }
            // 2. line has the priority, then the line will take all 
            else if (line->priority)
            {
                bupdatealldata = (line->tokencount == 0 && req->tokenacquired > 0);

                assert(line->invalidated == false);
                assert(line->tlock == false);

                req->tokenacquired = 0;
                req->dataavailable = true;
                req->btransient = false;

                line->tokencount += req->tokenacquired;
            }
            else
            {
                bupdatealldata = (line->tokencount == 0 && req->tokenacquired > 0);

                // both will get the same number of tokens, req will be at transient situation
                // and line will have the tokens locked.
                // All of them are only visible locally, cannot be transfered.
                // Transient tokens can later be transformed into permanent tokens by priority tokens in the line
                // locked tokesn can be unlocked by priority tokens.
                // Permanent tokens can later by transfered or used remotely.
                req->tokenacquired = req->tokenacquired + line->tokencount;
                req->dataavailable = true;
                req->btransient = true;

                line->tokencount = req->tokenacquired;
                line->invalidated = true;
                line->tlock = true;
            }
            
            // check the whole line with mask bits and update the request according to the cacheline
            for (unsigned int i = 0; i < m_lineSize; i++)
            {
                if (line->bitmask[i] || blinecomplete)
                if (!req->bitmask[i])
                {
                    req->data[i] = line->data[i];
                }
            }
            
            // update the cacheline with the dirty data in the request
            // start from the offset and update the size from the offset
            for (unsigned int i = 0; i < m_lineSize; i++)
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

        // save the current request
        InsertPASNodeRequest(req);
    }
}

// network request return, with token and data  // RS, SR, RE, ER
void CacheL2TOK::OnAcquireTokenDataRet(Message* req)
{
    cache_line_t* line = LocateLine(req->address);
    assert(line != NULL);
    assert(line->pending);     // non-pending states   // S, E, O, M

    // pending states       // R, T, P, U, W
    if (req->tokenrequested < GetTotalTokenNum())   // read, // RS, SR
    {
        if (!line->dirty)  // reading, before  // R, T
        {
            // resolve evicted lines short of data problem in directory configuration
            // need to resend the request again
            // REVISIT JXXX, maybe better solutions
            if (!req->dataavailable && !contains(line->bitmask, line->bitmask + m_lineSize, false))
            {
                req->bprocessed = true;

                // just send it again
                InsertPASNodeRequest(req);
                return;
            }

            assert(req->btransient == false);

            req->bprocessed = false;

            unsigned int tokenlinenew = line->tokencount + req->tokenacquired;
            bool newlinepriority = line->priority || req->bpriority;
            unsigned int evictthereturnedtokens = (line->invalidated) ? req->tokenacquired : 0;

            // instead of updating the cache line, the request should be updated first
            // update request from the line
            req->type = Message::READ_REPLY;
            if (!contains(line->bitmask, line->bitmask + m_lineSize, false))
            {
                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i])
                    {
                        req->data[i] = line->data[i];
                    }
                }
            }

            // update time and state
            if (line->invalidated || tokenlinenew == 0)
            {
                line->valid = false;
                // TOFIX JXXX backward invalidate L1 caches.

                // in multi-level configuration, there's a chance that an AD request could travel twice(first time reload), and return at invalidated state with non-transient tokens. 
                // in this case, the non-transient tokens should be dispatched again with disseminate request
                if (evictthereturnedtokens > 0)
                {
                    Message *reqevresend = new Message(req);
                    reqevresend->type    = Message::DISSEMINATE_TOKEN_DATA;
                    reqevresend->address = req->address;
                    reqevresend->size    = m_lineSize;
                    std::fill(reqevresend->bitmask, reqevresend->bitmask + m_lineSize, true);
                    reqevresend->dataavailable = true;
                    memcpy(reqevresend->data, req->data, m_lineSize);
                    reqevresend->source = m_id;

                    reqevresend->tokenacquired = evictthereturnedtokens;
                    if (evictthereturnedtokens == GetTotalTokenNum())
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
            else
            {
                if (line->tokencount == 0 || contains(line->bitmask, line->bitmask + m_lineSize, false))
                {
                    assert(!line->breserved);
                    for (unsigned int i = 0 ; i < m_lineSize; ++i)
                    {
                        if (req->bitmask[i])
                        {
                            line->bitmask[i] = true;
                            line->data[i] = req->data[i];
                        }
                    }
                }
                else if (line->breserved)
                {
                    line->dirty = true;
                }
                
                line->time = sc_time_stamp();
                line->tokencount = tokenlinenew;
                line->pending = false;
                line->invalidated = false;
                line->priority = newlinepriority;
                line->tlock = false;
            }

            line->breserved = false;

            OnPostAcquirePriorityToken(line, req);
            InsertSlaveReturnRequest(false, req);
        }
        else    // writing, before  // P, U, W
        {
            // just collect the tokens, 
            // it must be because an EV request dispatched a owner-ev to the R,T line
            assert(line->priority);
            assert(req->btransient == false);
            assert(!req->bpriority);

            unsigned int tokenlinenew = line->tokencount + req->tokenacquired;
            bool newlinepriority = line->priority || req->bpriority;


            line->tag = (req->address / m_lineSize) / m_nSets;
            line->time = sc_time_stamp();
            line->tokencount = tokenlinenew;
            line->priority = newlinepriority;
            line->tlock = false;
                                                    
            // instead of updating the cache line, the request should be updated first
            // update request from the line
            req->type = Message::READ_REPLY;
            if (line->tokencount != 0)
            {
                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i])
                    {
                        req->data[i] = line->data[i];
                    }
                }
            }

            line->breserved = false;

            // save reply request
            InsertSlaveReturnRequest(false, req);
        }
    }
    // write, // RE, ER, (or maybe RS, SR when GetTotalTokenNum() == 1)
    else if (!line->dirty)  // actually reading, before  // R, T
    {
        assert(GetTotalTokenNum() == 1);
        // line is shared but also exclusive (not dirty not owner)
           
        assert(!line->invalidated); 
        assert(line->tlock == false);
        assert(req->btransient == false);

        // update time and state
        assert(line->tokencount == 0);
        line->time = sc_time_stamp();
        line->tokencount += req->tokenacquired;
        line->pending = false;
        line->invalidated = false;
        line->priority = true;
        line->tlock = false;

        for (unsigned int i = 0 ; i < m_lineSize; ++i)
        {
            if (req->tokenacquired > 0 || req->bitmask[i])
            {
                line->bitmask[i] = true;
                line->data[i] = req->data[i];
            }
        }

        // instead of updating the cache line, the reqeust should be updated first
        // update request from the line
        req->type = Message::READ_REPLY;
        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            if (line->bitmask[i])
            {
                req->data[i] = line->data[i];
            }
        }

        // save reply request
        InsertSlaveReturnRequest(false, req);
        OnPostAcquirePriorityToken(line, req);
    }
    else    // writing, before  // P, U, W
    {
        unsigned int newtokenline = 0;
        unsigned int newtokenreq  = 0;
        unsigned int tokennotify  = (req->btransient) ? req->tokenacquired : 0;

         // check whether the line is already invalidated or not
        // or say, false sharing or races situation
        if (line->invalidated) // U state
        {
            assert(!line->priority);
            if (req->bpriority)
            {
                // all locked tokens are unclocked
                assert(!req->btransient);

                line->tlock = false;
                line->invalidated = false;
            }

            newtokenline = line->gettokenglobalvisible() + req->gettokenpermanent();    // this can be zero
            newtokenreq = 0;

            line->time = sc_time_stamp();
            
            // continue_here
            if (newtokenline == 0)
            {
                // Clear line
                line->valid = false;
            }
            else
            {
                assert(newtokenline == GetTotalTokenNum());
                
                if (line->tokencount == 0)
                {
                    for (unsigned int i = 0 ; i < m_lineSize; ++i)
                    {
                        if (req->tokenacquired > 0 || req->bitmask[i])
                        {
                            line->bitmask[i] = true;
                            line->data[i] = req->data[i];
                        }
                    }
                }

                line->dirty = true;
                line->tokencount = newtokenline;
                line->pending = false;
                line->invalidated = false;
                line->priority = true;
                line->tlock = false;
            }
            
            req->type = Message::WRITE_REPLY;
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
            assert(line->tokencount + req->tokenacquired == GetTotalTokenNum());
            assert(line->tlock == false);
            if (req->btransient)
                assert(line->priority);

            if (line->tokencount == 0)
            {
                for (unsigned int i = 0 ; i < m_lineSize; ++i)
                {
                    if (req->tokenacquired > 0 || req->bitmask[i])
                    {
                        line->bitmask[i] = true;
                        line->data[i] = req->data[i];
                    }
                }
            }
            
            line->time = sc_time_stamp();
            line->tokencount += req->tokenacquired;
            line->pending = false;
            line->invalidated = false;
            line->priority = true;
            line->tlock = false;

            req->type = Message::WRITE_REPLY;
        }

        if (req->bmerged)
        {
            for (unsigned int i = 0; i < req->msbcopy.size(); ++i)
            {
                InsertSlaveReturnRequest(false, req->msbcopy[i]);
            }
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

// network disseminate token and data, EV, WB, include IJ
void CacheL2TOK::OnDisseminateTokenData(Message* req)
{
    cache_line_t* line = LocateLine(req->address);

    if (line == NULL)
    {
        // We don't have the line
        if (!m_inject)
        {
            // Do not try to inject
            InsertPASNodeRequest(req);
            return;
        }
        
        // Try to allocate an empty line to inject the evicted line
        line = GetEmptyLine(req->address);
        if (line == NULL)
        {
            // No free line
            InsertPASNodeRequest(req);
            return;
        }
        
        // Store evicted line in the allocated line
        line->tag = (req->address / m_lineSize) / m_nSets;
        line->time = sc_time_stamp();
        line->dirty = (req->tokenrequested == GetTotalTokenNum());
        line->tokencount = req->tokenacquired;
        line->pending = false;
        line->invalidated = false;
        line->priority = req->bpriority;
        line->tlock = false;

        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            if (!line->bitmask[i])
            {
                line->data[i] = req->data[i];
            }
        }
        std::fill(line->bitmask, line->bitmask + m_lineSize, true);

        delete req;
        return;
    }
    
    // We have the line

    if (!line->pending)     // non-pending states   // S, E, O, M
    {
        // give the token of the request to the line 
        assert(line->tlock == false);
        
        line->time = sc_time_stamp();
        if (req->tokenrequested == GetTotalTokenNum())
        {
            line->dirty = true;
        }
        line->tokencount += req->tokenacquired;
        if (req->bpriority && !line->priority)
        {
            line->priority = true;
            OnPostAcquirePriorityToken(line, req);
        }
        delete req;
    }
    // pending states       // R, T, P, U, W
    else if (line->invalidated)      // T, U
    {
        // do not give the tokens to the T line, but U will decide whether the line should stay
        // the situation will never happen, check the label : label_tokenacquired_always_zero
        //
        // [the original line sent out the DD should have already had been invalidated if the line is U]
        // or [the DD will met a non-invalidated line first, as P, W] 
        assert(!line->dirty);

        InsertPASNodeRequest(req);
    }
    else
    {
        // R, P, W
        assert(line->tlock == false);

        if (line->tokencount == 0)
        {
            for (unsigned int i = 0; i < m_lineSize; i++)
            {
                if (!line->bitmask[i])
                {
                    line->data[i] = req->data[i];
                }
            }
            std::fill(line->bitmask, line->bitmask + m_lineSize, true);
        }

        // give all the tokens of the request to the line
        line->time = sc_time_stamp();
        line->tokencount += req->tokenacquired;

        if (req->bpriority && !line->priority)
        {
            line->priority = true;
            OnPostAcquirePriorityToken(line, req);
        }

        if (!line->dirty && req->tokenrequested == GetTotalTokenNum())
        {
            // Special case to reserve the line to be transferred to owner line after reply received.
            line->breserved = true;
        }
        delete req;
    }
}

void CacheL2TOK::OnPostAcquirePriorityToken(cache_line_t* line, Message* req)
{
    assert(line->invalidated == false);

    if (!m_msbModule.IsAddressPresent(req->address))
    {
        return;
    }
    
    const Message&          mergedreq      = m_msbModule.GetMergedRequest(req->address);
    const vector<Message*>& queuedrequests = m_msbModule.GetQueuedRequestVector(req->address);

    // Merge the change from the buffer back into the line
    for (unsigned int i = 0; i < m_lineSize; i++)
    {
        if (mergedreq.bitmask[i])
        {
            line->bitmask[i] = true;
            line->data[i] = mergedreq.data[i];
        }
    }
    
    // if the merged request can be handled directly. 
    if (line->dirty)
    {
        assert(line->tokencount != 0);

        // Acknowledge all buffered writes immediately
        for (size_t i = 0; i < queuedrequests.size(); i++)
        {
            InsertSlaveReturnRequest(false, queuedrequests[i]);
        }
    }
    else
    {
        assert(line->pending == false);
        
        line->dirty = true;
        line->pending = true;

        // Make a copy of the request and attach the write acknowledgements.
        // When the request comes back, the acknowledgements will be sent to the clients.
        Message* merge2send = NULL;
        merge2send = new Message(&mergedreq);
        merge2send->source  = m_id;
        merge2send->msbcopy = queuedrequests;

        InsertPASNodeRequest(merge2send);
    }

    // Remove the merged request, remove the msb slot
    m_msbModule.CleanSlot(req->address);
}

}
