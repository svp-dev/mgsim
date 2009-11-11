#include "cachel2tokim.h"

#include "../simlink/linkmgs.h"

using namespace MemSim;

#ifdef TOKEN_COHERENCE

//////////////////////////////////////////////////////////////////////////
// initiative request handling
//////////////////////////////////////////////////////////////////////////
    
//#define DEBUG_TEST_XXX {cache_line_t* xtempline = LocateLineEx(0x112280);  if (xtempline!=NULL)  cout << hex << (void*)&(xtempline->tokencount)<< (void*)&(xtempline->state)  << (void*)&(xtempline->pending) << "   " << xtempline->tokencount << " " << xtempline->state << " " << xtempline->pending << xtempline->invalidated << endl;}

//#define DEBUG_TEST_XXX {}


namespace MemSim{
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

//unsigned int nib = 0;
//vector<ST_request*> vecpib;

// Local Read 
void CacheL2TOKIM::OnLocalRead(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate certain set
    cache_line_t* line = LocateLineEx(address);

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
        cache_line_t* pline = GetReplacementLineEx(address);

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
            if (req->nsize > s_nLineSize)
            {
                cerr << ERR_HEAD_OUTPUT << "request size is comfined to be smaller than cacheline size now!" << endl;
                assert(false);
            }

            if ((req->nsize+req->offset) <= s_nLineSize)
            {
                // find out the bits and mark them 

                // save the corresponding data

            }
            else
            {
                // save the data of the whole line. 
                cerr << ERR_HEAD_OUTPUT << "request size error" << endl;
                assert(false);
            }

            // update line info
            UpdateCacheLine(pline, req, CLS_SHARER, 0, true, false, false, false, LUM_NO_UPDATE);
            //UpdateCacheLine(pline, req, LNREADPENDING, LUM_NO_UPDATE);

			LOG_VERBOSE_BEGIN(VERBOSE_STATE)
				clog << LOG_HEAD_OUTPUT << "cache line change from INVALID to Sharer Pending state" << endl;
			LOG_VERBOSE_END

            // modify request
            Modify2AcquireTokenRequestRead(req);

            // save the current request
            m_pReqCurINIasNode = req;

            // try to send the request to network
            
        }
        else if (!pline->pending)    // non-pending lines
        {
            // disseminate line tokens
            ST_request* reqdd = NewDisseminateTokenRequest(req, pline);

            // save the request for double node sending
            m_pReqCurINIasNodeDB = reqdd;

            // remove mask
            pline->removemask();

            // update cacheline
            // erase the previous values and store the new data with LNWRITEPENDINGI state
            UpdateCacheLine(pline, req, CLS_SHARER, 0, true, false, false, false, LUM_NO_UPDATE);

			LOG_VERBOSE_BEGIN(VERBOSE_STATE)
				clog << LOG_HEAD_OUTPUT << "cache line change from INVALID to Sharer Pending state" << endl;
			LOG_VERBOSE_END

            // modify request
            Modify2AcquireTokenRequestRead(req);

			LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
				clog << LOG_HEAD_OUTPUT << "Acquire Token request is created on local read miss:" << endl;
                print_request(req);
			LOG_VERBOSE_END

            // save this as a normal request
            m_pReqCurINIasNode = req;

            // the normal request will be sent out in later cycles
        }
        else    // pending lines
        {
            assert(false);  // just to alert once 
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
        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "LR hits at line: " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << " : " ;
            print_cline_data(line);
        LOG_VERBOSE_END

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

#ifdef OPTIMIZATION_READ_REPLY_FLUSHING
        // $$$ read reply flushing $$$
        ReadReplyFlushing(req);
#endif


        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
           clog << LOG_HEAD_OUTPUT << "LR done address " << FMT_ADDR(req->getreqaddress()) << ", " << FMT_DTA(req->data[0]) << "returned." << endl;
        print_request(req);
            print_cline_data(line);
        LOG_VERBOSE_END

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
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "line in pending state and locked no msb: " ;
                print_cline(line);
            LOG_VERBOSE_END
#if defined(CACHE_SRQ)
            assert(false);
            // queue the request 
            nEntry = PopEmptyQueueEntry();
            if (nEntry != EOQ)
            {
                m_pQueueBuffer[nEntry].request = req;
                INSERT_SUSPEND_ENTRY(nEntry);
            }
            else
            {
                cerr << ERR_HEAD_OUTPUT << "no empty slot available" << endl;
            }
            SetAvailableINI();
#else
            // cleansing the pipeline and insert the request to the global FIFO
            CleansingAndInsert(req);

            // pipeline done
#endif
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
                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << "LR local visible reading done address " << FMT_ADDR(req->getreqaddress()) << ", " << FMT_DTA(req->data[0]) << "returned." << endl;
                    LOG_VERBOSE_END

                    // save request 
                    InsertSlaveReturnRequest(true, req);
                    //m_pReqCurINIasSlave = req;
                    g_uHitCountL++;

                    return;
                }
                else
                {
                    assert(line->pending);

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << "line in pending state , queue it: " ;
                        print_cline(line);
                    LOG_VERBOSE_END
#if defined(CACHE_SRQ)
                    assert(false);
                    // queue the request 
                    nEntry = PopEmptyQueueEntry();
                    if (nEntry != EOQ)
                    {
                        m_pQueueBuffer[nEntry].request = req;
                        INSERT_SUSPEND_ENTRY(nEntry);
                    }
                    else
                    {
                        cerr << ERR_HEAD_OUTPUT << "no empty slot available" << endl;
                    }
                    SetAvailableINI();
#else
                    // cleansing the pipeline and insert the request to the global FIFO
                    CleansingAndInsert(req);

                    // pipeline done
#endif
                }
            }
            else if ((line->gettokenlocalvisible() > 0)&&(line->IsLineAtCompleteState()))
            {
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "LR hits at pending line: " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << " : " ;
                    print_cline(line);
                LOG_VERBOSE_END

                // update time for REPLACE_POLICY_LRU
                UpdateCacheLine(line, req, line->state, line->tokencount, line->pending, line->invalidated, line->priority, line->tlock, LUM_NO_UPDATE);

                // write data back
                UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY, address, true);

                // statistics
                if (line->tlock)
                    g_uProbingLocalLoad ++;


#ifdef OPTIMIZATION_READ_REPLY_FLUSHING
                // $$$ read reply flushing $$$
                ReadReplyFlushing(req);
#endif

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                   clog << LOG_HEAD_OUTPUT << "LR local visible reading done address " << FMT_ADDR(req->getreqaddress()) << ", " << FMT_DTA(req->data[0]) << "returned." << endl;
                LOG_VERBOSE_END

                // save request 
                InsertSlaveReturnRequest(true, req);
                //m_pReqCurINIasSlave = req;
                g_uHitCountL++;

                return;
            }
            else
            {
                assert(line->pending);

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "line in pending state , queue it: " ;
                    print_cline(line);
                LOG_VERBOSE_END
#if defined(CACHE_SRQ)
                assert(false);
                // queue the request 
                nEntry = PopEmptyQueueEntry();
                if (nEntry != EOQ)
                {
                    m_pQueueBuffer[nEntry].request = req;
                    INSERT_SUSPEND_ENTRY(nEntry);
                }
                else
                {
                    cerr << ERR_HEAD_OUTPUT << "no empty slot available" << endl;
                }
                SetAvailableINI();
#else
                // cleansing the pipeline and insert the request to the global FIFO
                CleansingAndInsert(req);

                // pipeline done
#endif
            }
        }
    }
}

