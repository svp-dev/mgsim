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
    if (!RequestNetwork(req))
    {
        return false;
    }

    // change the state to free
    return true;
}

bool CacheL2TOK::SendAsSlaveINI(ST_request* req)
{
    // reset the queued property
    req->bqueued = false;

    // send reply transaction
    if (!channel_fifo_slave.nb_write(req))
    {
        return false;
    }

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

void CacheL2TOK::PrefetchINI()
{
    if (m_prefetchBuffer.size() < MAX_PREFETCHBUFFERSIZE)
    {
        ST_request* fet = FetchRequestINIFromQueue();
        if (fet != NULL)
        {
            m_prefetchBuffer.push_back(fet);
        }
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
    if (m_pfifoReqIn.num_available_fast() <= 0)
    {
      if (GlobalFIFOEmpty())
            return NULL;

        // if no request then check the 
        return PopQueuedRequest();
    }
    else if (!m_pfifoReqIn.nb_read(ret))      // save the request to m_pReqCurINI
    {
        abort();
        return NULL;
    }

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


// react to the initiators, processors or level 1 cache. 
void CacheL2TOK::BehaviorIni()
{
    PrefetchINI();

    switch (m_nStateINI)
    {
    case STATE_INI_PROCESSING:
        // initialize requests to be sent
        m_pReqCurINIasNodeDB = NULL;
        m_pReqCurINIasNode   = NULL;
        m_pReqCurINIasSlaveX = NULL;

        AdvancePipelineINI(true);

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
            AdvancePipelineINI(false);
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
    if (!RequestNetwork(req))
    {
        return false;
    }

    return true;
}


bool CacheL2TOK::SendAsSlavePAS(ST_request* req)
{
    // send reply transaction
    if (!channel_fifo_slave.nb_write(req))
    {
        return false;
    }

    if (req->type == REQUEST_READ_REPLY)
    {
        //RemoveBRfromInvalidationBuffer(m_pReqCurPASasSlave);
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

ST_request* CacheL2TOK::FetchRequestPAS()
{
    ST_request* req_incoming = NULL;
    if (GetNetworkFifo().num_available_fast() <= 0)
    {
        req_incoming = NULL;
        return NULL;
    }
    else if (!GetNetworkFifo().nb_read(req_incoming))
    {
        abort();
        return NULL;
    }

    return req_incoming;
}


// react to the network request/feedback
void CacheL2TOK::BehaviorNet()
{
    ST_request* req_incoming;

    switch (m_nStatePAS)
    {
    case STATE_PAS_PROCESSING:
        req_incoming  = FetchRequestPAS();

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
            req_incoming = FetchRequestPAS();

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

cache_line_t* CacheL2TOK::LocateLineEx(__address_t address)
{
    cache_line_t* line;
    unsigned int index = CacheIndex(address);
    uint64 tag = CacheTag(address);

    line = &(m_pSet[index].lines[0]);

    for (unsigned int i=0; i<m_nAssociativity;i++, line++)
    {
        if ((line->state != CLS_INVALID) && (line->tag == tag))
            return line;
    }

    // miss
    return NULL;
}

cache_line_t* CacheL2TOK::GetEmptyLine(__address_t address)
{
    cache_line_t* line = &(m_pSet[CacheIndex(address)].lines[0]);
 
    for (unsigned int i=0; i<m_nAssociativity; i++, line++)
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

    if ( (lum != LUM_NO_UPDATE)&&(req->nsize > m_nLineSize) )	//// ****
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

// check the line validness according to the bitmask associated with the line
bool CacheL2TOK::CheckLineValidness(cache_line_t* line, bool bwholeline, char* pmask)
{
    if (!bwholeline)
    {
      assert (pmask != NULL);

        // this part is not implemented yet
      abort();
    }

    // a patch to writepending states
    if ((line->pending)&&(line->state == CLS_OWNER)&&(line->tokencount > 0))
    {
        return true;
    }

    if ((line->pending)&&(line->state == CLS_OWNER)&&(line->tokencount==0)&&(!line->invalidated))
    {
        return true;
    }

    // check the bitmasks
    for (int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
    {
        if (bwholeline)
        {
            if ((line->bitmask[i]) != (char)0xff)   // check only for the whole line
            {
                return false;
            }
        }
        else
        {
            if (line->bitmask[i] != pmask[i])   // check only the partial line according to the mask
                return false;
        }
    }

    return true;
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

    reqnew->addresspre = pline->getlineaddress(CacheIndex(req->getlineaddress()), ilog2(m_nSet)) / g_nCacheLineSize;
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
    reqdt->addresspre = line->getlineaddress(CacheIndex(req->getlineaddress()), ilog2(m_nSet)) / g_nCacheLineSize;
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

ST_request* CacheL2TOK::NewDisseminateTokenRequest(ST_request* req, cache_line_t* line)
{
    return NewDisseminateTokenRequest(req, line, line->tokencount, line->priority);
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

void CacheL2TOK::AcquireTokenFromLine(ST_request* req, cache_line_t* line, unsigned int ntoken)
{
    // make sure they have the same address
    assert(req->getlineaddress() == line->getlineaddress(CacheIndex(req->getlineaddress()), ilog2(m_nSet)));

    // make desision what to do 

    // first plan, give as much as required, if possible
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
