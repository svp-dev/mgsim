#include "directoryrttokim.h"
#include "../simlink/linkmgs.h"
using namespace MemSim;

//////////////////////////////////////////////////////////////////////////
// protocol handling: protocol with intermediate states.

//////////////////////////////////////////////////////////////////////////
// network request handler


void DirectoryRTTOKIM::OnNETAcquireTokenData(ST_request* req)
{
    // correct the counter and state before the 

    __address_t address = req->getreqaddress();
    
    // locate certain set
    dir_line_t* line = LocateLine(address);

//#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
//    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());
//#endif

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
            ADD_INITIATOR_BUS(req, this);
            get_initiator_bus(req);

            // set newline flag
            req->bnewline = true;

            // append the request to the queue
            if (!m_srqSusReqQ.AppendRequest2Line(req, line))
	      {
		cerr << "cannot append request to line (3, maybe queue is full)" << endl;
		abort();
	      }

            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "RS reserves a line at CACHED state <=" << endl;
            LOG_VERBOSE_END
        }
        else
        {
            m_srqSusReqQ.StartLoading(line);
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            // JOYING revisit, this is to resolve the reload bug
            if ((LinkMGS::s_oLinkConfig.m_nDirectory > 0) && (req->bprocessed))
            {
                req->bprocessed = false;
            }
#endif
            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                clog << LOG_HEAD_OUTPUT << "RS reserves a line at CACHED state =>" << endl;
            LOG_VERBOSE_END

            // redirect the request to the main memory
            ADD_INITIATOR_BUS(req, this);
            get_initiator_bus(req);

            // save the request
            m_pReqCurNET2Bus = req;

            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "RS " << FMT_ADDR(address) << " is being sent to memory " << endl;
            LOG_VERBOSE_END

        }

        return;

    }
    else
    {
        // line can be found in the group, just pass the request

// ???         //assert( ((!req->bqueued)&&(line->queuehead != EOQ)) == false );

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
        }

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
        // REVISIT, will this cause too much `additional traffic?
        if ( (LinkMGS::s_oLinkConfig.m_nDirectory > 0) && ( ((req->gettokenpermanent() == GetTotalTokenNum())&&(!req->dataavailable))
            ||((req->bprocessed) && ( !req->dataavailable )) ) )
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

                // append the request to the queue
                // if (!m_srqSusReqQ.AppendRequest2Line(req, line))
                //     abort();

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "RS/RE reserves a line at CACHED state <=" << endl;
                LOG_VERBOSE_END
            }
            else
            {
                m_srqSusReqQ.StartLoading(line);

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "RS/RE reload a line at CACHED state =>" << endl;
                    clog <<  req->gettokenpermanent() << " " << req->dataavailable << " " << req->bprocessed << endl;
                LOG_VERBOSE_END

                // redirect the request to the main memory
                ADD_INITIATOR_BUS(req, this);
                get_initiator_bus(req);

                // save the request
                m_pReqCurNET2Bus = req;

                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "RS " << FMT_ADDR(address) << " is being sent to memory " << endl;
                LOG_VERBOSE_END

                return;
            }
        }
        else
#endif
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "RT directory : AD request passes directory to the next node. " << endl;
            LOG_VERBOSE_END
        }

    }


    if (m_srqSusReqQ.HasOutstandingRequest(line))
    {
        // append request
        if (!m_srqSusReqQ.AppendRequest2Line(req, line))
        {
	  cerr << "cannot append request to line (4, maybe queue is full)" << endl;
	  abort();
        }
        else
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "Append request " << FMT_ADDR(address) << " to the line" << endl;
            LOG_VERBOSE_END
 
        }
    }
    else
    {
        // save the request
        m_lstReqNET2Net.push_back(req);
        // m_pReqCurNET2Net = req;
    }
}