// local Write
void CacheL2TOKIM::OnLocalWrite(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate certain set
    cache_line_t* line = LocateLineEx(address);
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
        cache_line_t* pline = GetReplacementLineEx(address);

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
            if (req->nsize > s_nLineSize)
            {
                cerr << ERR_HEAD_OUTPUT << "request size is comfined to be smaller than cacheline size now!" << endl;
                assert(false);
                return;
            }

            if ((req->nsize+req->offset) <= s_nLineSize)
            {
                // find out the bits and mark them 

                // save the corresponding data

            }
            else
            {
                // save the data of the whole line. 
                cerr << ERR_HEAD_OUTPUT << "request size error" << endl;
                assert(false);
            }

            // update line info
            UpdateCacheLine(pline, req, CLS_OWNER, 0, true, false, false, false, LUM_STORE_UPDATE);

            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "cache line change from INVALID to Owner Pending state" << endl;
            LOG_VERBOSE_END

            // modify request
            Modify2AcquireTokenRequestWrite(req, true);

            // save the current request
            m_pReqCurINIasNode = req;
        }
        else if (!pline->pending)   // non-pending lines
        {
            // disseminate line tokens
            ST_request *reqdd = NewDisseminateTokenRequest(req, pline);

            // save the request for double node sending
            m_pReqCurINIasNodeDB = reqdd;

			// remove the bit mask!
			pline->removemask();

            // update cacheline 
            UpdateCacheLine(pline, req, CLS_OWNER, 0, true, false, false, false, LUM_STORE_UPDATE);

            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "cache line change from INVALID to Ownder Pending state" << endl;
            LOG_VERBOSE_END

            // modify request
            Modify2AcquireTokenRequestWrite(req, true);

			LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
				clog << LOG_HEAD_OUTPUT << "Acquire Token request is created on local write miss:" << endl;
                print_request(req);
			LOG_VERBOSE_END

            // save this as a normal request
            m_pReqCurINIasNode = req;

        }
        else    // pending requests
        {
            assert(false);  // just to alert once 
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

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "LW hits at line: " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << " : " ;
            LOG_VERBOSE_END

            // update line
            UpdateCacheLine(line, req, CLS_OWNER, line->tokencount, true, line->invalidated, line->priority, line->tlock, LUM_STORE_UPDATE); 


            if (hitonmsb)
            {
                assert(false);      // wanna know why here
                m_msbModule.WriteBuffer(req);
            }
            else
            {
                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "cache line change to Owner Pending state" << endl;
                    print_cline_data(line);
                LOG_VERBOSE_END

                // modify request
                Modify2AcquireTokenRequestWrite(req, line, false);

                // REVIST
                if (LinkMGS::s_oLinkConfig.m_nDirectory > 0)
                {
                    if (line->priority)
                    {
                        assert(line->tokencount > 0);

                        // make request priority, if possible
                        req->bpriority = line->priority;
                        req->tokenacquired += 1;

                        // update line
                        UpdateCacheLine(line, req, CLS_OWNER, line->tokencount-1, true, line->invalidated, false, line->tlock, LUM_STORE_UPDATE); 

                    }
                }

                // save the current request
                m_pReqCurINIasNode = req;

            }
        }
        else    // exclusive
        {
            // can write directly at exclusive or modified lines

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "LW hits at line: " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << " : " ;
            print_cline(line);
            LOG_VERBOSE_END

            // update time for REPLACE_POLICY_LRU
            UpdateCacheLine(line, req, CLS_OWNER, line->tokencount, false, line->invalidated, line->priority, line->tlock, LUM_STORE_UPDATE);

            // change reply
            UpdateRequest(req, line, MemoryState::REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

            LOG_VERBOSE_BEGIN(VERBOSE_MOST)
                clog << LOG_HEAD_OUTPUT << "write done address " << FMT_ADDR(req->getreqaddress()) << " with " << FMT_DTA(req->data[0]) << endl;
            LOG_VERBOSE_END

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
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "line in pending state and locked at msb: " ;
                print_cline(line);
            LOG_VERBOSE_END
#if defined(CACHE_SRQ)
            assert(false);
            // queue the request 
            nEntry = PopEmptyQueueEntry();
            if (nEntry != EOQ)
            {
                m_pQueueBuffer[nEntry].request = req;
                INSERT_SUSPEND_ENTRY(nEntry);
            }
            else
            {
                cerr << ERR_HEAD_OUTPUT << "no empty slot available" << endl;
            }
            SetAvailableINI();
#else
            // cleansing the pipeline and insert the request to the global FIFO
	    if (req->type == 4) cout << hex << "w2 " << req->getreqaddress() << endl;
            CleansingAndInsert(req);

            // pipeline done
#endif
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

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line change to Owner Pending state from Shared pending state" << endl;
                    LOG_VERBOSE_END

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
                        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                            clog << LOG_HEAD_OUTPUT << "write to msb buffer at shared pending state wthout pt" << endl;
                        LOG_VERBOSE_END

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
                        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                            clog << LOG_HEAD_OUTPUT << "write to msb buffer at owner pending state with pt" << endl;
                        LOG_VERBOSE_END

                        return;
                    }


                }
                else
                {
                    // currently the same
                    if (m_msbModule.WriteBuffer(req))
                    {
                        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                            clog << LOG_HEAD_OUTPUT << "write to msb buffer at owner pending state without pt" << endl;
                        LOG_VERBOSE_END

                        return;
                    }
                }
            }

            // always suspend on them   // JXXX maybe treat exclusive/available data differently
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "line in pending state and locked at msb: " ;
                print_cline(line);
            LOG_VERBOSE_END
#if defined(CACHE_SRQ)
            assert(false);
            // queue the request 
            nEntry = PopEmptyQueueEntry();
            if (nEntry != EOQ)
            {
                m_pQueueBuffer[nEntry].request = req;
                INSERT_SUSPEND_ENTRY(nEntry);
            }
            else
            {
                cerr << ERR_HEAD_OUTPUT << "no empty slot available" << endl;
            }
            SetAvailableINI();
#else
            // cleansing the pipeline and insert the request to the global FIFO
	    if (req->type == 4) cout << hex << "w3 " << req->getreqaddress() << endl;
            CleansingAndInsert(req);

            // pipeline done
#endif
        }
    }
}

#ifdef WAIT_INVALIDATE_INNER_CACHE
// ???????
// what tht hell is this
void CacheL2TOKIM::OnInvalidateRet(ST_request* req)
{
    assert(false);

    // pass the request directly through the network
    m_pReqCurINIasNode = req;

    // try to send the request to network
    // SendAsNodeINI();
}
#endif

//////////////////////////////////////////////////////////////////////////
// passive request handling
//////////////////////////////////////////////////////////////////////////

