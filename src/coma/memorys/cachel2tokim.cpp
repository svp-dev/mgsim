#include "cachel2tokim.h"

#include "../simlink/linkmgs.h"

using namespace MemSim;

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
            if (req->nsize > m_nLineSize)
            {
                abort();
            }

            if ((req->nsize+req->offset) <= m_nLineSize)
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
            ST_request* reqdd = NewDisseminateTokenRequest(req, pline);

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
            if (req->offset + req->nsize > m_nLineSize)
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
            ST_request *reqdd = NewDisseminateTokenRequest(req, pline);

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
void CacheL2TOKIM::OnInvalidateRet(ST_request* req)
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
void CacheL2TOKIM::OnAcquireTokenRem(ST_request* req)
{
	__address_t address = req->getreqaddress();

	// locate certain set
	cache_line_t* line = LocateLineEx(address);

    // handle invalid state, or say, the line cannot be found
    if (line == NULL)   // I
    {
		// save the current request
        InsertPASNodeRequest(req);
		//m_pReqCurPASasNode = req;

		return; // -- remove else -- 
    }

    assert(line->state != CLS_INVALID);
    assert(req->getlineaddress() == line->getlineaddress(CacheIndex(req->getlineaddress()), lg2(m_nSet)));

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
void CacheL2TOKIM::OnAcquireTokenRet(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate certain set
    cache_line_t* line = LocateLineEx(address);

    // make sure the line can be found
    assert(line!=NULL);

    // handle other states
    assert(line->state!=CLS_INVALID);
    assert(req->getlineaddress() == line->getlineaddress(CacheIndex(req->getlineaddress()), lg2(m_nSet)));

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
                            reqnotify->starttime = sc_time_stamp().to_double();

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
                        reqnotify->starttime = sc_time_stamp().to_double();

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

                // check whether the line is complete   // JXXX
                assert(CheckLineValidness(line));

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
void CacheL2TOKIM::OnAcquireTokenDataRem(ST_request* req)
{
	__address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

	// locate certain set
	cache_line_t* line = LocateLineEx(address);

    assert(req->tokenrequested <= GetTotalTokenNum());

    // handle invalid state, or say, the line cannot be found
    if (line == NULL)   // I
    {
		// save the current request
        InsertPASNodeRequest(req);
		return;
    }

    assert(line->state != CLS_INVALID);
    assert(req->getlineaddress() == line->getlineaddress(CacheIndex(req->getlineaddress()), lg2(m_nSet)));

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
void CacheL2TOKIM::OnAcquireTokenDataRet(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate certain set
    cache_line_t* line = LocateLineEx(address);

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
                                reqnotify->starttime = sc_time_stamp().to_double();

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

                    // check whether the line is complete   // JXXX
                    if (req->tokenacquired == 0)
                        assert(CheckLineValidness(line));

                    UpdateCacheLine(line, req, line->state, line->tokencount + req->tokenacquired, false, false, true, false, (line->tokencount == 0)?LUM_INCRE_OVERWRITE:LUM_NO_UPDATE);

                    // save reply request
                    pop_initiator(req);

                    UpdateRequest(req, line, REQUEST_WRITE_REPLY, address, false, false, false, 0xffff, RUM_NONE);

                    req->type = REQUEST_WRITE_REPLY;

                    if (req->bmerged)
                    {
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
void CacheL2TOKIM::OnDisseminateTokenData(ST_request* req)
{
    __address_t address = req->getreqaddress();

    //DEBUG_TEST_XXX

    // locate line
    cache_line_t* line = LocateLineEx(address);

    // handle INVALID state
    if (line == NULL)
    {
        if (    m_nInjectionPolicy == IP_NONE)
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



void CacheL2TOKIM::OnDirNotification(ST_request* req)
{
    // request meant for directory, shouldn't receive it again
    assert (!IS_INITIATOR(req, this));
    // just pass it by
    InsertPASNodeRequest(req);
    //m_pReqCurPASasNode = req;
}

// virtual function for find replacement line
cache_line_t* CacheL2TOKIM::GetReplacementLineEx(__address_t address)
{
    cache_line_t *line, *linelru;     // temp variables
    cache_line_t *linelruw = NULL; // replacement line for write-back request
    cache_line_t *linelrue = NULL; // replacement line for eviction request
    unsigned int index = CacheIndex(address);
    // uint64 tag = CacheTag(address);

    unsigned int nunlocked = 0;

    bool bcompareshared = false;

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
    }

    // all the lines are occupied by the locked state
    if (nunlocked == m_nAssociativity)
        return NULL;

    return (bcompareshared) ? linelrue : linelruw;
}
