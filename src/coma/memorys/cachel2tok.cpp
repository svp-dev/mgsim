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
    //    abort();
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
    if (req->type == MemoryState::REQUEST_INVALIDATE_BR ||
       (req->type == MemoryState::REQUEST_WRITE_REPLY && req->bbackinv))
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
    }
    else
    {
        // Send the feedback to the target master
        req->refcount = 1;
        if (!dynamic_cast<ProcessorTOK*>(get_initiator(req))->SendFeedback(req))
        {
            return false;
        }
        pop_initiator(req);
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
            req->bbackinv = true;
        }
    }

    // change the state to free
    return true;
}

void CacheL2TOK::SendFromINI()
{
    AutoFillSlaveReturnRequest(true);
    bool sentnodedb = (m_pReqCurINIasNodeDB == NULL);
    bool sentnode   = (m_pReqCurINIasNode   == NULL);
    bool sentslave  = (m_pReqCurINIasSlaveX == NULL);

    if (m_pReqCurINIasNodeDB == NULL && m_pReqCurINIasNode == NULL && m_pReqCurINIasSlaveX == NULL)
    {
        assert(m_nStateINI == STATE_INI_PROCESSING);
        return;
    }

    if (m_pReqCurINIasNodeDB != NULL)
    {
        if (!SendAsNodeINI(m_pReqCurINIasNodeDB))
        {
            sentnodedb = false;
        }
        else
        {
            sentnodedb = true;
            m_pReqCurINIasNodeDB = NULL;
        }
    }
    else if (m_pReqCurINIasNode != NULL)
    {
        if (!SendAsNodeINI(m_pReqCurINIasNode))
        {
            sentnode = false;
        }
        else
        {
            sentnode = true;
            m_pReqCurINIasNode = NULL;
        }
    }

    if (m_pReqCurINIasSlaveX != NULL)
    {
        if (!SendAsSlaveINI(m_pReqCurINIasSlaveX))
        {
            sentslave = false;
        }
        else
        {
            sentslave = true;
            m_pReqCurINIasSlaveX = NULL;
        }
    }

    if (sentnodedb&&sentnode&&sentslave)
    {
        m_nStateINI = STATE_INI_PROCESSING;
    }
    else
    {
        m_nStateINI = STATE_INI_RETRY;
    }
}

ST_request* CacheL2TOK::FetchRequestINI()
{
    ST_request* req = NULL;
    if (!m_prefetchBuffer.empty())
    {
        req = m_prefetchBuffer.front();
        m_prefetchBuffer.pop_front();
    }
    return req;
}

ST_request* CacheL2TOK::FetchRequestINIFromQueue()
{
    ST_request* ret = NULL;

    CheckThreshold4GlobalFIFO();

    // check whether the buffer has priority
    if (m_bBufferPriority)
    {
        // the queue shouldn't be empty when buffer has priority
        assert (!GlobalFIFOEmpty());

        // pop the request from the queue 
        return PopQueuedRequest();
    }

    // from here the buffer doesnt' have priority anymore

    // check input buffer
    // check whether there are any available requests
    if (m_requests.empty())
    {
      if (GlobalFIFOEmpty())
            return NULL;

        // if no request then check the 
        return PopQueuedRequest();
    }
    ret = m_requests.front();
    m_requests.pop();

    if (DoesFIFOSeekSameLine(ret))
    {
        InsertRequest2GlobalFIFO(ret);
        ret = NULL;
    }

    return ret;
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
    // Prefetch
    if (m_prefetchBuffer.size() < MAX_PREFETCHBUFFERSIZE)
    {
        ST_request* fet = FetchRequestINIFromQueue();
        if (fet != NULL)
        {
            m_prefetchBuffer.push_back(fet);
        }
    }

    switch (m_nStateINI)
    {
    case STATE_INI_PROCESSING:
        // initialize requests to be sent
        m_pReqCurINIasNodeDB = NULL;
        m_pReqCurINIasNode   = NULL;
        m_pReqCurINIasSlaveX = NULL;

        m_pReqCurINI = m_pPipelineINI.shift(FetchRequestINI());

        // if nothing to process, just skip this round
        if (m_pReqCurINI == NULL)
        {
            AutoFillSlaveReturnRequest(true);
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
            m_pPipelineINI.shift(FetchRequestINI());
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
        Postpone();
        return;
    }
    
    m_nStatePAS = STATE_PAS_PROCESSING;

    ST_request* req = m_pReqCurPAS;
    switch(req->type)
    {
    case MemoryState::REQUEST_ACQUIRE_TOKEN:
		if (IS_INITIATOR(req, this))
			OnAcquireTokenRet(req);
		else
			OnAcquireTokenRem(req);
        break;

    case MemoryState::REQUEST_ACQUIRE_TOKEN_DATA:
		if (IS_INITIATOR(req, this))
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
            req->bbackinv = true;
        }
    }

    // interface is free to process next req
    return true;
}


void CacheL2TOK::SendFromPAS()
{
    AutoFillSlaveReturnRequest(false);
    AutoFillPASNodeRequest();
    bool sentnode  = (m_pReqCurPASasNodeX  == NULL);
    bool sentslave = (m_pReqCurPASasSlaveX == NULL);

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
        if (!SendAsSlavePAS(m_pReqCurPASasSlaveX))
        {
            sentslave = false;
        }
        else
        {
            sentslave = true;
            m_pReqCurPASasSlaveX = NULL;
        }
    }

    if (m_pReqCurPASasNodeX != NULL)
    {
        if (!SendAsNodePAS(m_pReqCurPASasNodeX))
        {
            sentnode = false;
        }
        else
        {
            sentnode = true;
            m_pReqCurPASasNodeX = NULL;
        }
    }

    if (sentnode && sentslave && m_queReqPASasNode.empty())
    {
        m_nStatePAS = STATE_PAS_PROCESSING;
    }
    else
    {
        m_nStatePAS = STATE_PAS_RETRY;
    }
}

void CacheL2TOK::FinishCyclePAS()
{
    // reset state to processing 
    m_nStatePAS = STATE_PAS_PROCESSING;
}

void CacheL2TOK::Postpone()
{
    // reset state to processing 
    assert(m_nStatePAS == STATE_PAS_PROCESSING || m_nStatePAS == STATE_PAS_POSTPONE);
    m_nStatePAS = STATE_PAS_POSTPONE;
}

