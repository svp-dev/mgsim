#include "directorytokim.h"
using namespace MemSim;

//////////////////////////////////////////////////////////////////////////
// BELOW PROTOCOL TRANSACTION HANDLER


void DirectoryTOKIM::OnBELAcquireTokenData(ST_request* req)
{
    // correct the counter and state before the 

    __address_t address = req->getreqaddress();
    
    // locate certain set
    dir_line_t* line = LocateLine(address);

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());
#endif

    if (IsRequestLocal(req, true))
    {
        if (req->tokenacquired > 0)
            assert((line != NULL)||(evictedhit));

        if (line == NULL)
        {
//            assert(req->tokenacquired == 0);

            // need to fetch a outside the subring

            // allocate a space
            line = GetReplacementLine(req->getreqaddress());

            // must return an invalid line
            assert(line != NULL);
            assert(line->tokencount == 0);
            assert(line->ntokenline  == 0);
            assert(line->ntokenrem == 0);

            // update line info
            UpdateDirLine(line, req, DLS_CACHED, 0, 0, 0, false, false, true);

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            if (evictedhit)
                m_evictedlinebuffer.DumpEvictedLine2Line(req->getlineaddress(), line);
#endif

            // prepare the request to send to upper level
            ADD_INITIATOR_NODE(req, this);

            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "reserves a line at CACHED state" << endl;
            LOG_VERBOSE_END

            // save the request
            m_pReqCurBEL2Above = req;

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            line->nrequestout++;
#endif

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "request " << FMT_ADDR(address) << " is being sent to upper level " << endl;
            LOG_VERBOSE_END

            return;

        }

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
        // make sure that no buffer hit
        assert(evictedhit == false);
#endif

        if (req->tokenrequested < GetTotalTokenNum())  // read: RS, SR
        {
            if (ShouldLocalReqGoGlobal(req, line))
            {
                // no token in local leve, the line must be acquiring tokens from somewhere else
                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "RS/SR hit a dir line at CACHED, and will go upper level" << endl;
                LOG_VERBOSE_END

                 // transfer tokens to request, if any.
                Update_RequestRipsLineTokens(true, true, false, req, line, 0, 1);

                // send the request to upper level
                m_pReqCurBEL2Above = req;
            }
            else
            {
                // if there are lines in the same level
                // then let the request stay in the same level

                // UpdateDirLine();
                line->time = sc_time_stamp();

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "RS/SR hit a dir line at CACHED, and will stay in the same level" << endl;
                LOG_VERBOSE_END
  
                // if directory has token in hand, then hand it over to the reqeust.
                if (line->tokencount > 0)
                {
                    if (req->btransient)
                    {
                        // RS/SR cannot be transient request
                        assert(false);
                    }
                    else
                    {
                        Update_RequestRipsLineTokens(true, true, true, req, line);
                    }
                }

                // save the reqeust 
                m_pReqCurBEL2Below = req;
            }
        }
        else    // RE, ER
        {
            if (ShouldLocalReqGoGlobal(req, line))
            {
                // need to go out the local level

                // Update request and line
                Update_RequestRipsLineTokens(true, true, false, req, line, 0, 1);

                m_pReqCurBEL2Above = req;

            }
            else // all tokens are in local level
            {
                //no necessary to go outside

                // make sure at least some cache has the data
                assert((line->ntokenline+line->ntokenrem)> 0);

                // Update request and line
                Update_RequestRipsLineTokens(true, true, true, req, line);

                m_pReqCurBEL2Below = req;
            }
        }
        return;
    }
    else
    {
        if (line == NULL)
        {
            // probably there should be remote reuqest inside local level in this case

            // prepare the request to send to upper level

            // just go out
            m_pReqCurBEL2Above = req;

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            if (evictedhit)
                m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), false, req->gettokenpermanent(), req->bpriority);
            else
                assert(false);
#endif

            // pop initiator
            pop_initiator_node(req);

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "request " << FMT_ADDR(address) << " is being sent to upper level " << endl;
            LOG_VERBOSE_END

            return;
        }


#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
        assert(evictedhit == false);
#endif

        // get token from the directory if any        
        Update_RequestRipsLineTokens(false, true, false, req, line, -1);

        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "request " << FMT_ADDR(address) << " is being sent to upper level " << endl;
            clog << "zzzzzzzzzzzzzzzzzzzzzzzzz " << line->tokencount << "|" << line->ntokenline << "|" << line->ntokenrem << endl;
        LOG_VERBOSE_END

        // remote request is going out anyway
        m_pReqCurBEL2Above = req;
    }

}