void DirectoryRTTOKIM::OnNETAcquireToken(ST_request* req)
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

        LOG_VERBOSE_BEGIN(VERBOSE_STATE)
            clog << LOG_HEAD_OUTPUT << "RT directory item change from INVALID to DIRTY state" << endl;
        LOG_VERBOSE_END

        // counter should be set to 1
        line->counter = 1;
    }
    else if (line->state == DLS_CACHED)
    {
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

        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "RT directory : AT request pass through directory, and acquired " << line->tokencount << " tokens." << endl;
        LOG_VERBOSE_END

        line->tokencount = 0;
    }
    else // never reached
    {
        cerr << ERR_HEAD_OUTPUT << "wrong state" << endl;
      abort();
        //// a local write
        //line->state = DRDIRTY;
        //line->time = sc_time_stamp();
        //line->tag = DirTag(address);

    }

    //// the auxstate should be originally deferred
    //// and now it should be deferred or none
    //if (m_pPrefetchDeferredReq == req)
    //{
    //    dir_line_t* pline;
    //    PopDeferredRequest(pline);

    //    // no need to change auxstate
    //}

    if (m_srqSusReqQ.HasOutstandingRequest(line))
    {
        // append request
        if (!m_srqSusReqQ.AppendRequest2Line(req, line))
        {
	  cerr << "cannot append request to line (5, maybe queue is full)" << endl;
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


void DirectoryRTTOKIM::OnNETDisseminateTokenData(ST_request* req)
{
    // EV request will always terminate at directory
    __address_t address = req->getreqaddress();

    assert(req->tokenacquired > 0);

    // locate certain set
    dir_line_t* line = LocateLine(address);

//  double check JOYING
//    assert( ((line != NULL)&&(!req->bqueued)&&(!m_srqSusReqQ.IsRequestQueueEmpty(line))) == false );

    assert (line != NULL);    // DRINVALID

    if (line->state == DLS_CACHED)
    {
        if (req->tokenrequested == 0)    // EV
        {
//#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
//            if ((LinkMGS::s_oLinkConfig.m_nDirectory > 0)&&(req->bpriority)&&(line->tokencount + req->gettokenpermanent() < GetTotalTokenNum()))
//            {
//                // do not stack it here on the directoy, instead forward it to the next node
//                // do not change anything, just forward
//                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
//                    clog << LOG_HEAD_OUTPUT << "DD/ev[P] detected at directory, forwarded to the next node" << endl;
//                LOG_VERBOSE_END
//
//                if (m_srqSusReqQ.HasOutstandingRequest(line))
//                {     
//                    // append request
//                    if (!m_srqSusReqQ.AppendRequest2Line(req, line))
//                    {
//                        abort();
//                    }
//                }
//                else
//                {
//                    // save the request
//                    m_pReqCurNET2Net = req;
//                }
//
//            }
//            else
//#endif
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            if ((LinkMGS::s_oLinkConfig.m_nDirectory > 0)&&(m_srqSusReqQ.HasOutstandingRequest(line)))
            {     
                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "DD/e detected at directory with outstanding requests, forwarded to the next node" << endl;
                LOG_VERBOSE_END

                // append request
                if (!m_srqSusReqQ.AppendRequest2Line(req, line))
                {
		  cerr << "cannot append request to line (6, maybe queue is full)" << endl;
		  abort();
                }
            }
            else
#endif
            {
                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "DD/ev detected at directory, DD is terminated, and " << req->tokenacquired << " tokens are transfered to the directory" << endl;
                LOG_VERBOSE_END

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
        else if (req->tokenrequested == GetTotalTokenNum())  // WB
        {
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            if ((LinkMGS::s_oLinkConfig.m_nDirectory > 0)&&(m_srqSusReqQ.HasOutstandingRequest(line)))
            {     
                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "DD/wb detected at directory with outstanding requests, forwarded to the next node" << endl;
                LOG_VERBOSE_END

                // append request
                if (!m_srqSusReqQ.AppendRequest2Line(req, line))
                {
		  cerr << "cannot append request to line (7, maybe queue is full)" << endl;
		  abort();
                }
            }
            else
#endif
            {
                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "DD/wb detected at directory, and " << req->tokenacquired << " tokens are transfered to the directory" << endl;
                LOG_VERBOSE_END

                line->tokencount += req->tokenacquired;

                line->priority = line->priority || req->bpriority;

                //cout << "tokens line " << line->tokencount << " addr " << hex << address << endl;
                //assert(line->tokencount == GetTotalTokenNum());

                // fix the line 
                FixDirLine(line);


                if (m_srqSusReqQ.HasOutstandingRequest(line))
                {
                    // append request
                    if (!m_srqSusReqQ.AppendRequest2Line(req, line))
                    {
		      cerr << "cannot append request to line (8, maybe queue is full)" << endl;
		      abort();
                    }
                }
                else
                {
                    // redirect the request to the main memory
                    ADD_INITIATOR_BUS(req, this);
                    get_initiator_bus(req);

                    // save the request
                    m_pReqCurNET2Bus = req;

                    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                        clog << LOG_HEAD_OUTPUT << "DD/wb " << FMT_ADDR(address) << " is being sent to memory " << endl;
                    LOG_VERBOSE_END
                }

                return;
            }
        }
        else
        {
	  abort();
        }
    }
    else // not reachable
    {
      abort();
    }

    //// the auxstate should be originally deferred
    //// and now it should be deferred or none
    //if (m_pPrefetchDeferredReq == req)
    //{
    //    dir_line_t* pline;
    //    PopDeferredRequest(pline);

    //    // no need to change auxstate
    //}

    // pipeline cycle done
    //FinishCycleNET();
}


//////////////////////////////////////////////////////////////////////////
// bus request handler
//////////////////////////////////////////////////////////////////////////

void DirectoryRTTOKIM::OnBUSSharedReadReply(ST_request* req)
{
}

void DirectoryRTTOKIM::OnBUSExclusiveReadReply(ST_request* req)
{
}

// AA indicator handler to adjust the counter value and directory state
void DirectoryRTTOKIM::AAIndicatorHandler(ST_request* req, dir_line_t* line)
{

//    // if both indicators are set, disable the A-A indicator
//    if (req->IsIndUASet() && req->IsIndAASet())
//    {
//        req->SetIndAA(false);
//        return;
//    }
//
//    if (!req->IsIndAASet())
//        return;
//
//    // when only the A-A indicator is set
//    // check the counter
//    assert(line->counter != 0);
//    //cout << "/////////////////////////////////////////////" << endl;
//    //print_request(req);
//    line->counter--;
//
//    assert (line->counter >= 0);

//    {
//        if (line->counter == 0)
//        {
//            line->state = DRINVALID;
//
//            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
//                clog << LOG_HEAD_OUTPUT << "line counter reaches zero, state changes to INVALID." << endl;
//            LOG_VERBOSE_END
//        }
//    }

}

void DirectoryRTTOKIM::ReviewState(REVIEW_LEVEL rev)
{
    // switch to hexdecimal

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)

    clog << hex;
    clog << "|||========================||| [* " << name() << " *]" << " <[B:" << m_pfifoFeedback->num_available_fast() << "]>"<< " <[N:" << m_fifoinNetwork.num_available_fast() << "]>" << " |||========================|||" << endl;

    // print pipeline
    clog << "-- Pipeline Net --" << endl;
    m_pPipelineNET->print();
    clog << "-- Pipeline Bus --" << endl;
    m_pPipelineBUS->print();
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
                    clog << "<Set>0x" << i << "-<no>0x" << j << " [" << addr << ", " << addr + s_nLineSize << "] : " << line.StateName() << " #" << line.counter << " " << m_srqSusReqQ.AuxStateName(line.aux, true);
                    
                    // check whether there are requests queue on it
                    if (!m_srqSusReqQ.IsRequestQueueEmpty(&line))
                    {
                        clog << "; [Q].head ";
                        ST_request* reqque;
                        if (m_srqSusReqQ.GetNextReq(line, reqque, true))
                        {
                            clog << reqque->RequestInfo2Text(ptext) << endl;
                        }
                    }
                    else
                        clog << endl;
                }
            }

            // check the activated line queue
            clog << "[L-Q]: ";

            dir_line_t* line;

            m_srqSusReqQ.GetNextActiveLine(line, true);

            do {
                if (line == NULL)
                {
                    clog << "NULL" << endl << endl;
                    break;
                }

                clog << "<S>" << line->setid << " <No.>" << LineNumber(&m_pSet[line->setid], line) << " ==> ";
                
            } while (m_srqSusReqQ.GetNextActiveLine(line));

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
                    sprintf(str, "%s%s", str, line.StateName(true, true));

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
                    clog << "  " << "<N>0x" << j << " [" << addr << ", " << addr + s_nLineSize << "] : " << line.StateName() << " #" << line.counter << " " << m_srqSusReqQ.AuxStateName(line.aux, true) << endl;

                    // check whether there are requests queue on it
                    //if (line.queuehead != EOQ)
                    if (!m_srqSusReqQ.IsRequestQueueEmpty(&line))
                    {
                        clog << "[RQ]: ";

                        ST_request* reqque;
                        m_srqSusReqQ.GetNextReq(line, reqque, true);

                        do {
                            if (reqque == NULL)
                            {
                                clog << "NULL" << endl;
                                break;
                            }

                            clog << reqque->RequestInfo2Text(ptext, false) << " ->" << endl << "     ";
                        } while(m_srqSusReqQ.GetNextReq(line, reqque));
                    }
                }
            }

            // check the activated line queue
            clog << "[L-Q]: ";

            dir_line_t* line;

            m_srqSusReqQ.GetNextActiveLine(line, true);

            do {
                if (line == NULL)
                {
                    clog << "NULL" << endl << endl;
                    break;
                }

                clog << "<S>" << line->setid << " <No.>" << LineNumber(&m_pSet[line->setid], line) << " ==> ";
                
            } while (m_srqSusReqQ.GetNextActiveLine(line));

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
                    sprintf(str, "%s%s", str, line.StateName(true, true));

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
                    clog << "  " << "<N>0x" << j << " [" << addr << ", " << addr + s_nLineSize << "] : " << line.StateName() << " #" << line.counter << " " << m_srqSusReqQ.AuxStateName(line.aux, true) << endl;

                    // check whether there are requests queue on it
                    //if (line.queuehead != EOQ)
                    if (!m_srqSusReqQ.IsRequestQueueEmpty(&line))
                    {
                        clog << "[RQ]: ";

                        ST_request* reqque;
                        m_srqSusReqQ.GetNextReq(line, reqque, true);

                        do {
                            if (reqque == NULL)
                            {
                                clog << "NULL" << endl;
                                break;
                            }

                            clog << reqque->RequestInfo2Text(ptext, false) << " ->" << endl << "     ";
                        } while(m_srqSusReqQ.GetNextReq(line, reqque));
                    }
                }
            }

            // check the activated line queue
            clog << "[L-Q]: ";

            dir_line_t* line;

            m_srqSusReqQ.GetNextActiveLine(line, true);

            do {
                if (line == NULL)
                {
                    clog << "NULL" << endl << endl;
                    break;
                }

                clog << "<S>" << line->setid << " <No.>" << LineNumber(&m_pSet[line->setid], line) << " ==> ";
                
            } while (m_srqSusReqQ.GetNextActiveLine(line));

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
                    clog << line.StateName(true, true);

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
                    clog << "  " << "<N>0x" << j << " [" << addr << ", " << addr + s_nLineSize << "] : " << line.StateName() << " #" << line.counter << " " << m_srqSusReqQ.AuxStateName(line.aux, true) << endl;

                    // check whether there are requests queue on it
                    //if (line.queuehead != EOQ)
                    if (!m_srqSusReqQ.IsRequestQueueEmpty(&line))
                    {
                        clog << "[RQ]: ";

                        ST_request* reqque;
                        m_srqSusReqQ.GetNextReq(line, reqque, true);

                        do {
                            if (reqque == NULL)
                            {
                                clog << "NULL" << endl;
                                break;
                            }

                            clog << reqque->RequestInfo2Text(ptext, false) << " ->" << endl << "     ";
                        } while(m_srqSusReqQ.GetNextReq(line, reqque));
                    }
                }
            }

            // check the activated line queue
            clog << "[L-Q]: ";

            dir_line_t* line;

            m_srqSusReqQ.GetNextActiveLine(line, true);

            do {
                if (line == NULL)
                {
                    clog << "NULL" << endl << endl;
                    break;
                }

                clog << "<S>" << line->setid << " <No.>" << LineNumber(&m_pSet[line->setid], line) << " ==> ";
                
            } while (m_srqSusReqQ.GetNextActiveLine(line));

        }
        break;

    default:
        cerr << ERR_HEAD_OUTPUT << "error in review state" << endl;
        break;
    }

    clog << dec;

    LOG_VERBOSE_END

}

void DirectoryRTTOKIM::MonitorAddress(ostream& ofs, __address_t addr)
{
    dir_line_t* line = LocateLine(addr);

    if (line == NULL)
    {
        ofs << "I00";
        return;
    }

    char temp[20];
    ofs << DirectoryStateName(line->state, line->tokencount, line->breserved, line->priority, temp, true);
}