// react to the network request/feedback
void CacheL2TOK::BehaviorNet()
{
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
            AutoFillSlaveReturnRequest(false);
            AutoFillPASNodeRequest();
            if (m_pReqCurPASasSlaveX != NULL)
                SendFromPAS();

            break;
        }

        // processing the reply request
        ProcessPassive();

        if (m_nStatePAS == STATE_PAS_POSTPONE)
            break;

        SendFromPAS();

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

void CacheL2TOK::UpdateCacheLine(cache_line_t* line, ST_request* req, CACHE_LINE_STATE state, unsigned int token, bool pending, bool invalidated, bool priority, bool tlock, LINE_UPDATE_METHOD lum, bool bupdatetime)
{
    // make sure the reuqest conform to the bit vector format before entering this procedure.

    // always assume the cache line is picked up correctly

    // always assume line is already found thus
    assert(line != NULL);

    // request has to abey the alignment

	assert(req->getreqaddress()%CACHE_REQUEST_ALIGNMENT == 0);

    if ( (req->type == REQUEST_READ) || (req->type == REQUEST_WRITE) || (req->type == REQUEST_READ_REPLY) || (req->type == REQUEST_WRITE_REPLY) )
    	assert(req->nsize%CACHE_REQUEST_ALIGNMENT == 0);     // maybe treat this differently

    if ( (lum != LUM_NO_UPDATE)&&(req->nsize > m_lineSize) )	//// ****
    {
        return;
    }

    if (lum == LUM_STORE_UPDATE)
    {
        if ((!line->pending)&&(state == CLS_OWNER))
        {
            // clear the bitmask 
            for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
                line->bitmask[i] = 0;

        }
        lum = LUM_PRIMARY_UPDATE;
    }

    // update TAG
    line->tag = CacheTag(req->getlineaddress());

    // update time if necessary
    if (bupdatetime)
        line->time = sc_time_stamp();

    // update line state
    line->state = state;

    // update other info
    line->tokencount = token;
    line->pending = pending;
    line->invalidated = invalidated;
    line->priority = priority;
    line->tlock = tlock;

	// reset the mask bits for CLS_INVALID
	if (state == CLS_INVALID)
	{
		for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
			line->bitmask[i] = 0;

        assert(priority == false);
	}

    if (line->tokencount == 0)
    {
        assert(priority == false);
    }
    else if (line->tokencount == GetTotalTokenNum())
    {
        assert(priority == true);
    }

    // if no need to update data
    if (lum == LUM_NO_UPDATE)
        return; 

    // check whether it's clear [***] update, if yes, clear the mask 
    if (lum == LUM_CLR_PRIMARY_UPDATE)
    {
        //assert(state == LNWRITEPENDINGM);
        assert(state == CLS_OWNER);
        assert(pending);
        assert(token > 0);
        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
            line->bitmask[i] = 0;
        lum = LUM_PRIMARY_UPDATE;
    }

    // primary update will overwrite all the updated information from the request to the cacheline
	if (lum == LUM_PRIMARY_UPDATE)	// incremental update
	{
        // reset the mask bits for CLS_INVALID
        if (state == CLS_INVALID)
        {
            // should this be reached?
	  abort();
            for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
                line->bitmask[i] = 0;

            return;
        }

        bool allupdate = true;
        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
        {
            if ((unsigned char)req->bitmask[i] == 0xff)
                continue;
            else
            {
                allupdate = false;
                break;
            }
        }
        // maybe also check from the state point of view to verify

        // dont need to check the offset and size of the request 
        // assume the request is always at the cache line size to be handled in this level

        if (req->IsRequestWithCompleteData() && allupdate)
        {
            memcpy(line->data, req->data, g_nCacheLineSize);

            for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
                line->bitmask[i] = 0xff;
        }
        else
        {
            for (unsigned int i=0;i<g_nCacheLineSize;i+=CACHE_REQUEST_ALIGNMENT)
            {
                unsigned int maskhigh = i / (8*CACHE_REQUEST_ALIGNMENT);
                unsigned int masklow = i % (8*CACHE_REQUEST_ALIGNMENT);


                char maskbit = 1 << masklow;

                if ((req->IsRequestWithCompleteData())||((req->bitmask[maskhigh]&maskbit) != 0))
                {
                    line->bitmask[maskhigh] |= maskbit;

                    memcpy(&line->data[i], &req->data[i], CACHE_REQUEST_ALIGNMENT);
                }
            }

        }
	}

    // feedback update will update only the empty slots in the cacheline
    // and finally make the line complete
	else if ( (lum == LUM_FEEDBACK_UPDATE)||(lum == LUM_NOM_FEEDBACK_UPDATE) )	// complete the cacheline
	{
        // make sure the request is complete
#ifndef NDEBUG
        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
        {

	  assert ((unsigned char)req->bitmask[i] == 0xff);
        }
#endif

        // reset the mask bits for CLS_INVALID
        if (state == CLS_INVALID)
        {
            for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
                line->bitmask[i] = 0;

            return;
        }

		// __address_t alignedaddr = AlignAddress4Cacheline(req->getlineaddress());

		// check the mask bits, and only update the unmasked ones
		for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH;i++)
		{
			unsigned int maskhigh = i/8;
			unsigned int masklow = i%8;

			char testchr = 1 << masklow;

			if ((line->bitmask[maskhigh]&testchr) == 0)	// if it's unmasked
			{
				// update line
				memcpy(&line->data[i*CACHE_REQUEST_ALIGNMENT], &req->data[i*CACHE_REQUEST_ALIGNMENT], CACHE_REQUEST_ALIGNMENT);
				// CHKS:  assumed replied data always contains the whole line
				// which means replied data are always cacheline aligned.
			}
		}

		// update the mask bits
		
		if (lum == LUM_FEEDBACK_UPDATE)
			for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
				line->bitmask[i] = 0xff;
	}
}