void DirectoryTOKIM::OnBELAcquireToken(ST_request* req)
{
    assert(req->tokenrequested == GetTotalTokenNum());

    __address_t address = req->getreqaddress();
    
    // locate certain set
    dir_line_t* line = LocateLine(address);

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());
#endif


    if (IsRequestLocal(req, true))
    {
        if (req->tokenacquired > 0)
            assert(line != NULL);

        if (line == NULL)
        {
            assert(req->tokenacquired == 0);

            // need to fetch a outside the subring

            // allocate a space
            line = GetReplacementLine(req->getreqaddress());

            // must return an invalid line
            assert(line != NULL);
            assert(line->tokencount == 0);
            assert(line->ntokenline == 0);
            assert(line->ntokenrem == 0);

            // update line info
            UpdateDirLine(line, req, DLS_CACHED, 0, 0, 0, false, false, true);

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            if (evictedhit)
                m_evictedlinebuffer.DumpEvictedLine2Line(req->getlineaddress(), line);
#endif

            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "reserves a line at CACHED state" << endl;
            LOG_VERBOSE_END

            // prepare the request to send to upper level
            ADD_INITIATOR_NODE(req, this);

            // save the request
            m_pReqCurBEL2Above = req;

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            line->nrequestout++;
#endif

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "request " << FMT_ADDR(address) << " is being sent to upper level " << endl;
            LOG_VERBOSE_END

            return;

        }

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
        assert(evictedhit == false);
#endif

        // request is IV
        if (ShouldLocalReqGoGlobal(req, line))
        {
            // need to go out the local level

            // Update request and line
            Update_RequestRipsLineTokens(true, true, false, req, line, 0, 1);

            m_pReqCurBEL2Above = req;

        }
        else // all tokens are in local level
        {
            //no necessary to go outside

            // make sure at least some cache has the data
            assert((line->ntokenline+line->ntokenrem) > 0);

            // Update request and line
            Update_RequestRipsLineTokens(true, true, true, req, line);

            m_pReqCurBEL2Below = req;
        }

        return;
    }
    else
    {
        if (line == NULL)
        {
            // prepare the request to send to upper level

            // just go out
            m_pReqCurBEL2Above = req;

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            if (evictedhit)
                m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), false, req->gettokenpermanent(), req->bpriority);
            else
                assert(false);
#endif

            // pop initiator
            pop_initiator_node(req);

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "request " << FMT_ADDR(address) << " is being sent to upper level " << endl;
            LOG_VERBOSE_END

            return;
        }

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
        assert(evictedhit == false);
#endif

        // get token from the directory if any        
        Update_RequestRipsLineTokens(false, true, false, req, line, -1, 0);

        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "request " << FMT_ADDR(address) << " is being sent to upper level " << endl;
        LOG_VERBOSE_END
 
        // remote request is going out anyway
        m_pReqCurBEL2Above = req;
    }

}


void DirectoryTOKIM::OnBELDisseminateTokenData(ST_request* req)
{
    // EV request will always terminate at directory
    __address_t address = req->getreqaddress();

    assert(req->tokenacquired > 0);

    // locate certain set
    dir_line_t* line = LocateLine(address);

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());
#endif


    if (IsRequestLocal(req, true))
    {
        if (line == NULL)
        {
            // send the request to upper level
            m_pReqCurBEL2Above = req;

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            if (evictedhit)
            {
                m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), false, req->gettokenpermanent(), req->bpriority, true);
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "DD missed hit on evicted buffer," << FMT_ADDR(address) << " is being sent to upper level " << endl;
                LOG_VERBOSE_END
            }
            else
                assert(false);
#endif

            return;
        }

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
        assert(evictedhit == false);