// network remote request to acquire token - invalidates/IV
void CacheL2TOKIM::OnAcquireTokenRem(ST_request* req)
{
	__address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

	// locate certain set
	cache_line_t* line = LocateLineEx(address);

    // just to make sure it's actually and IV. 
    // only in this case will the invalidation make sense.
    //assert(req->tokenrequested == GetTotalTokenNum());

    // create backward invalidation
    if (m_nBBIPolicy == BBI_LAZY)
    {
        ST_request *newreq = new ST_request(req);
        //req->address = (address>>m_nLineBits)<<m_nLineBits;  // alert
        //ADD_INITIATOR_NODE(newreq, this);
		ADD_INITIATOR_NODE(newreq, (void*)NULL);
        newreq->type = REQUEST_INVALIDATE_BR;
        InsertSlaveReturnRequest(false, newreq);
        //m_pReqCurPASasSlave = newreq;
        newreq->starttime = sc_time_stamp().to_double();

        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << " IB request address " << FMT_ADDR(address) << hex << " " << newreq << endl;
        LOG_VERBOSE_END

        //nib++;
        //vecpib.push_back(newreq);
    }

    // handle invalid state, or say, the line cannot be found
    if (line == NULL)   // I
    {
        LOG_VERBOSE_BEGIN(VERBOSE_ALL)
            clog << LOG_HEAD_OUTPUT << "line not found in cache" << endl;
        LOG_VERBOSE_END

		LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
		    clog << LOG_HEAD_OUTPUT << "AT passed at address " << FMT_ADDR(req->getreqaddress()) << endl;
		LOG_VERBOSE_END

        // line miss, do extra stuff
        LineMissedExtra(req);

        // invalidate line and l1 lines when invalidated
        if (m_nBBIPolicy == BBI_LAZY)
            LineInvalidationExtra(req, true);

		// save the current request
        InsertPASNodeRequest(req);
		//m_pReqCurPASasNode = req;

		return; // -- remove else -- 
    }

    assert(line->state != CLS_INVALID);
    assert(req->getlineaddress() == line->getlineaddress(CacheIndex(req->getlineaddress()), m_nSetBits));

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

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << " AT address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
            LOG_VERBOSE_END

            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "cache line is invalidated by remote AT request" << endl;
            LOG_VERBOSE_END

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << " AT passed @ address " << FMT_ADDR(req->getreqaddress()) << ", acquired " << line->tokencount << "tokens" << endl;
            LOG_VERBOSE_END

            // extra line invalidation method
            // ALERT
            // if BBI != LAZY
            //LineInvalidationExtra(req, true);


            // update request
            UpdateRequest(req, line, req->type, address, true, req->bpriority||line->priority, req->btransient, req->tokenacquired+line->gettokenglobalvisible(), RUM_NON_MASK);    // ??? JONYXXX data availabe ?= true

            // update line
            UpdateCacheLine(line, req, CLS_INVALID, 0, false, false, false, false, LUM_NO_UPDATE);

            // invalidate line and l1 lines when invalidated
            if (m_nBBIPolicy == BBI_LAZY)
                LineInvalidationExtra(req, true);

        }
        else    // only give out some tokens 
        {
            assert(false);  // not reachable for now
        }

        // save the current request
        InsertPASNodeRequest(req);
        //m_pReqCurPASasNode = req;
    }
    else    // pending reqeust      // R, T, P, M, U
    {
        // mark for statistics
        if (!req->bmerged)
            MemStatMarkConflict(req->ref);

        //assert(req->tokenrequested == GetTotalTokenNum());

        // the line must have less token than required, since request require all the thokens
        assert(line->tokencount <= (req->tokenrequested - req->tokenacquired));

        if (line->state == CLS_SHARER)  // reading, before  // R, T
        {
            // get tokens if any. set invalidated flag
            // if the line already has the priority token then take the priority token as well.
            // no matter what, the line will be invalidated

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << " AT address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
            LOG_VERBOSE_END

            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "cache line at Sharer Pending state gives up " << line->tokencount <<  " tokens on AT request, and set/maintain invalidated flag" << endl;
            LOG_VERBOSE_END

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << " AT passed @ address " << FMT_ADDR(req->getreqaddress()) << ", acquired " << line->tokencount << "tokens" << endl;
            LOG_VERBOSE_END

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

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << " AT address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "cache line at Owner Pending state gives up " << line->tokencount - newtokenline <<  " tokens on AT request, and set/maintain invalidated flag" << endl;
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << " AT passed @ address " << FMT_ADDR(req->getreqaddress()) << ", acquired " << newtokenreq - req->tokenacquired << "tokens" << endl;
                LOG_VERBOSE_END

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

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << " AT address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "cache line at Owner Pending state keeps tokens on AT request" << endl;
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << " AT passed @ address " << FMT_ADDR(req->getreqaddress()) << ", lost all " << req->tokenacquired << " tokens" << endl;
                LOG_VERBOSE_END

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

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << " AT address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "cache line at Owner Pending state keeps tokens on AT request" << endl;
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << " AT passed @ address " << FMT_ADDR(req->getreqaddress()) << ", without  lost any tokens, t/l" << endl;
                LOG_VERBOSE_END

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
void CacheL2TOKIM::OnAcquireTokenRet(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate certain set
    cache_line_t* line = LocateLineEx(address);

    if ( (req->getlineaddress() == 0x00116a40) )
    {
        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << "token " << line->tokencount << " " << req->gettokenpermanent() << endl;
        LOG_VERBOSE_END
    }

    // make sure the line can be found
    assert(line!=NULL);

    // handle other states
    assert(line->state!=CLS_INVALID);
    assert(req->getlineaddress() == line->getlineaddress(CacheIndex(req->getlineaddress()), m_nSetBits));

    if (!line->pending) // non-pending states   // S, E, O, M
    {
        // this shouldn't happen
        assert(false);
    }
    else    // pending states,  R, T, P, W, U
    {
        if (line->state == CLS_SHARER)  // reading, before // R, T
        {
            assert(false);
        }
        else    // writing, before // P, W, U
        {
            unsigned int newtokenline=0;
            unsigned int newtokenreq=0;

#ifndef MEMSIM_DIRECTORY_REQUEST_COUNTING
            // double check this, whether this is correct // JXXX
            if ((!line->invalidated)&&(line->tokencount == 0))
            {
                assert(false);
            }
#endif

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            unsigned int tokennotify = (req->btransient)?req->tokenacquired:0;
#endif

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
                    assert(false);
                    // all transient tokens became normal
                    if (req->btransient)
                    {
                        req->btransient = false;

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
                        if ((tokennotify > 0) && (LinkMGS::s_oLinkConfig.m_nDirectory > 0))
                        {
                            ST_request *reqnotify = new ST_request(req);
		                    ADD_INITIATOR_NODE(reqnotify, this);
                            reqnotify->type = Request_LOCALDIR_NOTIFICATION;
                            reqnotify->tokenacquired = tokennotify;
                            //m_pReqCurPASasSlave = newreq;
                            reqnotify->starttime = sc_time_stamp().to_double();

                            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                                clog << LOG_HEAD_OUTPUT << " NOTIFY  request address " << FMT_ADDR(address) << hex << " " << reqnotify << endl;
                            LOG_VERBOSE_END
                
                            InsertPASNodeRequest(reqnotify); 
                        }
#endif
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
                pop_initiator_node(req);

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "Token acquisition finished at invalidated state, " << req->tokenacquired << " tokens acquired " << endl;
                LOG_VERBOSE_END

                UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

                if (newtokenline == 0)
                {
                    // extra line invalidation method
                    LineInvalidationExtra(req, false);
                    // CHKS: assume no needs for the same update again. this might leave the value from another write
                    UpdateCacheLine(line, req, CLS_INVALID, newtokenline, false, false, false, false, LUM_NO_UPDATE);
                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line is invalidated to CLS_INVALID state" << endl;
                    LOG_VERBOSE_END

                }
                else
                {
                    assert(newtokenline == GetTotalTokenNum());

                    UpdateCacheLine(line, req, CLS_OWNER, newtokenline, false, false, true, false, LUM_NO_UPDATE);

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line is not pending anymore, at M" << endl;
                    LOG_VERBOSE_END

                }

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "write reply return to processor" << endl;
                LOG_VERBOSE_END

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

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
                    if ((tokennotify > 0) && (LinkMGS::s_oLinkConfig.m_nDirectory > 0))
                    {
                        ST_request *reqnotify = new ST_request(req);
                        ADD_INITIATOR_NODE(reqnotify, this);
                        reqnotify->type = Request_LOCALDIR_NOTIFICATION;
                        reqnotify->tokenacquired = tokennotify;
                        //m_pReqCurPASasSlave = newreq;
                        reqnotify->starttime = sc_time_stamp().to_double();

                        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                            clog << LOG_HEAD_OUTPUT << " NOTIFY  request address " << FMT_ADDR(address) << hex << " " << reqnotify << endl;
                        LOG_VERBOSE_END
            
                        InsertPASNodeRequest(reqnotify); 
                    }
#endif
                }
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
                // resolve evicted lines short of data problem in directory configuration
                // need to resend the request again
                // REVISIT JXXX, maybe better solutions
                // JOYING or maybe just delay a little bit
                else if (line->tokencount + req->tokenacquired < GetTotalTokenNum())
                {
                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << "AT address " << FMT_ADDR(address) << " hit at CLS_OWNER line without tokens, to be dispatched to again to get toekns @ set " << FMT_SETIDX(CacheIndex(address)) << " Invalidated?" << (line->invalidated?"yes":"no") << endl;
                        print_cline_data(line);
                    LOG_VERBOSE_END


                    // should only happen with directory
                    if (LinkMGS::s_oLinkConfig.m_nDirectory == 0)
                        assert(false);

                    req->bprocessed = true;

                    // just send it again
                    InsertPASNodeRequest(req);
                    return;
                }

                req->bprocessed = false;
#endif


                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                print_cline_data(line);
                LOG_VERBOSE_END
  
                assert((line->tokencount + req->gettokenpermanent()) == GetTotalTokenNum());
                assert(line->tlock == false);

                // check whether the line is complete   // JXXX
                assert(CheckLineValidness(line));

                pop_initiator_node(req);

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "token acquisition finished. " << req->tokenacquired << " tokens acquired by request, and line has " << line->tokencount << " tokens." << endl;
                LOG_VERBOSE_END


                // CHKS: assume no needs for the same update again. this might leave the value from another write
                UpdateCacheLine(line, req, line->state, line->gettokenglobalvisible() + req->gettokenpermanent(), false, false, true, false, LUM_NO_UPDATE);

                UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "cache line is not pending anymore, at M" << endl;
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "write reply return to processor" << endl;
                LOG_VERBOSE_END

                // save reply request
                InsertSlaveReturnRequest(false, req);
                //m_pReqCurPASasSlave = req;
            }

            OnPostAcquirePriorityToken(line, req);
        }
    }
}

// network remote request to acquire token and data     // RE, RS, SR, ER
void CacheL2TOKIM::OnAcquireTokenDataRem(ST_request* req)
{
	__address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

	// locate certain set
	cache_line_t* line = LocateLineEx(address);

    assert(req->tokenrequested <= GetTotalTokenNum());

    // create backward invalidation // for RE, ER
    if ((m_nBBIPolicy == BBI_LAZY)&&(req->tokenrequested == GetTotalTokenNum()))
    {
        ST_request *newreq = new ST_request(req);
        //req->address = (address>>m_nLineBits)<<m_nLineBits;  // alert
        //ADD_INITIATOR_NODE(newreq, this);
		ADD_INITIATOR_NODE(newreq, (void*)NULL);
        newreq->type = REQUEST_INVALIDATE_BR;
        InsertSlaveReturnRequest(false, newreq);
        //m_pReqCurPASasSlave = newreq;
        newreq->starttime = sc_time_stamp().to_double();

        //nib++;
        //vecpib.push_back(newreq);
    }

    // handle invalid state, or say, the line cannot be found
    if (line == NULL)   // I
    {
        LOG_VERBOSE_BEGIN(VERBOSE_ALL)
            clog << LOG_HEAD_OUTPUT << "line not found in cache" << endl;
        LOG_VERBOSE_END

		LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
		    clog << LOG_HEAD_OUTPUT << "AD passed at address " << FMT_ADDR(req->getreqaddress()) << endl;
		LOG_VERBOSE_END

        // line miss, do extra stuff
        LineMissedExtra(req);

		// save the current request
        InsertPASNodeRequest(req);
		//m_pReqCurPASasNode = req;

        // invalidate line and l1 lines when invalidated
        if ((m_nBBIPolicy == BBI_LAZY)&&(req->tokenrequested == GetTotalTokenNum()))
            LineInvalidationExtra(req, true);

		return; // -- remove else -- 
    }

    assert(line->state != CLS_INVALID);
    assert(req->getlineaddress() == line->getlineaddress(CacheIndex(req->getlineaddress()), m_nSetBits));

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
                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line has only one token, request require one token, just pass the req however take the data" << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD passed @ address " << FMT_ADDR(req->getreqaddress()) << ", acquired " << line->tokencount << "tokens" << endl;
                    LOG_VERBOSE_END
                       

                    UpdateRequest(req, line, req->type, address, true, req->bpriority, req->btransient, req->tokenacquired);

                    // JOYING distinguish about modified data and normal data

                }
                else
                {
                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line gave up all the tokens(invalidated) on remote AD request" << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD passed @ address " << FMT_ADDR(req->getreqaddress()) << ", acquired " << line->tokencount << "tokens" << endl;
                    LOG_VERBOSE_END

                    // extra line invalidation method
                    // JONY ALSERT
                    //LineInvalidationExtra(req, true);

                    // update request
                    UpdateRequest(req, line, req->type, address, true, req->bpriority||line->priority, req->btransient, req->tokenacquired+line->gettokenglobalvisible(), RUM_NON_MASK);

                    // update line
                    UpdateCacheLine(line, req, CLS_INVALID, 0, false, false, false, false, LUM_NO_UPDATE);

                    // invalidate line and l1 lines when invalidated
                    if ((m_nBBIPolicy == BBI_LAZY)&&(req->tokenrequested == GetTotalTokenNum()))
                        LineInvalidationExtra(req, true);

                }
            }
            else
            {
                assert(line->priority == true);
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << " AD with probing tokens address " << FMT_ADDR(address) << " hit at set pending probing line " << FMT_SETIDX(CacheIndex(address)) << endl;
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "cache line gave up all the tokens(invalidated) on remote AD request" << endl;
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << " AD acquires the priority token, all transient request will be transformed passed @ address " << FMT_ADDR(req->getreqaddress()) << ", acquired " << line->tokencount << "tokens" << endl;
                LOG_VERBOSE_END

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

                // invalidate line and l1 lines when invalidated
                if ((m_nBBIPolicy == BBI_LAZY)&&(req->tokenrequested == GetTotalTokenNum()))
                    LineInvalidationExtra(req, true);
            }
        }
        else    // only give out some tokens 
        {
            assert(req->btransient == false);

            unsigned int newlinetoken = line->gettokenglobalvisible() - (req->tokenrequested - req->tokenacquired);

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << " AD address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
            LOG_VERBOSE_END

            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "cache line gives up " << (req->tokenrequested-req->tokenacquired) << " tokens by remote AD request" << endl;
            LOG_VERBOSE_END

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << " AD passed @ address " << FMT_ADDR(req->getreqaddress()) << ", acquired " << (req->tokenrequested-req->tokenacquired) << "tokens( and data update)" << endl;
            LOG_VERBOSE_END

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
        // mark for statistics
        if (!req->bmerged)
            MemStatMarkConflict(req->ref);

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

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line at Sharer Pending state has " << line->tokencount <<  " tokens and may have data on AD request, and data and tokens may share with request" << endl;
                    LOG_VERBOSE_END

                    unsigned int ntokentoacq = 0;
                    unsigned int ntokenlinenew = 0;
                    // CHKS: ALERT: some policy needs to be made to accelerate the process
                 
                    ntokenlinenew = line->tokencount;
                    ntokentoacq = 0;

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD passed @ address " << FMT_ADDR(req->getreqaddress()) << ", acquired " << ntokentoacq << "tokens" << endl;
                    LOG_VERBOSE_END

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

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line at Sharer Pending state has " << line->tokencount <<  " tokens and may has data on AD request, and data and tokens may share with request" << endl;
                    LOG_VERBOSE_END

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

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD passed @ address " << FMT_ADDR(req->getreqaddress()) << ", acquired " << dec << ntokentoacqmore << " tokens " << endl;
                    print_request(req);
                    print_cline(line, false);
                    LOG_VERBOSE_END

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
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << " AD address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
                    print_cline_data(line);
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "cache line at Onwer Pending state has " << line->tokencount <<  " tokens and may has data on AD request, and data and tokens may share with request" << endl;
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << " AD passed @ address " << FMT_ADDR(req->getreqaddress()) << ", lost " << req->tokenacquired - newtokenreq << " tokens" << endl;
                LOG_VERBOSE_END

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


                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << " AD address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
                LOG_VERBOSE_END

                // update request
                UpdateRequest(req, line, req->type, address, true, req->bpriority||line->priority, req->btransient, newtokenreq, (line->IsLineAtCompleteState()?RUM_ALL:RUM_NON_MASK));

                // update line
                UpdateCacheLine(line, req, line->state, newtokenline, line->pending, true, false, line->tlock, LUM_INCRE_OVERWRITE);

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "cache line at Sharer Pending state has now " << line->tokencount <<  " tokens and set/maintain invalidated flag" << endl;
                    print_cline_data(line);
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << " AD passed @ address " << FMT_ADDR(req->getreqaddress()) << ", acquired " << newtokenreq - req->tokenacquired << " tokens" << endl;
                LOG_VERBOSE_END

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

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line at Owner Pending state gives up " << line->tokencount - newtokenline <<  " tokens on AD request, and set/maintain invalidated flag" << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD passed @ address " << FMT_ADDR(req->getreqaddress()) << ", acquired " << newtokenreq - req->tokenacquired << "tokens" << endl;
                    LOG_VERBOSE_END

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

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line at Owner Pending state keeps tokens on AD request" << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD passed @ address " << FMT_ADDR(req->getreqaddress()) << ", lost all " << req->tokenacquired << " tokens" << endl;
                    LOG_VERBOSE_END

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

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD address " << FMT_ADDR(address) << " hit at set " << FMT_SETIDX(CacheIndex(address)) << endl;
                        print_cline_data(line);
                    LOG_VERBOSE_END

                    // update request, rip the available token off to the request
                    UpdateRequest(req, line, req->type, address, true, req->bpriority, true, newtokenreq, RUM_NON_MASK);

                    // update line, and keep the state, and 
                    IVUpdateCacheLine(line, req, line->state, newtokenline, line->pending, true, line->priority, true, LUM_RAC_FEEDBACK_UPDATE);

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line at Owner Pending state keeps tokens on AD request" << endl;
                        print_cline_data(line);
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << " AD passed @ address " << FMT_ADDR(req->getreqaddress()) << ", without losing any tokens, t/l" << endl;
                        print_request(req);
                    LOG_VERBOSE_END

                }

            }

            // save the current request
            InsertPASNodeRequest(req);
            //m_pReqCurPASasNode = req;
        }
    }
}