void CacheL2TOK::IVUpdateCacheLine(cache_line_t* line, ST_request* req, CACHE_LINE_STATE state, unsigned int token, bool pending, bool invalidated, bool priority, bool tlock, LINE_UPDATE_METHOD lum, bool bupdatetime)
{
    // always assume the cache line is picked up correctly

    // always assume line is already found thus
    assert(line != NULL);

    assert(lum == LUM_RAC_FEEDBACK_UPDATE);

    // assert tag
    assert(line->tag == CacheTag(req->getlineaddress()));

    // assert quest size // double check, this might be no longer needed

    // update time
    if (bupdatetime)
        line->time = sc_time_stamp();

	bool bupdatealldata = ( (line->state == CLS_OWNER)&&(state == CLS_OWNER)&&(line->pending)&&(pending)&&(line->tokencount ==0)&&(token > 0) );

    // update line state (maybe not needed)
    line->state = state;

    // update other info
    line->tokencount = token;
    line->pending = pending;
    line->invalidated = invalidated;
    line->priority = priority;
    line->tlock = tlock;

    if (line->tokencount == 0)
    {
        assert(priority == false);
    }
    else if (line->tokencount == GetTotalTokenNum())
    {
        assert(priority == true);
    }

    // update the cacheline with the dirty data in the request
    // start from the offset and update the size from the offset
    if (lum == LUM_RAC_FEEDBACK_UPDATE) // of course it is
    {

        //for (unsigned int i=0;i<req->nsize;i++)
		for (unsigned int i=0;i<g_nCacheLineSize;i++)
        {
            // get the mask high and low
            unsigned int maskhigh = i/8;
            unsigned int masklow = i%8;

            // make the bit mask for this certain data character
            char testchar = 1 << masklow;

			// if not all the data needs to be updated. then skip the unnecessary parts, only update if required
			if ((req->bitmask[maskhigh]&testchar) == 0)
			{
                if ( (bupdatealldata) && ((line->bitmask[maskhigh]&testchar) == 0) )    // then update
					line->data[i] = req->data[i];

				continue;
			}
            else
            {
                // if the mask is already on there in the cache line then dont update 
                // otherwise update
                if ( ((line->bitmask[maskhigh]&testchar) == 0)||(bupdatealldata) )    // then update
                {
                    line->data[i] = req->data[i];
                    
                    // update the cacheline mask
                    line->bitmask[maskhigh] |= testchar;
                }
            }

            //// we dont care about the cacheline bit mask, we just write the updated data.

        }
    }
}


void CacheL2TOK::UpdateRequest(ST_request* req, cache_line_t* line, MemoryState::REQUEST requesttype, __address_t address, bool bdataavailable, bool bpriority, bool btransient, unsigned int ntokenacquired, REQUEST_UPDATE_METHOD rum)
{
    req->type = requesttype;
    req->tokenacquired = ntokenacquired;
    req->dataavailable = bdataavailable;
    req->bpriority = bpriority;
    req->btransient = btransient;

    if (req->bpriority)
    {
        assert(req->btransient == false);
    }

    if (ntokenacquired == 0)
        assert(bpriority == false);
    else if (ntokenacquired == GetTotalTokenNum())
        assert(bpriority == true);

    if ((requesttype == REQUEST_READ)||(requesttype == REQUEST_WRITE)||(requesttype == REQUEST_READ_REPLY)||(requesttype == REQUEST_WRITE_REPLY)||(requesttype == REQUEST_INVALIDATE_BR))
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
    for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH;i++)
    {
        unsigned int maskhigh = i / 8;
        unsigned int masklow = i % 8;

        char testmask = 1 << masklow;

        if ( ((testmask & line->bitmask[maskhigh]) != 0) || (blinecomplete) )
        {
            // copy that part
            for (int j=0;j<CACHE_REQUEST_ALIGNMENT;j++)
            {
				// calculate the current position
//				unsigned int curpos = i*8+j;

				//if ( (rum == RUM_MASK) && ((curpos<reqoffset)||(curpos>=(reqoffset+reqnsize))) )
				if ( (rum == RUM_MASK) && ((req->bitmask[maskhigh]&testmask) == 0) )
					continue;

				//if ( (rum == RUM_NON_MASK) && ((i>=reqoffset)&&(i<(reqoffset+reqnsize))) )
				if ( (rum == RUM_NON_MASK) && ((req->bitmask[maskhigh]&testmask) != 0) )
					continue;

                unsigned int index = i*CACHE_REQUEST_ALIGNMENT+j;
                req->data[index] = line->data[index];

                // update read request bitmaks, write request bitmask will not be updated. 
                if ((requesttype == REQUEST_ACQUIRE_TOKEN_DATA) && (req->tokenrequested != GetTotalTokenNum()))
                {
                    req->bitmask[maskhigh] |= testmask;
                }
            }
        }

    }
}

// create new eviction or write back request: 
ST_request* CacheL2TOK::NewEVorWBRequest(ST_request* req, cache_line_t* pline, bool beviction, bool bINI)
{
    // create and initialize the new eviction / write back request
    ST_request* reqnew = new ST_request();

    reqnew->type = MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA;

    if (beviction)
        reqnew->tokenrequested = 0;     // request
    else
        reqnew->tokenrequested = GetTotalTokenNum();

    reqnew->addresspre = pline->getlineaddress(CacheIndex(req->getlineaddress()), lg2(m_nSets)) / g_nCacheLineSize;
    reqnew->offset = 0;
    reqnew->nsize = g_nCacheLineSize;
    reqnew->Conform2BitVecFormat();
    reqnew->dataavailable = true;
    memcpy(reqnew->data, pline->data, g_nCacheLineSize);			// copy data ??? maybe not necessary for EVs
    ADD_INITIATOR(reqnew, this);

    // $$$ optimization for victim line buffer $$$
    m_fabEvictedLine.InsertItem2Buffer(reqnew->getlineaddress(), reqnew->data, g_nCacheLineSize);

    // if it's from passive interface, skip new IB request
    if (!bINI)
        return reqnew;

    assert(m_pReqCurINIasSlaveX == NULL);
    ST_request *reqib = new ST_request(reqnew);
    ADD_INITIATOR(reqib, (void*)NULL);
    reqib->type = REQUEST_INVALIDATE_BR;
    m_pReqCurINIasSlaveX = reqib;

    return reqnew;
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
    ADD_INITIATOR(req, this);

    req->tokenrequested = ntoken;
    req->dataavailable = false;
}