#endif

        // issue: the eviction request might go around the local network, in turn change the request sequence.
        // this might generate leave the root directory without sufficient space
        // solution: stack the request tokens on the directory line 
        // if there are request in or out, then go global, since stacking the token might lost data
        // if nothing in or out, and there are tokens locally, then stack them on the line
        // if all the tokens are stacked on the line, then request will be delievered to outside 
        // otherwise deleted
        // NOT IMPLEMENTED YET
        //
        // // Possible issue: sacrifice locality. for example, if locally another line is request, while all the tokens are evicted to outside. without evicted to the same level and informing the request line, locallity might be suffered. JXXX, REVISIT
        //
        // possibly, deal with rquest out differently as well, REVISIT JXXX
        if ((line->nrequestin != 0) || (line->nrequestout != 0))
        {
            // should always go global
            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "DD hit a dir line at CACHED with Rin/out, and goes to the global level " << endl;
            LOG_VERBOSE_END

            Update_RequestRipsLineTokens(true, true, false, req, line);

            // save the request
            m_pReqCurBEL2Above = req;
        }
        else
        {
            if ((req->tokenacquired < line->ntokenline))
            {
                // just stack, no ripping
                line->ntokenline -= req->tokenacquired;
                line->tokencount += req->tokenacquired;

                line->priority |= req->bpriority;

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "DD hit a dir line at CACHED, token stacks on the directory. and request is terminated."  << endl;
                LOG_VERBOSE_END

                delete req;
            }
            else
            {
                // send out
                // should always go global
                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "DD hit a dir line at CACHED without Rin/out, and goes to the global level" << endl;
                LOG_VERBOSE_END

                Update_RequestRipsLineTokens(true, true, false, req, line);

                // save the request
                m_pReqCurBEL2Above = req;
            }
        }
    }
    else    // global request
    {
        if (line == NULL)
        {
            // just dispatch to the upper level
            m_pReqCurBEL2Above = req;

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            if (evictedhit)
                m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), false, req->gettokenpermanent(), req->bpriority, true);
            else
                assert(false);
#endif

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "DD missed locally, hit on evicted buffer, origined from other level " << FMT_ADDR(address) << " is being sent to upper level " << endl;
            LOG_VERBOSE_END

            return;
        }

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
        assert(evictedhit == false);
#endif

        // update the directory
        Update_RequestRipsLineTokens(false, true, false, req, line);

        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "DD hit, origined from other level " << FMT_ADDR(address) << " is being sent to upper level " << endl;
        LOG_VERBOSE_END

        // remote request is going out anyway
        m_pReqCurBEL2Above = req;
    }
}


#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
void DirectoryTOKIM::OnBELDirNotification(ST_request* req)
{
    // address
    __address_t address  = req->getreqaddress();

    // locate certain line
    dir_line_t* line = LocateLine(address);

    // evicted line buffer
    //bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());

    // the line must be existing
    assert(line != NULL);

    line->ntokenline += req->tokenacquired;

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "Notification request received " << FMT_ADDR(address) << ", will be terminated directly  " << endl;
        print_request(req);
     LOG_VERBOSE_END


    // terminate the request
    delete req;
}
#endif






//////////////////////////////////////////////////////////////////////////
// ABOVE PROTOCOL TRANSACTION HANDLER

void DirectoryTOKIM::OnABOAcquireTokenData(ST_request* req)
{
    // correct the counter and state before the 

    __address_t address = req->getreqaddress();
    
    // locate certain set
    dir_line_t* line = LocateLine(address);

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());
#endif


    if (IsRequestLocal(req, false))
    {
//        if (req->tokenacquired > 0)
        {
            //clog << "assert alert " << hex << req->getlineaddress() << endl;
            assert(line != NULL);
        }

        if (line == NULL)
        {
            assert(false);
            return;
        }

        if (req->tokenrequested < GetTotalTokenNum())  // read: RS, SR
        {
            // always go local
            m_pReqCurABO2Below = req;

            // pop the initiator/dir in the update function
            // Update the dir
            Update_RequestRipsLineTokens(true, false, true, req, line, 0, -1);

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "AD-RS/SR origined from this local level " << FMT_ADDR(address) << " is being returned to below level " << endl;
                print_request(req);
            LOG_VERBOSE_END

        }
        else
        {
            // always go local
            m_pReqCurABO2Below = req;

            // pop the initiator/dir in the update function
            // Update the dir
            Update_RequestRipsLineTokens(true, false, true, req, line, 0, -1);
        
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "AD-RE/ER origined from this local level " << FMT_ADDR(address) << " is being returned to upper level " << endl;
                print_request(req);
            LOG_VERBOSE_END

        }
    }
    else    // remote request
    {
        // as long as the line exist, the requet, no matter RS or RE, has to get in
        if ((line == NULL)&&(!evictedhit))
        {
            // just go to above level
            m_pReqCurABO2Above = req;

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "nothing can be found locally, skip local ring @" << FMT_ADDR(address) << endl;
            LOG_VERBOSE_END
        }
        else    // somehting inside lower level, just always get in
        {
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            if (evictedhit)
            {
                // get in lower level, but update the evicted buffer
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "remote request hit on evicted buffer " << FMT_ADDR(address) << " is entering the lower level " << endl;
                LOG_VERBOSE_END

                m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), true, req->gettokenpermanent(), req->bpriority);

                ADD_INITIATOR_NODE(req, this);
            }
            else
            {
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "remote request " << FMT_ADDR(address) << " is entering the lower level " << endl;
                LOG_VERBOSE_END

                Update_RequestRipsLineTokens(false, false, true, req, line, 1);
            }