// network request return, with token and data  // RS, SR, RE, ER
void CacheL2TOKIM::OnAcquireTokenDataRet(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate certain set
    cache_line_t* line = LocateLineEx(address);

    assert(line != NULL);

    assert(line->state != CLS_INVALID);

    if (!line->pending)     // non-pending states   // S, E, O, M
    {
        assert(false);
    }
    else    // pending states       // R, T, P, U, W
    {
        if (req->tokenrequested < GetTotalTokenNum())   // read, // RS, SR
        {
            if (line->state == CLS_SHARER)  // reading, before  // R, T
            {
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
                // resolve evicted lines short of data problem in directory configuration
                // need to resend the request again
                // REVISIT JXXX, maybe better solutions
                if ((req->dataavailable == false) && (line->IsLineAtCompleteState() == false))
                {
                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << "AD address " << FMT_ADDR(address) << " hit at CLS_SHARER line without data, to be dispatched to again to get data @ set " << FMT_SETIDX(CacheIndex(address)) << " Invalidated?" << (line->invalidated?"yes":"no") << endl;
                        print_cline_data(line);
                    LOG_VERBOSE_END


                    // should only happen with directory
                    if (LinkMGS::s_oLinkConfig.m_nDirectory == 0)
                        assert(false);

                    req->bprocessed = true;

                    // just send it again
                    InsertPASNodeRequest(req);
                    return;
                }

                req->bprocessed = false;
#endif

                assert(req->btransient == false);
                unsigned int tokenlinenew = line->tokencount + req->gettokenpermanent();
                bool newlinepriority = line->priority || req->bpriority;


                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "AD address " << FMT_ADDR(address) << " hit at valid CLS_SHARER line @ set " << FMT_SETIDX(CacheIndex(address)) << " Invalidated?" << (line->invalidated?"yes":"no") << endl;
                    print_cline_data(line);
                LOG_VERBOSE_END

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "AD done address " << FMT_ADDR(req->getlineaddress()) << endl;
                    clog << LOG_HEAD_OUTPUT << "RR will be sent to L1 @ address " << FMT_ADDR(req->getreqaddress()) << endl;
                LOG_VERBOSE_END

                // pop the initiator
                pop_initiator_node(req);

                bool evictthereturnedtokens = 0;
                if ((LinkMGS::s_oLinkConfig.m_nDirectory > 0) && (line->invalidated) && (req->btransient == false) && (req->tokenacquired > 0))
                {
                    evictthereturnedtokens = req->tokenacquired;
                }

                // instead of updating the cache line, the reqeust should be updated first
                // update request from the line
                //UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY_X, address);
                UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY, address, true, false, false, 0xffff, (line->IsLineAtCompleteState()?RUM_ALL:RUM_NONE));

                if ((line->invalidated)||(tokenlinenew==0))
                {
                    //assert(tokenlinenew == 0);
                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line is invalidated(or failed to acquire any tokens) on return request" << endl;
                        print_cline_data(line);
                    LOG_VERBOSE_END
                }
                else
                {
                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line change to non-pending state" << endl;
                    LOG_VERBOSE_END
                }

                // update time and state
                if ((line->invalidated)||(tokenlinenew == 0))
                {
                    //assert(tokenlinenew == 0);
                    UpdateCacheLine(line, req, CLS_INVALID, 0, false, false, false, false, LUM_NO_UPDATE);
                    // TOFIX JXXX backward invalidate L1 caches.

                    // in multi-level configuration, there's a chance that an AD request could travel twice(first time reload), and return at invalidated state with non-transient tokens. 
                    // in this case, the non-transient tokens should be dispatched again with disseminate request
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
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
                        ADD_INITIATOR_NODE(reqevresend, this);

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

                        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                            clog << LOG_HEAD_OUTPUT << " Disseminate " << evictthereturnedtokens << " tokens @address " << FMT_ADDR(address) << hex << " " << reqevresend << endl;
                        LOG_VERBOSE_END
            
                        InsertPASNodeRequest(reqevresend); 
                    }
#endif

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


#ifdef OPTIMIZATION_READ_REPLY_FLUSHING
                // $$$ read reply flushing $$$
                ReadReplyFlushing(req);
#endif

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


                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "AD address " << FMT_ADDR(address) << " hit at CLS_OWNER line (previously write-after-read), data @ set " << FMT_SETIDX(CacheIndex(address)) << endl;
                    print_cline_data(line);
                LOG_VERBOSE_END


                unsigned int tokenlinenew = line->tokencount + req->gettokenpermanent();
                bool newlinepriority = line->priority || req->bpriority;


                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "AD done address " << FMT_ADDR(req->getlineaddress()) << endl;
                    clog << LOG_HEAD_OUTPUT << "RR will be sent to L1 @ address " << FMT_ADDR(req->getreqaddress()) << endl;
                LOG_VERBOSE_END


                UpdateCacheLine(line, req, line->state, tokenlinenew, line->pending, line->invalidated, newlinepriority, false, LUM_NO_UPDATE);

                // instead of updating the cache line, the reqeust should be updated first
                // update request from the line
                //UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY_X, address);
                UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY, address, true, false, false, 0xffff, (line->IsLineAtCompleteState()?RUM_ALL:RUM_NONE));

                // pop the initiator
                pop_initiator_node(req);

                line->breserved = false;

                //OnPostAcquirePriorityToken(line, req);


#ifdef OPTIMIZATION_READ_REPLY_FLUSHING
                // $$$ read reply flushing $$$
                ReadReplyFlushing(req);
#endif

                // save reply request
                InsertSlaveReturnRequest(false, req);
                //m_pReqCurPASasSlave = req;

            }
        }
        else    // write, // RE, ER, (or maybe RS, SR when GetTotalTokenNum() == 1)
        {
            if (line->state == CLS_SHARER)  // actually reading, before  // R, T
            {
                if (GetTotalTokenNum() == 1)
                {
                    // line is shared but also exclusive (not dirty not owner)
                   
                    assert(!line->invalidated); 
                    assert(line->tlock == false);
                    assert(req->btransient == false);

                    unsigned int tokenlinenew = line->tokencount + req->tokenacquired;

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << "AD address " << FMT_ADDR(address) << " hit at valid CLS_SHARER line @ set " << FMT_SETIDX(CacheIndex(address)) << " Invalidated?" << (line->invalidated?"yes":"no") << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << "AD done address " << FMT_ADDR(req->getlineaddress()) << endl;
                        clog << LOG_HEAD_OUTPUT << "RR will be sent to L1 @ address " << FMT_ADDR(req->getreqaddress()) << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line change to non-pending state" << endl;
                    LOG_VERBOSE_END

                    // update time and state
                    if (line->tokencount == 0)
                        UpdateCacheLine(line, req, line->state, tokenlinenew, false, false, true, false, LUM_INCRE_OVERWRITE);
                    else
                        assert(false);

                    // pop the initiator
                    pop_initiator_node(req);

                    // instead of updating the cache line, the reqeust should be updated first
                    // update request from the line
                    //UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY_X, address);
                    UpdateRequest(req, line, MemoryState::REQUEST_READ_REPLY, address, true, false, false, 0xffff);       // ALERT

#ifdef OPTIMIZATION_READ_REPLY_FLUSHING
                    // $$$ read reply flushing $$$
                    ReadReplyFlushing(req);
#endif

                    // save reply request
                    InsertSlaveReturnRequest(false, req);
                    //m_pReqCurPASasSlave = req;

                    OnPostAcquirePriorityToken(line, req);
                }
                else
                    assert(false);
            }
            else    // writing, before  // P, U, W
            {
                unsigned int newtokenline=0;
                unsigned int newtokenreq=0;

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            unsigned int tokennotify = (req->btransient)?req->tokenacquired:0;
#endif

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
                        assert(false);
                        // all transient tokens became normal
                        if (req->btransient)
                        {
                            req->btransient = false;

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
                            if ((tokennotify > 0) && (LinkMGS::s_oLinkConfig.m_nDirectory > 0))
                            {
                                ST_request *reqnotify = new ST_request(req);
                                reqnotify->tokenacquired = tokennotify;
                                ADD_INITIATOR_NODE(reqnotify, this);
                                reqnotify->type = Request_LOCALDIR_NOTIFICATION;
                                //m_pReqCurPASasSlave = newreq;
                                reqnotify->starttime = sc_time_stamp().to_double();

                                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                                    clog << LOG_HEAD_OUTPUT << " NOTIFY  request address " << FMT_ADDR(address) << hex << " " << reqnotify << endl;
                                LOG_VERBOSE_END
                    
                                InsertPASNodeRequest(reqnotify); 
                            }
#endif
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
                    pop_initiator_node(req);

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << "Token acquisition finished at invalidated state, " << req->tokenacquired << " tokens acquired " << endl;
                    LOG_VERBOSE_END

                    if (newtokenline == 0)
                    {
                        UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

                        // extra line invalidation method
                        LineInvalidationExtra(req, false);
                        // CHKS: assume no needs for the same update again. this might leave the value from another write
                        UpdateCacheLine(line, req, CLS_INVALID, newtokenline, false, false, false, false, LUM_NO_UPDATE);
                        LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                            clog << LOG_HEAD_OUTPUT << "cache line is invalidated to CLS_INVALID state" << endl;
                        LOG_VERBOSE_END

                    }
                    else
                    {
                        assert(newtokenline == GetTotalTokenNum());

                        UpdateCacheLine(line, req, CLS_OWNER, newtokenline, false, false, true, false, (line->tokencount == 0 )?LUM_INCRE_OVERWRITE:LUM_NO_UPDATE);

                        UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

                        LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                            clog << LOG_HEAD_OUTPUT << "cache line is not pending anymore, at M" << endl;
                        LOG_VERBOSE_END

                     }


                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "write reply return to processor" << endl;
                    LOG_VERBOSE_END

                    // save reply request
                    if (req->bmerged)
                    {
                        //print_request(req, true);

                        assert(req->ref != NULL);
                        vector<ST_request*>* pvec = (vector<ST_request*>*)req->ref;
                        for (unsigned int i=0;i<pvec->size();i++)
                        {
                            InsertSlaveReturnRequest(false, (*pvec)[i]);

                            // mark for statistics
                            if (i==0)
                                MemStatMarkConflict(((*pvec)[i])->ref);
                        }

                        pvec->clear();
                        delete pvec;

                        OnPostAcquirePriorityToken(line, req);

                        delete req;
                        //assert(false);
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
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
                    // resolve evicted lines short of data problem in directory configuration
                    // need to resend the request again
                    // REVISIT JXXX, maybe better solutions
                    // JOYING or maybe just delay a little bit
                    if (line->tokencount + req->tokenacquired < GetTotalTokenNum())
                    {
                        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                            clog << LOG_HEAD_OUTPUT << "AD(EX) address " << FMT_ADDR(address) << " hit at CLS_OWNER line without data or tokens, to be dispatched to again to get data and toekns @ set " << FMT_SETIDX(CacheIndex(address)) << " Invalidated?" << (line->invalidated?"yes":"no") << endl;
                            print_cline_data(line);
                        LOG_VERBOSE_END


                        // should only happen with directory
                        if (LinkMGS::s_oLinkConfig.m_nDirectory == 0)
                            assert(false);

                        req->bprocessed = true;

                        // just send it again
                        InsertPASNodeRequest(req);
                        return;
                    }

                    req->bprocessed = false;
#endif

                    // double check the request and the line get all the tokens
                    assert((line->tokencount + req->tokenacquired) == GetTotalTokenNum());
                    assert(line->tlock == false);
                    //print_request(req);
                    if (req->btransient)
                        assert(line->priority);

                    // check whether the line is complete   // JXXX
                    if (req->tokenacquired == 0)
                        assert(CheckLineValidness(line));

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << "token acquisition finished. " << req->tokenacquired << " tokens acquired by request, and line has " << line->tokencount << " tokens." << endl;
                    LOG_VERBOSE_END

                    UpdateCacheLine(line, req, line->state, line->tokencount + req->tokenacquired, false, false, true, false, (line->tokencount == 0)?LUM_INCRE_OVERWRITE:LUM_NO_UPDATE);

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "cache line is not pending anymore, at M" << endl;
                    LOG_VERBOSE_END

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "write reply return to processor" << endl;
                    LOG_VERBOSE_END

                    // save reply request
                    pop_initiator_node(req);

                    UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

                    req->type = REQUEST_WRITE_REPLY;

                    if (req->bmerged)
                    {
                        //print_request(req, true);

                        assert(req->ref != NULL);
                        vector<ST_request*>* pvec = (vector<ST_request*>*)req->ref;
                        for (unsigned int i=0;i<pvec->size();i++)
                        {
                            InsertSlaveReturnRequest(false, (*pvec)[i]);

                            // mark for statistics
                            if (i==0)
                                MemStatMarkConflict(((*pvec)[i])->ref);
                        }

                        pvec->clear();
                        delete pvec;

                        OnPostAcquirePriorityToken(line, req);

                        delete req;
                        //assert(false);
                    }
                    else
                    {
                        InsertSlaveReturnRequest(false, req);

                        OnPostAcquirePriorityToken(line, req);
                    }
                    //m_pReqCurPASasSlave = req;
                }
            }
        }
    }
}