void CacheL2TOK::Modify2AcquireTokenRequestRead(ST_request* req)
{
    assert((req->type != MemoryState::REQUEST_ACQUIRE_TOKEN)&&(req->type != MemoryState::REQUEST_ACQUIRE_TOKEN_DATA));

    req->type = MemoryState::REQUEST_ACQUIRE_TOKEN_DATA;
    // clear request bitmask
    for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
        req->bitmask[i] = 0;

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
ST_request* CacheL2TOK::NewDisseminateTokenRequest(ST_request* req, cache_line_t* line, unsigned int ntoken, bool bpriority)
{
    assert(line->tokencount >= ntoken);
    assert(line->state != CLS_INVALID);
    ST_request* reqdt = new ST_request();

    if (line->priority == false)
        assert(bpriority == false);

    reqdt->type = MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA;
    reqdt->addresspre = line->getlineaddress(CacheIndex(req->getlineaddress()), lg2(m_nSets)) / g_nCacheLineSize;
    reqdt->offset = 0;
    reqdt->nsize = g_nCacheLineSize;
    reqdt->Conform2BitVecFormat();
    reqdt->dataavailable = true;
    memcpy(reqdt->data, line->data, g_nCacheLineSize);
    ADD_INITIATOR(reqdt, this);

    if (bpriority)
    {
        reqdt->bpriority = true;
        line->priority = false;
    }
    else
        reqdt->bpriority = false;

    reqdt->tokenacquired = ntoken;

    if (line->state == CLS_SHARER)
    {
        reqdt->tokenrequested = 0;
    }
    else
    {
        reqdt->tokenrequested = GetTotalTokenNum();
    }
    assert(reqdt->btransient == false);

    line->tokencount = line->tokencount - ntoken;

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

void CacheL2TOK::AutoFillPASNodeRequest()
{
    if (m_pReqCurPASasNodeX == NULL)
    {
        if (!m_queReqPASasNode.empty())
        {
            m_pReqCurPASasNodeX = m_queReqPASasNode.front();
            m_queReqPASasNode.pop();
        }
        else
            m_pReqCurPASasNodeX = NULL;
    }
}

//////////////////////////////////////////////////////////////////////////
// initiative request handling
//////////////////////////////////////////////////////////////////////////
    
namespace MemSim
{
    unsigned int g_uHitCountL = 0;
    unsigned int g_uHitCountS = 0;
    unsigned int g_uConflictAddL = 0;
    unsigned int g_uConflictAddS = 0;
    unsigned int g_uProbingLocalLoad = 0;
}

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
        cache_line_t* pline = GetReplacementLine(address);

        // if all the cachelines are locked for now
        if (pline == NULL)
        {
            // cleansing the pipeline and insert the request to the global FIFO
            CleansingAndInsert(req);

            // pipeline done
            return;
        }

        if (pline->state == CLS_INVALID)
        {
            // check the size of the write request
            if (req->nsize > m_lineSize)
            {
                abort();
            }

            if ((req->nsize+req->offset) <= m_lineSize)
            {
                // find out the bits and mark them 

                // save the corresponding data

            }
            else
            {
                // save the data of the whole line. 
                abort();
            }

            // update line info
            UpdateCacheLine(pline, req, CLS_SHARER, 0, true, false, false, false, LUM_NO_UPDATE);

            // modify request
            Modify2AcquireTokenRequestRead(req);

            // save the current request
            m_pReqCurINIasNode = req;

            // try to send the request to network
            
        }
        else if (!pline->pending)    // non-pending lines
        {
            // disseminate line tokens
            ST_request* reqdd = NewDisseminateTokenRequest(req, pline, pline->tokencount, pline->priority);

            // save the request for double node sending
            m_pReqCurINIasNodeDB = reqdd;

            // remove mask
            pline->removemask();

            // update cacheline
            // erase the previous values and store the new data with LNWRITEPENDINGI state
            UpdateCacheLine(pline, req, CLS_SHARER, 0, true, false, false, false, LUM_NO_UPDATE);

            // modify request
            Modify2AcquireTokenRequestRead(req);

            // save this as a normal request
            m_pReqCurINIasNode = req;

            // the normal request will be sent out in later cycles
        }
        else    // pending lines
        {
	        abort();  // just to alert once 
            // cleansing the pipeline and insert the request to the global FIFO
            CleansingAndInsert(req);

            // pipeline done
            return;
        }

        return;
    }

    // the line state must be something here
    assert(line->state != CLS_INVALID);

    if ((line->gettokenlocalvisible() > 0)&&(!lockonmsb)&&(line->IsLineAtCompleteState()))   // have valid data available    // WPE with valid data might have some token
    {
        // update time for REPLACE_POLICY_LRU
        UpdateCacheLine(line, req, line->state, line->tokencount, line->pending, line->invalidated, line->priority, line->tlock, LUM_NO_UPDATE);
        // write data back
        UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY, address, true);

        // statistics
        if (line->tlock)
            g_uProbingLocalLoad ++;

        if (hitonmsb)
        {
            m_msbModule.LoadBuffer(req, line);
        }

        // save request
        InsertSlaveReturnRequest(true, req);
        g_uHitCountL++;
        //m_pReqCurINIasSlave = req;
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
                    if (line->invalidated)
                        g_uConflictAddL++;

                    // succeed
                    // save request 
                    InsertSlaveReturnRequest(true, req);
                    //m_pReqCurINIasSlave = req;
                    g_uHitCountL++;

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
            else if ((line->gettokenlocalvisible() > 0)&&(line->IsLineAtCompleteState()))
            {
                // update time for REPLACE_POLICY_LRU
                UpdateCacheLine(line, req, line->state, line->tokencount, line->pending, line->invalidated, line->priority, line->tlock, LUM_NO_UPDATE);

                // write data back
                UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY, address, true);

                // statistics
                if (line->tlock)
                    g_uProbingLocalLoad ++;

                // save request 
                InsertSlaveReturnRequest(true, req);
                //m_pReqCurINIasSlave = req;
                g_uHitCountL++;

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
        cache_line_t* pline = GetReplacementLine(address);

        // if all the cachelines are locked for now
        if (pline == NULL)
        {// put the current request in the global queue

            // cleansing the pipeline and insert the request to the global FIFO
	    if (req->type == 4) cout << hex << "w1 " << req->getreqaddress() << endl;
            CleansingAndInsert(req);

            // pipeline done
            return;
        }

        if (pline->state == CLS_INVALID)
        {
            // check the size of the write request
            if (req->offset + req->nsize > m_lineSize)
            {
                abort();
            }

            // update line info
            UpdateCacheLine(pline, req, CLS_OWNER, 0, true, false, false, false, LUM_STORE_UPDATE);

            // modify request
            Modify2AcquireTokenRequestWrite(req, true);

            // save the current request
            m_pReqCurINIasNode = req;
        }
        else if (!pline->pending)   // non-pending lines
        {
            // disseminate line tokens
            ST_request *reqdd = NewDisseminateTokenRequest(req, pline, pline->tokencount, pline->priority);

            // save the request for double node sending
            m_pReqCurINIasNodeDB = reqdd;

			// remove the bit mask!
			pline->removemask();

            // update cacheline 
            UpdateCacheLine(pline, req, CLS_OWNER, 0, true, false, false, false, LUM_STORE_UPDATE);

            // modify request
            Modify2AcquireTokenRequestWrite(req, true);

            // save this as a normal request
            m_pReqCurINIasNode = req;

        }
        else    // pending requests
        {
	        abort();  // just to alert once 
            // cleansing the pipeline and insert the request to the global FIFO
            CleansingAndInsert(req);

            // pipeline done
            return;
        }

        return;
    }

    // the line shouldn't be invalid anymore
    assert(line->state != CLS_INVALID);

    if (!line->pending)   // non-pending state
    {
        assert(line->invalidated == false);
        assert(line->tlock == false);

        if (line->gettokenglobalvisible() < GetTotalTokenNum())  // non-exclusive
        {
            // data is non-exclusive
            // 1. change states to Pending, and it will later change to modified
            // 2. acquire the rest of tokens

            // update line
            UpdateCacheLine(line, req, CLS_OWNER, line->tokencount, true, line->invalidated, line->priority, line->tlock, LUM_STORE_UPDATE); 


            if (hitonmsb)
            {
	            abort();      // wanna know why here
                m_msbModule.WriteBuffer(req);
            }
            else
            {
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
                    UpdateCacheLine(line, req, CLS_OWNER, line->tokencount-1, true, line->invalidated, false, line->tlock, LUM_STORE_UPDATE); 
                }

                // save the current request
                m_pReqCurINIasNode = req;
            }
        }
        else    // exclusive
        {
            // can write directly at exclusive or modified lines

            // update time for REPLACE_POLICY_LRU
            UpdateCacheLine(line, req, CLS_OWNER, line->tokencount, false, line->invalidated, line->priority, line->tlock, LUM_STORE_UPDATE);

            // change reply
            UpdateRequest(req, line, MemoryState::REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

            // save request 
            InsertSlaveReturnRequest(true, req);
            //m_pReqCurINIasSlave = req;

            g_uHitCountS++;
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
            // pipeline done
        }
        else
        {
            assert(line->pending);
            // check whether any empty slot left in the MSB
            //int freeonmsb = m_msbModule.IsFreeSlotAvailable();

            // X-Token MSB implementation
            if (line->state == CLS_SHARER)  // reading pending
            {
                if (line->priority) // if read pending with priority token
                {
                    assert(line->tokencount > 0);
                    assert(!line->tlock);
                    assert(!line->invalidated);

                    // write directly to the line and change line state to W
                    UpdateCacheLine(line, req, CLS_OWNER, line->tokencount, line->pending, line->invalidated, line->priority, line->tlock, LUM_STORE_UPDATE);

                    // modify request
                    Modify2AcquireTokenRequestWrite(req, line, false);

                    // save the current request
                    m_pReqCurINIasNode = req;

                    return;
                }
                else
                {
                    // try to write to the buffer
                    if (m_msbModule.WriteBuffer(req))
                    {
                        return;
                    }
                }
            }
            else    // write pending
            {
                assert(line->state == CLS_OWNER);
                if (line->priority) // write pending with priority token
                {
                    // [N/A] should judge whether the request can proceed on the line or the MSB
                    // now : assume perform everything on the MSB

                    if (m_msbModule.WriteBuffer(req))
                    {
                        return;
                    }


                }
                else
                {
                    // currently the same
                    if (m_msbModule.WriteBuffer(req))
                    {
                        return;
                    }
                }
            }

            // always suspend on them   // JXXX maybe treat exclusive/available data differently
            // cleansing the pipeline and insert the request to the global FIFO
            CleansingAndInsert(req);
            // pipeline done
        }
    }
}