#endif

            // get in lower level
            m_pReqCurABO2Below = req;

        }
    }
}


void DirectoryTOKIM::OnABOAcquireToken(ST_request* req)
{
    // correct the counter and state before the 

    __address_t address = req->getreqaddress();
    
    // locate certain set
    dir_line_t* line = LocateLine(address);

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());
#endif


    if (IsRequestLocal(req, false))
    {
        if (req->tokenacquired > 0)
            assert(line != NULL);

        if (line == NULL)
        {
            assert(false);
            return;
        }

        // always go local
        m_pReqCurABO2Below = req;

        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "IV origined from this local level " << FMT_ADDR(address) << " is being returned to upper level " << endl;
        LOG_VERBOSE_END

        // pop the initiator/dir in the update function
        // Update the dir
        Update_RequestRipsLineTokens(true, false, true, req, line, 0, -1);
    }
    else    // remote request
    {
        // as long as the line exist, the requet, no matter RS or RE, has to get in
        if (line == NULL)
        {
            if (evictedhit)
            {
                // get in lower level, but update the evicted buffer
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "remote request hit on evicted buffer " << FMT_ADDR(address) << " is entering the lower level " << endl;
                LOG_VERBOSE_END

                m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), true, req->gettokenpermanent(), req->bpriority);

                ADD_INITIATOR_NODE(req, this);

                // get in local level
                m_pReqCurABO2Below = req;

            }
            else
            {
                 // just go to above level
                m_pReqCurABO2Above = req;

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "nothing can be found locally, IV skip local ring @" << FMT_ADDR(address) << endl;
                LOG_VERBOSE_END
            }
        }
        else    // somehting inside lower level, just always get in
        {
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            if (evictedhit)
            {
                // get in lower level, but update the evicted buffer
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "remote request hit on evicted buffer " << FMT_ADDR(address) << " is entering the lower level " << endl;
                LOG_VERBOSE_END

                m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), true, req->gettokenpermanent(), req->bpriority);

                ADD_INITIATOR_NODE(req, this);
            }
            else
            {
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "remote request " << FMT_ADDR(address) << " is entering the lower level " << endl;
                LOG_VERBOSE_END

                Update_RequestRipsLineTokens(false, false, true, req, line, 1);
            }
#endif

            // get in lower level
            m_pReqCurABO2Below = req;

        }
    }
}