// network disseminate token and data, EV, WB, include IJ
void CacheL2TOKIM::OnDisseminateTokenData(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate line
    cache_line_t* line = LocateLineEx(address);

    // handle INVALID state
    if (line == NULL)
    {
        if (m_nInjectionPolicy == IP_NONE)
        {
            // pass the transaction down 
            // this can change according to different strategies
            // JXXX

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "DD passed address " << FMT_ADDR(req->getreqaddress()) << "at invalid" << endl;
            LOG_VERBOSE_END

            // line miss, do extra stuff
            LineMissedExtra(req);

            // save the current request
            InsertPASNodeRequest(req);
            //m_pReqCurPASasNode = req;

            // request will be passed to the next node
        }
        else if (m_nInjectionPolicy == IP_EMPTY_1EJ)
        {
            line = GetEmptyLine(address);

            if (line == NULL)
            {
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "DD passed address " << FMT_ADDR(req->getreqaddress()) << "at invalid, with no emptylines" << endl;
                LOG_VERBOSE_END

                // line miss, do extra stuff
                LineMissedExtra(req);

                // save the current request
                InsertPASNodeRequest(req);
                //m_pReqCurPASasNode = req;
            }
            else
            {
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "DD address " << FMT_ADDR(req->getreqaddress()) << " injected at invalid" << endl;
                LOG_VERBOSE_END

                // update line info
                if (req->tokenrequested == GetTotalTokenNum()) // WB
                {
                    UpdateCacheLine(line, req, CLS_OWNER, req->tokenacquired, false, false, req->bpriority, false, LUM_FEEDBACK_UPDATE);
                }
                else
                {
                    UpdateCacheLine(line, req, CLS_SHARER, req->tokenacquired, false, false, req->bpriority, false, LUM_FEEDBACK_UPDATE);
                }
                //UpdateCacheLine(pline, req, LNREADPENDING, LUM_NO_UPDATE);

                // terminate request
                delete req;
            }

            
        }
        else
        {
            assert(false);
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

        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "DD gives all " << req->tokenacquired << " tokens to the line at "<< FMT_ADDR(req->getlineaddress()) << " currently with " << line->tokencount << " tokens." << endl;
        LOG_VERBOSE_END

        // JXXX policy maybe changed 
        LOG_VERBOSE_BEGIN(VERBOSE_STATE)
            clog << LOG_HEAD_OUTPUT << "DD request is terminated " << endl;
        LOG_VERBOSE_END

        bool acquiredp = false;
        if ((req->bpriority)&&(!line->priority))
            acquiredp = true;

        assert(line->tlock == false);
        UpdateCacheLine(line, req, ((req->tokenrequested==GetTotalTokenNum())?CLS_OWNER:line->state), line->tokencount+req->tokenacquired, line->pending, line->invalidated, line->priority||req->bpriority, line->tlock, LUM_NO_UPDATE);

        if (req->tokenrequested==GetTotalTokenNum())
        {
            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "Cache Line is updated " << endl;
                print_cline(line);
            LOG_VERBOSE_END
        }

        if (acquiredp)
            OnPostAcquirePriorityToken(line, req);

        // terminate the request
        delete req;

        // do not send anything away anymore
        //InsertPASNodeRequest(req);
        //m_pReqCurPASasNode = NULL;
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
 
            if (line->state == CLS_OWNER)
                assert(false);

            InsertPASNodeRequest(req);
            //m_pReqCurPASasNode = req;
        }
        else                        // R, P, W
        {
            if (line->state == CLS_SHARER)  // reading, before  // R
            {
                // give all the tokens of the request to the line
                unsigned int tokenlinenew = line->tokencount + req->tokenacquired;

                assert(tokenlinenew<=GetTotalTokenNum());

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "DD request @ " << FMT_ADDR(address) << " gives up " << req->tokenacquired << " tokens, hit at CLS_SHARER pending line @ set " << FMT_SETIDX(CacheIndex(address)) << " currently has " << line->tokencount << " tokens" << endl;
                LOG_VERBOSE_END

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
                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "DD request is terminated " << ((req->tokenrequested == GetTotalTokenNum())?"and line is reserved to be ownerline later":"") << endl;
                LOG_VERBOSE_END

                // terminate the request
                delete req;

                // do not send anything away anymore
                //InsertPASNodeRequest(req);
                //m_pReqCurPASasNode = NULL;
            }
            else    // writing, before  // P, W
            {
                assert(line->tlock == false);
                // give all the tokens of the request to the line
                unsigned int tokenlinenew = line->tokencount + req->tokenacquired;

                assert(tokenlinenew<=GetTotalTokenNum());

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "DD request @ " << FMT_ADDR(address) << " gives up " << req->tokenacquired << " tokens, hit at CLS_SHARER pending line @ set " << FMT_SETIDX(CacheIndex(address)) << " currently has " << line->tokencount << " tokens" << endl;
                LOG_VERBOSE_END

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
                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "DD request is terminated " << endl;
                LOG_VERBOSE_END

                // terminate the request
                delete req;

                // do not send anything away anymore
                //InsertPASNodeRequest(req);
                //m_pReqCurPASasNode = NULL;
            }
        }
    }
}