// ???????
// what tht hell is this
void CacheL2TOK::OnInvalidateRet(ST_request* req)
{
    abort();

    // pass the request directly through the network
    m_pReqCurINIasNode = req;

    // try to send the request to network
    // SendAsNodeINI();
}

//////////////////////////////////////////////////////////////////////////
// passive request handling
//////////////////////////////////////////////////////////////////////////

// network remote request to acquire token - invalidates/IV
void CacheL2TOK::OnAcquireTokenRem(ST_request* req)
{
	__address_t address = req->getreqaddress();

	// locate certain set
	cache_line_t* line = LocateLine(address);

    // handle invalid state, or say, the line cannot be found
    if (line == NULL)   // I
    {
		// save the current request
        InsertPASNodeRequest(req);
		//m_pReqCurPASasNode = req;

		return; // -- remove else -- 
    }

    assert(line->state != CLS_INVALID);
    assert(req->getlineaddress() == line->getlineaddress(CacheIndex(req->getlineaddress()), lg2(m_nSets)));

    // diffferent algorithm will determine the performance.  JXXX
    // this is the very first plan, by providing tokens as much as requested, if possible
    if (!line->pending) // non-pending states   // S, E, O, M
    {
        // data is already available but needs to give token to the request

        assert(line->tokencount > 0);
        assert(req->tokenrequested > 0);
        assert(line->tlock == false);

        if (line->gettokenglobalvisible() <= (req->tokenrequested - req->tokenacquired))   // will need to clean up line
        {
            // just_to_check
            assert(req->btransient == false);

            // update request
            UpdateRequest(req, line, req->type, address, true, req->bpriority||line->priority, req->btransient, req->tokenacquired+line->gettokenglobalvisible(), RUM_NON_MASK);    // ??? JONYXXX data availabe ?= true

            // update line
            UpdateCacheLine(line, req, CLS_INVALID, 0, false, false, false, false, LUM_NO_UPDATE);
        }
        else    // only give out some tokens 
        {
	        abort();  // not reachable for now
        }

        // save the current request
        InsertPASNodeRequest(req);
        //m_pReqCurPASasNode = req;
    }
    else    // pending reqeust      // R, T, P, M, U
    {
        //assert(req->tokenrequested == GetTotalTokenNum());

        // the line must have less token than required, since request require all the thokens
        assert(line->tokencount <= (req->tokenrequested - req->tokenacquired));

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
            UpdateCacheLine(line, req, line->state, 0, line->pending, true, false, false, LUM_INCRE_OVERWRITE);

        }
        else    // writing, before      // P, M, U
        {
            unsigned int newtokenline=0;
            unsigned int newtokenreq=0;

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
                    newtokenreq = req->tokenacquired + line->gettokenlocked();
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
                IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, true, line->priority, line->tlock, LUM_RAC_FEEDBACK_UPDATE);
            }
            else if (line->priority)
            {
                assert(line->invalidated == false);
                assert(line->tlock == false);
                assert(req->bpriority == false);

                if (req->btransient)
                {
                    // transient tokens will be changed to permanent tokens
                    req->btransient = false;
                }

                newtokenline = line->tokencount + req->gettokenpermanent();
                newtokenreq = 0;

                // update request, rip the available token off to the request
                UpdateRequest(req, line, req->type, address, true, req->bpriority, req->btransient, newtokenreq, RUM_NON_MASK);

                // update line, and keep the state, and 
                IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, line->invalidated, line->priority, line->tlock, LUM_RAC_FEEDBACK_UPDATE);
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
                //newtokenline = req->tokenacquired + line->tokencount;
                //newtokenreq = req->tokenacquired + line->tokencount;
                newtokenline = req->gettokenpermanent() + line->tokencount;
                newtokenreq = req->tokenacquired + line->gettokenglobalvisible();

                // update request, rip the available token off to the request
                UpdateRequest(req, line, req->type, address, true, req->bpriority, true, newtokenreq, RUM_NON_MASK);


                // update line, and keep the state, and 
                IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, true, line->priority, true, LUM_RAC_FEEDBACK_UPDATE);



            }

        }

        // save the current request
        InsertPASNodeRequest(req);
        //m_pReqCurPASasNode = req;

    }
}