void DirectoryTOKIM::OnABODisseminateTokenData(ST_request* req)
{
    // EV request will always terminate at directory
    __address_t address = req->getreqaddress();

    assert(req->tokenacquired > 0);

    // locate certain set
    dir_line_t* line = LocateLine(address);

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    // evicted line buffer
    unsigned int requestin = 0;
    unsigned int tokenrem;
    bool grouppriority;
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress(), requestin, tokenrem, grouppriority);
#endif


    // does not matter whether the request is from local level or not


    // issue: disemminated token if send to lower level, 
    // the replaced request from the evicted line can bypass the evicted token, 
    // which lead to insufficient lines in root directory
    // solution & analysis:
    // if tokens available in the lowerlevel, and they are not evicted to upper level yet, it's safe
    // if tokens not avaialbe, then skip the group and move to next group
    // if tokens are avaialbe, there are request in or out, then go in, it's safe
    // if tokens are avaible in evicted buffer, if there are request in, then get in, otherwise, skip to next group
    // if tokens are available in the line, there are no request in or out, then the line can be there or evicted. and lines should be or ever be in normal state. thus, leave the tokens and priority flag and other stuff directly in the directory is fine. in this case, the request should be terminated here.


    if ((line == NULL)&&(!evictedhit))
    {
        if ((m_nInjectionPolicy == IP_NONE)||(m_nInjectionPolicy == IP_EMPTY_1EJ))
 
        {
            // skip the local level and pass it on
            m_pReqCurABO2Above = req;

            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "DD miss at the directory, and skip local level " << endl;
            LOG_VERBOSE_END
        }
//        else if (m_nInjectionPolicy == IP_EMPTY_1EJ)
//        {
//            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
//                clog << LOG_HEAD_OUTPUT << "DD address " << FMT_ADDR(req->getreqaddress()) << " (ij1ej) policy, is going to be sent in lower level" << endl;
//            LOG_VERBOSE_END
//
//            // will only update eviction buffer
//            m_evictedlinebuffer.AddEvictedLine(req->getlineaddress(), 0, req->gettokenpermanent(), req->bpriority);
//
//            ADD_INITIATOR_NODE(req, this);
//
//            m_pReqCurABO2Below = req;
////            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
////                clog << LOG_HEAD_OUTPUT << "DD address is going to be sent " << endl;
////                print_request(req);
////            LOG_VERBOSE_END
//        }
        else
        {
            assert(false);
        }

        return;
    }

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    if (evictedhit) // REVIST, JXXX, this may not be necessary
    {
        if (requestin == 0)
        {
            // skip the local group to next group
            // get in lower level, but update the evicted buffer
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "remote DD request hit on evicted buffer/without RIN " << FMT_ADDR(address) << " skip the local level " << endl;
            LOG_VERBOSE_END
            
            // to above level
            m_pReqCurABO2Above = req;
        }
        else
        {
            // get in lower level, but update the evicted buffer
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "remote DD request hit on evicted buffer " << FMT_ADDR(address) << " is entering the lower level " << endl;
            LOG_VERBOSE_END

            m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), true, req->gettokenpermanent(), req->bpriority, true);

            // lower level
            m_pReqCurABO2Below = req;
        }
    }
    else
    {
        if ((line->nrequestin != 0)||(line->nrequestout != 0))
        {
            // lower level
            m_pReqCurABO2Below = req;

            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "DD hit a dir line, and enters the local level" << endl;
            LOG_VERBOSE_END

            // assume it's from outside not local,
            Update_RequestRipsLineTokens(false, false, true, req, line);
        }
        else
        {
            assert(line->ntokenline + line->ntokenrem > 0);
            // leave the tokens on the line. without getting in or send to the next node

            // notgoing anywhere, just terminate the request
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "remote DD request hit on line, request terminated " << FMT_ADDR(address) << endl;
            LOG_VERBOSE_END

            assert(req->tokenacquired < GetTotalTokenNum());
            line->tokencount += req->tokenacquired;
            line->priority |= req->bpriority;

            delete req;
        }

    }
#endif
}