void CacheL2TOKIM::OnPostAcquirePriorityToken(cache_line_t* line, ST_request* req)
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

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "MSB cleanup on Owner non-pending" << hex << address << " line updated" << endl;
            LOG_VERBOSE_END

            // and append all the queued request on the msb slot to a return buffer
            for (unsigned int i=0;i<queuedrequests->size();i++)
            {
                InsertSlaveReturnRequest(false, (*queuedrequests)[i]);
            }

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << queuedrequests->size() << " " << endl;
                clog << LOG_HEAD_OUTPUT << "MSB append the finished the requests to return queue" << endl;
            LOG_VERBOSE_END

            // remove the merged request, remove the msb slot
            m_msbModule.CleanSlot(address);
        }
        else
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "MSB on Owner pending" << hex << address << " the line will get the priority token sooner or later" << endl;
                print_cline_data(line);
                print_request(mergedreq);
            LOG_VERBOSE_END


            // CHKS, alert
            // the line might not be able to write directly if they are from differnt families. 

            UpdateCacheLine(line, mergedreq, line->state, line->tokencount, line->pending, line->invalidated, line->priority, line->tlock, LUM_PRIMARY_UPDATE);

            // and append all the queued request on the msb slot to a return buffer
            for (unsigned int i=0;i<queuedrequests->size();i++)
            {
                InsertSlaveReturnRequest(false, (*queuedrequests)[i]);
            }

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "MSB done on the line and append the finished the requests to return queue" << endl;
                print_cline_data(line);
            LOG_VERBOSE_END

            // remove the merged request, remove the msb slot
            m_msbModule.CleanSlot(address);

            // assert(false);  // JONY ALERT JONY_ALERT
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

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "MSB merged request on shared lines and change the line to owner pending state, @" << hex << address << " line updated@" << endl;
                print_cline_data(line);
            LOG_VERBOSE_END

            merge2send = new ST_request(mergedreq);
            ADD_INITIATOR_NODE(merge2send, this);

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "MSB merged request send to node with link to return request queue" << endl;
                print_request(merge2send);
            LOG_VERBOSE_END

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

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "MSB cleanup on Sharer state with no/0 tokens @" << hex << address << " line updated@" << endl;
            LOG_VERBOSE_END

            // then send it from the network interface
            // Duplicate the merged request

            merge2send = new ST_request(mergedreq);
            ADD_INITIATOR_NODE(merge2send, this);

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "MSB merged request send to node with link to return request queue" << endl;
                print_request(merge2send);
            LOG_VERBOSE_END

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

        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "MSB merged request on INVALID lines and change the line to owner pending state, @" << hex << address << " line updated@" << endl;
        LOG_VERBOSE_END

        merge2send = new ST_request(mergedreq);
        ADD_INITIATOR_NODE(merge2send, this);

        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "MSB merged request send to node with link to return request queue" << endl;
            print_request(merge2send);
        LOG_VERBOSE_END

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