// network return request to acquire token - IV return or DE
void CacheL2TOK::OnAcquireTokenRet(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate certain set
    cache_line_t* line = LocateLine(address);

    // make sure the line can be found
    assert(line!=NULL);

    // handle other states
    assert(line->state!=CLS_INVALID);
    assert(req->getlineaddress() == line->getlineaddress(CacheIndex(req->getlineaddress()), lg2(m_nSets)));

    if (!line->pending) // non-pending states   // S, E, O, M
    {
        // this shouldn't happen
      abort();
    }
    else    // pending states,  R, T, P, W, U
    {
      assert(line->state != CLS_SHARER);  // reading, before // R, T

        // writing, before // P, W, U
        {
            unsigned int newtokenline=0;
            unsigned int newtokenreq=0;

            unsigned int tokennotify = (req->btransient)?req->tokenacquired:0;

            // check whether the line is already invalidated or not
            // or say, false sharing or races situation
            if (line->invalidated)
            {
                assert(line->priority == false);
                if (req->bpriority)
                {
                    // all locked tokens are unclocked
                    assert(req->btransient == false);
                    assert(line->priority == false);

                    if (line->tlock)
                    {
                        line->tlock = false;
                    }
                    else
                    {
                        assert(line->tokencount == 0);
                    }

                    line->invalidated = false;
                }
                else if (line->priority)
                {
		            abort();
		            
                    // all transient tokens became normal
                    if (req->btransient)
                    {
                        req->btransient = false;

                        if (tokennotify > 0)
                        {
                            ST_request *reqnotify = new ST_request(req);
		                    ADD_INITIATOR(reqnotify, this);
                            reqnotify->type = Request_LOCALDIR_NOTIFICATION;
                            reqnotify->tokenacquired = tokennotify;

                            InsertPASNodeRequest(reqnotify); 
                        }
                    }
                    else
                    {
                        assert(req->tokenacquired == 0);
                        assert(line->tlock == false);
                    }
                }
                else
                {
                    // all transient tokens or locked tokens will be stay as they are, no state changes
                }

                newtokenline = line->gettokenglobalvisible() + req->gettokenpermanent();    // this can be zero
                newtokenreq = 0;

                // continue_here
                pop_initiator(req);

                UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

                if (newtokenline == 0)
                {
                    // extra line invalidation method
                    LineInvalidationExtra(req, false);
                    // CHKS: assume no needs for the same update again. this might leave the value from another write
                    UpdateCacheLine(line, req, CLS_INVALID, newtokenline, false, false, false, false, LUM_NO_UPDATE);
                }
                else
                {
                    assert(newtokenline == GetTotalTokenNum());

                    UpdateCacheLine(line, req, CLS_OWNER, newtokenline, false, false, true, false, LUM_NO_UPDATE);
                }

                // save reply request
                InsertSlaveReturnRequest(false, req);
                //m_pReqCurPASasSlave = req;a

            }
            else
            {
                // CHKS: double check the request and the line get all the tokens

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
                        ADD_INITIATOR(reqnotify, this);
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

                pop_initiator(req);

                // CHKS: assume no needs for the same update again. this might leave the value from another write
                UpdateCacheLine(line, req, line->state, line->gettokenglobalvisible() + req->gettokenpermanent(), false, false, true, false, LUM_NO_UPDATE);

                UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

                // save reply request
                InsertSlaveReturnRequest(false, req);
                //m_pReqCurPASasSlave = req;
            }

            OnPostAcquirePriorityToken(line, req);
        }
    }
}