#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
void DirectoryTOKIM::ReviewState(REVIEW_LEVEL rev)
{
    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
    // switch to hexdecimal

    // print pipeline and buffer
    clog << hex;
    clog << "|||========================||| [* " << name() << " *]" << " <[B:" << GetBelowIF().m_fifoinNetwork.num_available_fast() << "]>"<< " <[A:" << GetAboveIF().m_fifoinNetwork.num_available_fast() << "]>" << " |||========================|||" << endl;
    clog << "-- Pipeline BEL --" << endl;
    m_pPipelineBEL->print();
    clog << "-- Pipeline ABO --" << endl;
    m_pPipelineABO->print();
    clog << endl;


    char ptext[0x500];
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
                    dir_line_t &line = m_pSet[i].lines[j];

                    // skip invalid lines
                    if (line.state == DLS_INVALID)
                        continue;

                    // print a cacheline
                    // print the line set and no. within a set
                    __address_t addr = line.getlineaddress(i, m_nSetBits);
                    clog << "<Set>0x" << i << "-<no>0x" << j << " [" << addr << ", " << addr + s_nLineSize << "] : " << line.StateName(false) << " #(R) " << hex << line.nrequestin << "|" << line.nrequestout << " #(T) " << line.tokencount << "|" << line.ntokenline << "|" << line.ntokenrem << endl;
                    
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
                bool hasvalid = false;
                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    dir_line_t &line = m_pSet[i].lines[j];

                    if (line.state != DLS_INVALID)
                        hasvalid |= true;

                    // skip invalid lines
                    sprintf(str, "%s%s", str, line.StateName(false, true));

                }

                if (hasvalid)
                    clog << "<Set>0x" << i << ": " << str << endl;

                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    dir_line_t &line = m_pSet[i].lines[j];

                    // skip invalid lines
                    if (line.state == DLS_INVALID)
                        continue;

                    // print a cacheline
                    // print the line set and no. within a set
                    __address_t addr = line.getlineaddress(i, m_nSetBits);
                    clog << "<Set>0x" << i << "-<no>0x" << j << " [" << addr << ", " << addr + s_nLineSize << "] : " << line.StateName(false) << " #(R) " << hex << line.nrequestin << "|" << line.nrequestout << " #(T) " << line.tokencount << "|" << line.ntokenline << "|" << line.ntokenrem << endl;
                }
            }
            m_evictedlinebuffer.CheckStatus(clog);
        }
        break;

    case SO_REVIEW_DETAIL:
        {
            // check through all the cachelines and print only the valid ones 
            for (unsigned int i=0;i<m_nSet;i++)
            {
                char str[100];
                str[0] = '\0';
                bool hasvalid = false;
                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    dir_line_t &line = m_pSet[i].lines[j];

                    if (line.state != DLS_INVALID)
                        hasvalid |= true;

                    // skip invalid lines
                    sprintf(str, "%s%s", str, line.StateName(false, true));

                }

                if (hasvalid)
                    clog << "<Set>0x" << i << ": " << str << endl;

                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    dir_line_t &line = m_pSet[i].lines[j];

                    // skip invalid lines
                    if (line.state == DLS_INVALID)
                        continue;

                    // print a cacheline
                    // print the line set and no. within a set
                    __address_t addr = line.getlineaddress(i, m_nSetBits);
                    clog << "<Set>0x" << i << "-<no>0x" << j << " [" << addr << ", " << addr + s_nLineSize << "] : " << line.StateName(false) << " #(R) " << hex << line.nrequestin << "|" << line.nrequestout << " #(T) " << line.tokencount << "|" << line.ntokenline << "|" << line.ntokenrem << endl;

                }
            }
            m_evictedlinebuffer.CheckStatus(clog);

        }
        break;

    case SO_REVIEW_ALL:
        {
            // check through all the cachelines and print only the valid ones 
            for (unsigned int i=0;i<m_nSet;i++)
            {
                clog << "<Set>0x" << i << ": ";
                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    dir_line_t &line = m_pSet[i].lines[j];

                    // skip invalid lines
                    clog << line.StateName(false, true);

                }
                clog << endl;

                for (unsigned int j=0;j<m_nAssociativity;j++)
                {
                    dir_line_t &line = m_pSet[i].lines[j];

                    // skip invalid lines
                    if (line.state == DLS_INVALID)
                        continue;

                    // print a cacheline
                    // print the line set and no. within a set
                    __address_t addr = line.getlineaddress(i, m_nSetBits);
                    clog << "<Set>0x" << i << "-<no>0x" << j << " [" << addr << ", " << addr + s_nLineSize << "] : " << line.StateName(false) << " #(R) " << hex << line.nrequestin << "|" << line.nrequestout << " #(T) " << line.tokencount << "|" << line.ntokenline << "|" << line.ntokenrem << endl;
                }
            }

            m_evictedlinebuffer.CheckStatus(clog);
        }
        break;

    default:
        cerr << ERR_HEAD_OUTPUT << "error in review state" << endl;
        break;
    }

    clog << dec;
    LOG_VERBOSE_END
}

void DirectoryTOKIM::MonitorAddress(ostream& ofs, __address_t addr)
{
    dir_line_t* line = LocateLine(addr);

    if (line == NULL)
    {
        ofs << "-.R0.0-T0.0.0";
        m_evictedlinebuffer.CheckStatus(ofs, addr);
        return;
    }

    ofs << line->StateName(false, true) << ".R" << hex << line->nrequestin << "." << line->nrequestout << "-T" << line->tokencount << "." << line->ntokenline << "." << line->ntokenrem ;
    m_evictedlinebuffer.CheckStatus(ofs, addr);
}

#endif