#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
void CacheL2TOKIM::OnDirNotification(ST_request* req)
{
    // request meant for directory, shouldn't receive it again
    if (IS_NODE_INITIATOR(req, this))
    {
        assert(false);
    }

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "Notification request received, pass it on - " << req << endl;
    LOG_VERBOSE_END


    // just pass it by
    InsertPASNodeRequest(req);
    //m_pReqCurPASasNode = req;
}
#endif





// virtual function for find replacement line
cache_line_t* CacheL2TOKIM::GetReplacementLineEx(__address_t address)
{
#define __PREFERED_REPLACE_OWNER__    // if we are currently comparing shared lines, 
                                        // then we dont care about modified and owned lines
                                        // since we always choose shared lines before dirty lines

    cache_line_t *line, *linelru;     // temp variables
#ifdef __PREFERED_REPLACE_OWNER__
    cache_line_t *linelruw = NULL; // replacement line for write-back request
#endif
    cache_line_t *linelrue = NULL; // replacement line for eviction request
    unsigned int index = CacheIndex(address);
    // uint64 tag = CacheTag(address);

    unsigned int nunlocked = 0;

#ifdef __PREFERED_REPLACE_OWNER__
    bool bcompareshared = false;
#endif

    linelru = line = &(m_pSet[index].lines[0]);
    for (unsigned int i=0; i<m_nAssociativity; i++, line++)
    {
        // return the first found empty one
        if (line->state == CLS_INVALID)
            return line;

        // count the unlocked lines
        if (line->pending)
        {
            nunlocked ++;
            // this one doesn't count as normal replacable lines
            continue;
        }

#ifdef __PREFERED_REPLACE_OWNER__
        if (line->state == CLS_SHARER)
        {
            // from now on only compare shared lines
            bcompareshared = true;
            if ( (linelrue == NULL) || (line->time < linelrue->time) )
                linelrue = line;
        }
        else if ( (!bcompareshared)&&(line->state == CLS_OWNER) )
        {
            if ( (linelruw == NULL) || (line->time < linelruw->time) )
                linelruw = line;
        }
#else
        // do not distinguish dirty or shared lines
        // from now on only compare shared lines
        if ( (linelrue == NULL) || (line->time < linelrue->time) )
            linelrue = line;
#endif
    }

    // all the lines are occupied by the locked state
    if (nunlocked == m_nAssociativity)
        return NULL;

#ifdef __PREFERED_REPLACE_OWNER__
    linelru = (bcompareshared)?linelrue:linelruw;
#else
    linelru = linelrue;
#endif

    // for RP_RND
    // for random policy we dont care about whether it's shared, owned or modified
    if (m_policyReplace == RP_RND)
    {
        assert(false);
    }

    return linelru;
}