// network remote request to acquire token and data     // RE, RS, SR, ER
void CacheL2TOK::OnAcquireTokenDataRem(ST_request* req)
{
	__address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

	// locate certain set
	cache_line_t* line = LocateLine(address);

    assert(req->tokenrequested <= GetTotalTokenNum());

    // handle invalid state, or say, the line cannot be found
    if (line == NULL)   // I
    {
		// save the current request
        InsertPASNodeRequest(req);
		return;
    }

    assert(line->state != CLS_INVALID);
    assert(req->getlineaddress() == line->getlineaddress(CacheIndex(req->getlineaddress()), lg2(m_nSets)));

    // diffferent algorithm will determine the performance.  JXXX
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
                    UpdateCacheLine(line, req, CLS_INVALID, 0, false, false, false, false, LUM_NO_UPDATE);
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
                UpdateCacheLine(line, req, CLS_INVALID, 0, false, false, false, false, LUM_NO_UPDATE);
            }
        }
        else    // only give out some tokens 
        {
            assert(req->btransient == false);

            unsigned int newlinetoken = line->gettokenglobalvisible() - (req->tokenrequested - req->tokenacquired);

            // update request
            UpdateRequest(req, line, req->type, address, true, req->bpriority, req->btransient, req->tokenrequested);

            // update line  ??
            //UpdateCacheLine(line, req, line->state, newlinetoken, line->pending, line->invalidated, LUM_RAC_FEEDBACK_UPDATE);
            // check the update request and line data about the consistency !!!! XXXX JXXX !!!???
            UpdateCacheLine(line, req, line->state, newlinetoken, line->pending, line->invalidated, line->priority, line->tlock, LUM_NO_UPDATE);
            
        }

        // save the current request
        InsertPASNodeRequest(req);
        //m_pReqCurPASasNode = req;
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

                    unsigned int ntokentoacq = 0;
                    unsigned int ntokenlinenew = 0;
                    // CHKS: ALERT: some policy needs to be made to accelerate the process
                 
                    ntokenlinenew = line->tokencount;
                    ntokentoacq = 0;

                    // update request
                    UpdateRequest(req, line, req->type, address, (line->IsLineAtCompleteState()?true:false)||req->dataavailable, req->bpriority, req->btransient, req->tokenacquired+ntokentoacq);

                    // update line  ??? no update?
                    UpdateCacheLine(line, req, line->state, ntokenlinenew, line->pending, line->invalidated, line->priority, line->tlock, (req->dataavailable&&(!line->IsLineAtCompleteState())?LUM_INCRE_COMPLETION:LUM_NO_UPDATE));
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

                    UpdateCacheLine(line, req, line->state, ntokenlinenew, line->pending, line->invalidated, line->priority, line->tlock, (req->dataavailable&&(!line->IsLineAtCompleteState())?LUM_INCRE_COMPLETION:LUM_NO_UPDATE));


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

                        newtokenline = req->gettokenpermanent() + line->gettokenlocked();
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
                        newtokenline = req->gettokenpermanent() + line->tokencount;
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

                if ((!oldpriority)&&(line->priority))
                    acquiredp = true;

                // get the data if available, and no token will be granted.
                // update request, get more token or get own tokens ripped off
                UpdateRequest(req, line, req->type, address, (req->dataavailable||(line->IsLineAtCompleteState())?true:false), req->bpriority, req->btransient, newtokenreq, (line->IsLineAtCompleteState()?RUM_ALL:RUM_NONE));
                assert(req->bpriority == false);
                assert(req->btransient == false);

                // update line  ??? no update?
                if (line->tlock)
                    assert(newtokenline == line->tokencount);

                UpdateCacheLine(line, req, line->state, newtokenline, line->pending, line->invalidated, line->priority, line->tlock, ((req->dataavailable&&(!line->IsLineAtCompleteState()))?LUM_INCRE_COMPLETION:LUM_NO_UPDATE));

                if (acquiredp)
                    OnPostAcquirePriorityToken(line, req);
            }

            // save the current request
            InsertPASNodeRequest(req);
            //m_pReqCurPASasNode = req;
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
                UpdateCacheLine(line, req, line->state, newtokenline, line->pending, true, false, line->tlock, LUM_INCRE_OVERWRITE);
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
                        newtokenreq = req->tokenacquired + line->gettokenlocked();
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
                    IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, true, line->priority, line->tlock, LUM_RAC_FEEDBACK_UPDATE);

                }
                // 2. line has the priority, then the line will take all 
                else if (line->priority)
                {
                    assert(line->invalidated == false);
                    assert(line->tlock == false);
                    assert(req->bpriority == false);

                    if (req->btransient)
                    {
                        // transient tokens will be changed to permanent tokens
                        req->btransient = false;
                    }

                    newtokenline = line->tokencount + req->gettokenpermanent();
                    newtokenreq = 0;

                    // update request, rip the available token off to the request
                    UpdateRequest(req, line, req->type, address, true, req->bpriority, req->btransient, newtokenreq, RUM_NON_MASK);

                    // update line, and keep the state, and 
                    IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, line->invalidated, line->priority, line->tlock, LUM_RAC_FEEDBACK_UPDATE);
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
                    IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, true, line->priority, true, LUM_RAC_FEEDBACK_UPDATE);
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

    assert (line->pending);     // non-pending states   // S, E, O, M

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
                unsigned int tokenlinenew = line->tokencount + req->gettokenpermanent();
                bool newlinepriority = line->priority || req->bpriority;

                // pop the initiator
                pop_initiator(req);

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
                    UpdateCacheLine(line, req, CLS_INVALID, 0, false, false, false, false, LUM_NO_UPDATE);
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
                        ADD_INITIATOR(reqevresend, this);

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
                else
                {
                    if ((line->tokencount == 0)||(!line->IsLineAtCompleteState()))
                    {
                        assert(!line->breserved);   // just alert, may not be error
                        UpdateCacheLine(line, req, line->state, tokenlinenew, false, false, newlinepriority, false, LUM_INCRE_OVERWRITE);
                    }
                    else
                        UpdateCacheLine(line, req, ((line->breserved)?CLS_OWNER:line->state), tokenlinenew, false, false, newlinepriority, false, LUM_NO_UPDATE);
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

                unsigned int tokenlinenew = line->tokencount + req->gettokenpermanent();
                bool newlinepriority = line->priority || req->bpriority;


                UpdateCacheLine(line, req, line->state, tokenlinenew, line->pending, line->invalidated, newlinepriority, false, LUM_NO_UPDATE);

                // instead of updating the cache line, the reqeust should be updated first
                // update request from the line
                //UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY_X, address);
                UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY, address, true, false, false, 0xffff, (line->IsLineAtCompleteState()?RUM_ALL:RUM_NONE));

                // pop the initiator
                pop_initiator(req);

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
	      assert (GetTotalTokenNum() == 1);
                {
                    // line is shared but also exclusive (not dirty not owner)
                   
                    assert(!line->invalidated); 
                    assert(line->tlock == false);
                    assert(req->btransient == false);

                    unsigned int tokenlinenew = line->tokencount + req->tokenacquired;

                    // update time and state
		            assert (line->tokencount == 0);
                    UpdateCacheLine(line, req, line->state, tokenlinenew, false, false, true, false, LUM_INCRE_OVERWRITE);


                    // pop the initiator
                    pop_initiator(req);

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

            unsigned int tokennotify = (req->btransient)?req->tokenacquired:0;

                 // check whether the line is already invalidated or not
                // or say, false sharing or races situation
                if (line->invalidated) // U state
                {
                    assert(line->priority == false);
                    if (req->bpriority)
                    {
                        // all locked tokens are unclocked
                        assert(req->btransient == false);
                        assert(line->priority == false);

                        if (line->tlock)
                        {
                            line->tlock = false;
                        }
                        else
                        {
                            assert(line->tokencount == 0);
                        }

                        line->invalidated = false;
                    }
                    else if (line->priority)
                    {
		                abort();
                        // all transient tokens became normal
                        if (req->btransient)
                        {
                            req->btransient = false;

                            if (tokennotify > 0)
                            {
                                ST_request *reqnotify = new ST_request(req);
                                reqnotify->tokenacquired = tokennotify;
                                ADD_INITIATOR(reqnotify, this);
                                reqnotify->type = Request_LOCALDIR_NOTIFICATION;

                                InsertPASNodeRequest(reqnotify); 
                            }
                        }
                        else
                        {
                            assert(req->tokenacquired == 0);
                            assert(line->tlock == false);
                        }
                    }
                    else
                    {
                        // all transient tokens or locked tokens will be stay as they are, no state changes

                    }

                    newtokenline = line->gettokenglobalvisible() + req->gettokenpermanent();    // this can be zero
                    newtokenreq = 0;


                    // continue_here
                    pop_initiator(req);

                    if (newtokenline == 0)
                    {
                        UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

                        // extra line invalidation method
                        LineInvalidationExtra(req, false);
                        // CHKS: assume no needs for the same update again. this might leave the value from another write
                        UpdateCacheLine(line, req, CLS_INVALID, newtokenline, false, false, false, false, LUM_NO_UPDATE);
                    }
                    else
                    {
                        assert(newtokenline == GetTotalTokenNum());

                        UpdateCacheLine(line, req, CLS_OWNER, newtokenline, false, false, true, false, (line->tokencount == 0 )?LUM_INCRE_OVERWRITE:LUM_NO_UPDATE);

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
                        //abort();
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

                    UpdateCacheLine(line, req, line->state, line->tokencount + req->tokenacquired, false, false, true, false, (line->tokencount == 0)?LUM_INCRE_OVERWRITE:LUM_NO_UPDATE);

                    // save reply request
                    pop_initiator(req);

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
        if (m_nInjectionPolicy == IP_NONE)
        {
            // pass the transaction down 
            // this can change according to different strategies
            // JXXX

            // save the current request
            InsertPASNodeRequest(req);

            // request will be passed to the next node
        }
        else if (m_nInjectionPolicy == IP_EMPTY_1EJ)
        {
            line = GetEmptyLine(address);

            if (line == NULL)
            {
                // save the current request
                InsertPASNodeRequest(req);
            }
            else
            {
                // update line info
                if (req->tokenrequested == GetTotalTokenNum()) // WB
                {
                    UpdateCacheLine(line, req, CLS_OWNER, req->tokenacquired, false, false, req->bpriority, false, LUM_FEEDBACK_UPDATE);
                }
                else
                {
                    UpdateCacheLine(line, req, CLS_SHARER, req->tokenacquired, false, false, req->bpriority, false, LUM_FEEDBACK_UPDATE);
                }

                // terminate request
                delete req;
            }
        }
        else
        {
	        // invalid case
	        abort();
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
        UpdateCacheLine(line, req, ((req->tokenrequested==GetTotalTokenNum())?CLS_OWNER:line->state), line->tokencount+req->tokenacquired, line->pending, line->invalidated, line->priority||req->bpriority, line->tlock, LUM_NO_UPDATE);

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

                assert(tokenlinenew<=GetTotalTokenNum());

                assert(line->tlock == false);

                bool acquiredp = false;
                if ((req->bpriority)&&(!line->priority))
                    acquiredp = true;

                if (line->tokencount == 0)
                {
                    UpdateCacheLine(line, req, line->state, tokenlinenew, line->pending, line->invalidated, line->priority||req->bpriority, line->tlock, LUM_FEEDBACK_UPDATE);
                }
                else
                {
                    UpdateCacheLine(line, req, line->state, tokenlinenew, line->pending, line->invalidated, line->priority||req->bpriority, line->tlock, LUM_NO_UPDATE);
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
                    UpdateCacheLine(line, req, line->state, tokenlinenew, line->pending, line->invalidated, line->priority||req->bpriority, line->tlock, LUM_FEEDBACK_UPDATE);
                }
                else
                {
                    UpdateCacheLine(line, req, line->state, tokenlinenew, line->pending, line->invalidated, line->priority||req->bpriority, line->tlock, LUM_NO_UPDATE);
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
    
    bool hitonmsb = m_msbModule.IsAddressPresent(address);
//    bool lockonmsb = hitonmsb?m_msbModule.IsSlotLocked(address, req):false;

    // in case no related msb slot
    if (!hitonmsb)
        return;

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
            UpdateCacheLine(line, mergedreq, line->state, line->tokencount, line->pending, line->invalidated, line->priority, line->tlock, LUM_PRIMARY_UPDATE);

            // and append all the queued request on the msb slot to a return buffer
            for (unsigned int i=0;i<queuedrequests->size();i++)
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

            UpdateCacheLine(line, mergedreq, line->state, line->tokencount, line->pending, line->invalidated, line->priority, line->tlock, LUM_PRIMARY_UPDATE);

            // and append all the queued request on the msb slot to a return buffer
            for (unsigned int i=0;i<queuedrequests->size();i++)
            {
                InsertSlaveReturnRequest(false, (*queuedrequests)[i]);
            }

            // remove the merged request, remove the msb slot
            m_msbModule.CleanSlot(address);

            // abort();  // JONY ALERT JONY_ALERT
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
            UpdateCacheLine(line, mergedreq, CLS_OWNER, line->tokencount, true, false, line->priority, false, LUM_PRIMARY_UPDATE);

            merge2send = new ST_request(mergedreq);
            ADD_INITIATOR(merge2send, this);

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
            UpdateCacheLine(line, mergedreq, CLS_OWNER, line->tokencount, true, line->invalidated, line->priority, line->tlock, LUM_PRIMARY_UPDATE);

            // then send it from the network interface
            // Duplicate the merged request

            merge2send = new ST_request(mergedreq);
            ADD_INITIATOR(merge2send, this);

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

        UpdateCacheLine(line, mergedreq, CLS_OWNER, line->tokencount, true, false, false, false, LUM_PRIMARY_UPDATE);

        merge2send = new ST_request(mergedreq);
        ADD_INITIATOR(merge2send, this);

        // associate the merged request with a request queue
        // dumplicate request queue
        m_msbModule.DuplicateRequestQueue(address, merge2send);

        // remove the msb slot
        m_msbModule.CleanSlot(address);
    }

    // send request if any
    if (merge2send != NULL)
    {
//        assert(m_pReqCurPASasNode == NULL);

        InsertPASNodeRequest(merge2send);
        //m_pReqCurPASasNode = merge2send;
    }

    // when the merged request comes back. the queue will be added to the return buffer

    // in the middle other reuqest can still be queued in msb or queue to the line
    //
}



void CacheL2TOK::OnDirNotification(ST_request* req)
{
    // request meant for directory, shouldn't receive it again
    assert (!IS_INITIATOR(req, this));
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

    for (unsigned int i=0; i<m_assoc; i++)
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