void CacheL2TOKIM::ReviewState(REVIEW_LEVEL rev)
{
    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)   
    // switch to hexdecimal
    //DEBUG_TEST_XXX

    clog << hex;
    clog << endl << "|||========================||| [* " << name() << " *]" << " <[P:" << channel_fifo_slave.num_available_fast() << "]>" << " <[N:" << m_fifoinNetwork.num_available_fast() << "]>" << " |||========================|||" << endl;

    //////////////////////////////////////////////////////////////////////////
    
    clog << "Global FIFO: [" << dec << GlobalFIFONumberAvailable() << "]" << endl;
    int queuesize = GlobalFIFONumberAvailable();
    // get and request one by one 
    for (int i=0;i<queuesize;i++)
    {
        ST_request* reqtemp=NULL;
        GlobalFIFONBRead(reqtemp);
        print_request(reqtemp);
        GlobalFIFONBWrite(reqtemp);
    }
    
    //////////////////////////////////////////////////////////////////////////
    // print pipeline
    clog << "-- Pipeline INI --" << endl;
    m_pPipelineINI->print();
    clog << "-- Pipeline PAS --" << endl;
    m_pPipelinePAS->print();
    clog << endl;


    //////////////////////////////////////////////////////////////////////////

    char ptext[0x500];
    char tempstatename[30];

    // print out state
    switch(rev)
    {
    case SO_REVIEW_NONE:
        break;

    case SO_REVIEW_BASICS:
        {
            // check through all the cachelines and print only the valid ones 
            for (unsigned int i=0;i<m_nSet;i++)
            {
                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    cache_line_t &line = m_pSet[i].lines[j];

                    // skip invalid lines
                    if (line.state == CLS_INVALID)
                        continue;

                    // print a cacheline
                    // print the line set and no. within a set
                    __address_t addr = line.getlineaddress(i, m_nSetBits);
                    clog << hex << "<Set>0x" << i << "-<no>0x" << j << " [" << addr << ", " << addr + s_nLineSize << "] : " << CacheStateName(line.state, line.tokencount, line.pending, line.invalidated, line.priority, line.tlock, tempstatename) << " |0X";


					ptext[0] = '\0';

                    for (unsigned int biti=0;biti<CACHE_BIT_MASK_WIDTH/8;biti++)
                        sprintf(ptext, "%s%02x", ptext, line.bitmask[biti]);
                    clog << ptext << endl;
                }
            }
        }
        break;

    case SO_REVIEW_NORMAL:
        {
            // check through all the cachelines and print only the valid ones 
            for (unsigned int i=0;i<m_nSet;i++)
            {
                char str[100];
				str[0] = '\0';
				ptext[0] = '\0';
                bool hasvalid = false;
                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    cache_line_t &line = m_pSet[i].lines[j];

                    if (line.state != CLS_INVALID)
                        hasvalid |= true;

                    // skip invalid lines
                    sprintf(str, "%s%s", str, CacheStateName(line.state, line.tokencount, line.pending, line.invalidated, line.priority, line.tlock, tempstatename, true));

                }

                if (hasvalid)
                    clog << hex << "<Set>0x" << i << ": " << str << endl;

                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    cache_line_t &line = m_pSet[i].lines[j];

                    // skip invalid lines
                    if (line.state == CLS_INVALID)
                        continue;

                    // print a cacheline
                    // print the line set and no. within a set
                    __address_t addr = line.getlineaddress(i, m_nSetBits);
                    clog << hex << "  " << "<N>0x" << j << " [" << addr << ", " << addr + s_nLineSize << "] : [" 
                        << CacheStateName(line.state, line.tokencount, line.pending, line.invalidated, line.priority, line.tlock, tempstatename) << "].[HEX]" << endl;

                    // print data
#ifdef SIMULATE_DATA_TRANSACTION
                    clog << setw(2);
                    unsigned int count=0;
                    for (unsigned int k=0;k<s_nLineSize;k++)
                    {
                        if (count == 0x20)
                        {
                            count = 0;
                            clog << endl;
                        }

                        if (count++ == 0)
                            clog << "  ";

                        unsigned int maskoffset = k/CACHE_REQUEST_ALIGNMENT;

                        // update the mask
                        unsigned int maskhigh = maskoffset / 8;
                        unsigned int masklow = maskoffset % 8;

                        if ( ((line.bitmask[maskhigh] & (1 << masklow)) == 0) && (!line.IsLineAtCompleteState()) )
                        {
#ifdef SPECIFIED_COLOR_OUTPUT
							__TEXTCOLOR_GRAY();
#else
                            __TEXTCOLOR(clog,__TEXT_BLACK,true);
#endif
                            clog << "-- ";
                            __TEXTCOLORNORMAL();
                        }
                        else if ( ((line.bitmask[maskhigh] & (1 << masklow)) == 0) && (line.IsLineAtCompleteState()) )
                        {
                            clog << setw(2);
                            clog << setfill('0');
                            clog << (unsigned int)(unsigned char)line.data[k] << " ";
                        }
                        else
                        {
                            if (!line.islinecomplete())
#ifdef SPECIFIED_COLOR_OUTPUT
								__TEXTCOLOR_CYAN();
#else
                                __TEXTCOLOR(clog, __TEXT_CYAN, true);
#endif
                            clog << setw(2);
                            clog << setfill('0');
                            clog << (unsigned int)(unsigned char)line.data[k] << " ";
                            __TEXTCOLORNORMAL();
                        }
                    }
#endif
                    clog << endl;
                }
            }
        }
        break;

    case SO_REVIEW_DETAIL:
        {
            // check through all the cachelines and print only the valid ones 
            for (unsigned int i=0;i<m_nSet;i++)
            {
                char str[100];
				str[0] = '\0';
				ptext[0] = '\0';
                bool hasvalid = false;
                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    cache_line_t &line = m_pSet[i].lines[j];

                    if (line.state != CLS_INVALID)
                        hasvalid |= true;

                    // skip invalid lines
                    sprintf(str, "%s%s", str, CacheStateName(line.state, line.tokencount, line.pending, line.invalidated, line.priority, line.tlock, tempstatename, true));

                }

                if (hasvalid)
                    clog << hex << "<Set>0x" << i << ": " << str << endl;

                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    cache_line_t &line = m_pSet[i].lines[j];

                    // skip invalid lines
                    if (line.state == CLS_INVALID)
                        continue;

                    // print a cacheline
                    // print the line set and no. within a set
                    __address_t addr = line.getlineaddress(i, m_nSetBits);
                    clog << hex << "  " << "<N>0x" << j << " [" << addr << ", " << addr + s_nLineSize << "] : [" 
                        << CacheStateName(line.state, line.tokencount, line.pending, line.invalidated, line.priority, line.tlock, tempstatename) << "].[HEX]" << endl;

                    // print data
#ifdef SIMULATE_DATA_TRANSACTION
                    clog << setw(2);
                    unsigned int count=0;
                    for (unsigned int k=0;k<s_nLineSize;k++)
                    {
                        if (count == 0x20)
                        {
                            count = 0;
                            clog << endl;
                        }

                        if (count++ == 0)
                            clog << "  ";

                        unsigned int maskoffset = k/CACHE_REQUEST_ALIGNMENT;

                        // update the mask
                        unsigned int maskhigh = maskoffset / 8;
                        unsigned int masklow = maskoffset % 8;

                        if ( ((line.bitmask[maskhigh] & (1 << masklow)) == 0) && (!line.IsLineAtCompleteState()) )
                        {
#ifdef SPECIFIED_COLOR_OUTPUT
							__TEXTCOLOR_GRAY();
#else
                            __TEXTCOLOR(clog,__TEXT_BLACK,true);
#endif
                            clog << "-- ";
                            __TEXTCOLORNORMAL();
                        }
                        else if ( ((line.bitmask[maskhigh] & (1 << masklow)) == 0) && (line.IsLineAtCompleteState()) )
                        {
#ifdef SPECIFIED_COLOR_OUTPUT
						   __TEXTCOLOR_YELLOW();
#else
                           __TEXTCOLOR(clog, __TEXT_YELLOW, false);
#endif
                            clog << setw(2);
                            clog << setfill('0');
                            clog << (unsigned int)(unsigned char)line.data[k] << " ";
                            __TEXTCOLORNORMAL();
                        }
                        else
                        {
                            if (!line.IsLineAtCompleteState())
#ifdef SPECIFIED_COLOR_OUTPUT
							    __TEXTCOLOR_GREEN();
#else
                                __TEXTCOLOR(clog, __TEXT_GREEN, true);
#endif
                            else if ((line.state == CLS_OWNER)&&(line.tokencount == GetTotalTokenNum())&&line.pending)
#ifdef SPECIFIED_COLOR_OUTPUT
						        __TEXTCOLOR_GREEN();
#else
								__TEXTCOLOR(clog, __TEXT_GREEN, true);
#endif
							else
                                __TEXTCOLORNORMAL();
                            clog << setw(2);
                            clog << setfill('0');
                            clog << (unsigned int)(unsigned char)line.data[k] << " ";
                            __TEXTCOLORNORMAL();
                        }
                    }
#endif
                    clog << endl;
                }
            }
        }
        break;

    case SO_REVIEW_ALL:
        {
            // check through all the cachelines and print only the valid ones 
            for (unsigned int i=0;i<m_nSet;i++)
            {
                clog << hex << "<Set>0x" << i << ": ";
                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    cache_line_t &line = m_pSet[i].lines[j];

                    // skip invalid lines
                    clog << CacheStateName(line.state, line.tokencount, line.pending, line.invalidated, line.priority, line.tlock, tempstatename, true);

                }
                clog << endl;

                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    cache_line_t &line = m_pSet[i].lines[j];

                    // skip invalid lines
                    if (line.state == CLS_INVALID)
                        continue;

                    // print a cacheline
                    // print the line set and no. within a set
                    __address_t addr = line.getlineaddress(i, m_nSetBits);
                    clog << hex << "  " << "<N>0x" << j << " [" << addr << ", " << addr + s_nLineSize << "] : [" 
                        << CacheStateName(line.state, line.tokencount, line.pending, line.invalidated, line.priority, line.tlock, tempstatename) << "].[HEX]" << endl;
                    
                    sprintf(ptext, "  [MASK]|0X");
                    for (unsigned int biti=0;biti<CACHE_BIT_MASK_WIDTH/8;biti++)
                        sprintf(ptext, "%s%x", ptext, line.bitmask[biti]);
                    clog << ptext << endl;

                    // print data
#ifdef SIMULATE_DATA_TRANSACTION
                    clog << setw(2);
                    unsigned int count=0;
                    for (unsigned int k=0;k<s_nLineSize;k++)
                    {
                        if (count == 0x20)
                        {
                            count = 0;
                            clog << endl;
                        }

                        if (count++ == 0)
                            clog << "  ";

                        unsigned int maskoffset = k/CACHE_REQUEST_ALIGNMENT;

                        // update the mask
                        unsigned int maskhigh = maskoffset / 8;
                        unsigned int masklow = maskoffset % 8;

                        if ( ((line.bitmask[maskhigh] & (1 << masklow)) == 0) && (!line.IsLineAtCompleteState()) )
                        {
#ifdef SPECIFIED_COLOR_OUTPUT
							__TEXTCOLOR_GRAY();
#else
                            __TEXTCOLOR(clog,__TEXT_BLACK,true);
#endif
                            clog << "-- ";
                            __TEXTCOLORNORMAL();
                        }
                        else if ( ((line.bitmask[maskhigh] & (1 << masklow)) == 0) && (line.IsLineAtCompleteState()) )
                        {
                            clog << setw(2);
                            clog << setfill('0');
                            clog << (unsigned int)(unsigned char)line.data[k] << " ";
                        }
                        else
                        {
                            if (!line.islinecomplete())
#ifdef SPECIFIED_COLOR_OUTPUT
								__TEXTCOLOR_CYAN();
#else
                                __TEXTCOLOR(clog,__TEXT_CYAN,true);
#endif
                            clog << setw(2);
                            clog << setfill('0');
                            clog << (unsigned int)(unsigned char)line.data[k] << " ";
                            __TEXTCOLORNORMAL();
                        }
                    }
#endif

                    clog << endl;
                }
            }
        }
        break;

    default:
        cerr << ERR_HEAD_OUTPUT << "error in review state" << endl;
        break;
    }

    clog << dec << endl;;
    LOG_VERBOSE_END
}

void CacheL2TOKIM::MonitorAddress(ostream& ofs, __address_t addr)
{
    cache_line_t* line = LocateLineEx(addr);
   
    if (line == NULL)
    {
        ofs << "I--  ";
        return;
    }
    // check whether it's a hit on the MSB
    bool hitonmsb = m_msbModule.IsAddressPresent(addr);

    // check whether the line is already locked
    bool lockonmsb = hitonmsb?m_msbModule.IsSlotLocked(addr, NULL):false;

    char pstatename[10];
    ofs << CacheStateName(line->state, line->tokencount, line->pending, line->invalidated, line->priority, line->tlock, hitonmsb, lockonmsb, pstatename, true);
}

#endif  // token coherence

