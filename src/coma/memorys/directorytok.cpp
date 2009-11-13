#include "directorytok.h"
using namespace MemSim;

#if defined(MEMSIM_DIRECTORY_IMPLEMENTATION) && (MEMSIM_DIRECTORY_IMPLEMENTATION == MEMSIM_DIRECTORY_IMPLEMENTATION_NAIVE)


void DirectoryTOK::InitializeDirLines()
{
	// allocate sets
	m_pSet = (dir_set_t*)malloc(m_nSet * sizeof(dir_set_t));

    //char auxinistate = m_srqSusReqQ.GetAuxInitialState();

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
            //m_pSet[i].lines[j].aux = auxinistate;
            m_pSet[i].lines[j].queuehead = SuspendedRequestQueue::EOQ;
            m_pSet[i].lines[j].queuetail = SuspendedRequestQueue::EOQ;
            m_pSet[i].lines[j].breserved = false;
            m_pSet[i].lines[j].priority = false;

            m_pSet[i].lines[j].bdataavailable = false;

            m_pSet[i].lines[j].setid = i;

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
            m_pSet[i].lines[j].nrequestin = 0;
            m_pSet[i].lines[j].nrequestout = 0;
            m_pSet[i].lines[j].grouppriority = false;
            m_pSet[i].lines[j].ntokenline = 0;
            m_pSet[i].lines[j].ntokenrem = 0;
#endif
		}
	}

    // initialize queue
    // suspended queue, if any, is already initialized from the constructor

LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
	clog << LOG_HEAD_OUTPUT << ": " << dec << m_nSet << "-set "  << m_nAssociativity << "-way associative " << s_nLineSize << "-byte line directory initialized" << endl;
LOG_VERBOSE_END
}

bool DirectoryTOK::SendRequestBELtoBelow(ST_request* req)
{
    if (!GetBelowIF().RequestNetwork(req))
    {
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "Below protocol: Send request through B/B interface failed, buffer full" << endl;
        LOG_VERBOSE_END

        return false;
    }

    LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "Below protocol: Send request through B/B interface succeed." << endl;
    LOG_VERBOSE_END

    return true;
}

// send reply through the network above interface
bool DirectoryTOK::SendRequestBELtoAbove(ST_request* req)
{
    // send reply through the network above interface
    if (!GetAboveIF().RequestNetwork(req))
    {
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "Below protocol: Send request through B/A interface failed, buffer full" << endl;
        LOG_VERBOSE_END

        return false;
    }

    LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "Below protocol: Send request through B/A interface succeed." << endl;
    LOG_VERBOSE_END

    return true;
}

void DirectoryTOK::SendRequestFromBEL()
{
    bool allsent = true;
    if (m_pReqCurBEL2Above != NULL)
    {
        if (SendRequestBELtoAbove(m_pReqCurBEL2Above))
        {
            // succeed

            // clear
            m_pReqCurBEL2Above = NULL;
        }
        else
        {
            allsent &= false;
        }
    }

    if (m_pReqCurBEL2Below != NULL)
    {
        if (SendRequestBELtoBelow(m_pReqCurBEL2Below))
        {
            // succeed

            // clear
            m_pReqCurBEL2Below = NULL;
        }
        else
        {
            allsent &= false;
        }
    }

    if (allsent)
    {
        m_nStateBEL = STATE_BELOW_PROCESSING;
    }
    else
    {
        m_nStateBEL = STATE_BELOW_RETRY;
    }

}



void DirectoryTOK::ProcessRequestBEL()
{
    // handle request
    ST_request* req = m_pReqCurBEL;

    assert(req->bqueued == false);

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "below interface: process request " << FMT_ADDR(req->getreqaddress()) << endl;
        clog << "\t"; print_request(req);
    LOG_VERBOSE_END

    switch(req->type)
    {
    case MemoryState::REQUEST_ACQUIRE_TOKEN:
        OnBELAcquireToken(req);
        break;
    case MemoryState::REQUEST_ACQUIRE_TOKEN_DATA:
        OnBELAcquireTokenData(req);
        break;
    case MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA:
        OnBELDisseminateTokenData(req);
        break;
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    case Request_LOCALDIR_NOTIFICATION:
        OnBELDirNotification(req);
        break;
#endif

    case MemoryState::REQUEST_READ:
    case MemoryState::REQUEST_WRITE:
    case MemoryState::REQUEST_READ_REPLY:
    case MemoryState::REQUEST_WRITE_REPLY:
    case MemoryState::REQUEST_INVALIDATE_BR:
        // error
        cerr << ERR_HEAD_OUTPUT << "===================================== ERROR =====================================" << endl;
        abort();
        break;

    default:
      abort();
        break;
    }
}

void DirectoryTOK::BehaviorBelowNet()
{
    ST_request* req_incoming = NULL;

    // initialize
    m_pReqCurBEL = NULL;

    switch (m_nStateBEL)
    {
    // free to processor any request from below interface
    case STATE_BELOW_PROCESSING:
        m_pReqCurBEL2Above = NULL;
        m_pReqCurBEL2Below = NULL;

        // fetch the request from the correct interface
        req_incoming = FetchRequestNet(true); 

        if (req_incoming != NULL)
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "request to get in bel pipeline : " << FMT_ADDR(req_incoming->getlineaddress()) << endl;
                clog << "\t"; print_request(req_incoming);
            LOG_VERBOSE_END

        }

        m_pReqCurBEL = m_pPipelineBEL->shift(req_incoming);

        if (m_pReqCurBEL != NULL)
            ProcessRequestBEL();

        if ((m_pReqCurBEL2Above != NULL)||(m_pReqCurBEL2Below != NULL))
            SendRequestFromBEL();

        break;

    case STATE_BELOW_RETRY:
        if (m_pPipelineBEL->top() == NULL)
        {
            // fetch request from incoming buffer
            req_incoming = FetchRequestNet(true);

            if (req_incoming != NULL)
            {
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "request to get in bel pipeline : " << FMT_ADDR(req_incoming->getlineaddress()) << endl;
                    clog << "\t"; print_request(req_incoming);
                LOG_VERBOSE_END
            }

            // only shift
            m_pPipelineBEL->shift(req_incoming);

        }

        assert( (m_pReqCurBEL2Below != NULL)||(m_pReqCurBEL2Above != NULL) );
        SendRequestFromBEL();
        break;

    default:
      abort();
        break;
    }
}


// fetch request from below or above interface dictated by below
ST_request* DirectoryTOK::FetchRequestNet(bool below)
{
    ST_request* reqinc = NULL;

    Network_if* nif = below?((Network_if*)&GetBelowIF()):((Network_if*)&GetAboveIF());
    
    if (nif->m_fifoinNetwork.num_available_fast() > 0)
    {
        
        if (!nif->m_fifoinNetwork.nb_read(reqinc))
        {
            cerr << ERR_HEAD_OUTPUT << "fetch incoming request from " << (below?"below":"above") << " interface failed." << endl;
            abort();
            return NULL;
        }

        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "fetch incoming request " << FMT_ADDR(reqinc->getreqaddress()) << endl;
            clog << "\t"; print_request(reqinc);
        LOG_VERBOSE_END
    }

    return reqinc;
}


void DirectoryTOK::ProcessRequestABO()
{
    // handle request
    ST_request* req = m_pReqCurABO;

    assert(req->bqueued == false);

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "above interface: process request " << FMT_ADDR(req->getreqaddress()) << endl;
        clog << "\t"; print_request(req);
    LOG_VERBOSE_END

    switch(req->type)
    {
    case MemoryState::REQUEST_ACQUIRE_TOKEN:
        OnABOAcquireToken(req);
        break;
    case MemoryState::REQUEST_ACQUIRE_TOKEN_DATA:
        OnABOAcquireTokenData(req);
        break;
    case MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA:
        OnABODisseminateTokenData(req);
        break;

    case MemoryState::REQUEST_READ:
    case MemoryState::REQUEST_WRITE:
    case MemoryState::REQUEST_READ_REPLY:
    case MemoryState::REQUEST_WRITE_REPLY:
    case MemoryState::REQUEST_INVALIDATE_BR:
        // error
        cerr << ERR_HEAD_OUTPUT << "===================================== ERROR =====================================" << endl;
        abort();
        break;

    default:
      abort();
        break;
    }
}

void DirectoryTOK::BehaviorAboveNet()
{
    ST_request* req_incoming = NULL;

    m_pReqCurABO = NULL;

    switch (m_nStateABO)
    {
    // if the cache is available to process the request from above interface
    case STATE_ABOVE_PROCESSING:
        m_pReqCurABO2Above = NULL;
        m_pReqCurABO2Below = NULL;

        // fetch the request from the correct interface
        req_incoming = FetchRequestNet(false); 

        if (req_incoming != NULL)
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "request to get in abo pipeline : " << FMT_ADDR(req_incoming->getlineaddress()) << endl;
                clog << "\t"; print_request(req_incoming);
            LOG_VERBOSE_END

        }

        m_pReqCurABO = m_pPipelineABO->shift(req_incoming);

        if (m_pReqCurABO != NULL)
            ProcessRequestABO();

        if ((m_pReqCurABO2Above != NULL)||(m_pReqCurABO2Below != NULL))
            SendRequestFromABO();

        break;

    case STATE_ABOVE_RETRY:
        if (m_pPipelineABO->top() == NULL)
        {
            // fetch request from incoming buffer
            req_incoming = FetchRequestNet(false);

            if (req_incoming != NULL)
            {
                LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                    clog << LOG_HEAD_OUTPUT << "request to get in abo pipeline : " << FMT_ADDR(req_incoming->getlineaddress()) << endl;
                    clog << "\t"; print_request(req_incoming);
                LOG_VERBOSE_END
            }

            // only shift
            m_pPipelineABO->shift(req_incoming);

        }

        assert( (m_pReqCurABO2Below != NULL)||(m_pReqCurABO2Above != NULL) );
        SendRequestFromABO();
        break;

    default:
      abort();
        break;
    }
}

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
bool DirectoryTOK::ShouldLocalReqGoGlobal(ST_request* req, dir_line_t* line)
{
    assert(line != NULL);
    assert(req != NULL);
    int tokenlocalgroup = line->tokencount + line->ntokenline + line->ntokenrem;

    assert(tokenlocalgroup >= 0);

    if (req->type == MemoryState::REQUEST_ACQUIRE_TOKEN_DATA)
    {
        if (req->tokenrequested < GetTotalTokenNum())  // read: RS, SR
        {
            if (((line->ntokenline + line->ntokenrem) > 0) && (!req->bprocessed))
            {
                // no matter what, stay!
                return false;
            }
            else
            {
                return true;
            }
        
        }
        else    // RE, ER
        {
            if ((tokenlocalgroup == (int)GetTotalTokenNum()) && (line->nrequestin == 0) && (line->nrequestout == 0))
            {
                // stay in local
                return false;
            }
            else
            {
                return true;
            }
        }
    }
    else if (req->type == MemoryState::REQUEST_ACQUIRE_TOKEN)   // IV
    {
        if ((tokenlocalgroup == (int)GetTotalTokenNum()) && (line->nrequestin == 0) && (line->nrequestout == 0))
        {
            return false;
        }
        else
        {
            return true;
        }
        
    }
    else if (req->type == MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA)
    {
        // DD decision is make in tokim file
      cerr << __FILE__ << ':' << __LINE__ << ": assert(false)" << endl;
      abort();
      // assert(false);

    }
    else
    {
      cerr << __FILE__ << ':' << __LINE__ << ": assert(false)" << endl;
      abort();
      // assert(false);
    }
}
#endif

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
void DirectoryTOK::UpdateDirLine(dir_line_t* line, ST_request* req, DIR_LINE_STATE state, unsigned int tokencount, int tokenline, int tokenrem, bool priority, bool grouppriority, bool reserved)
{
    assert(line != NULL);
    assert(req != NULL);

    line->tag = DirTag(req->getreqaddress());
    line->time = sc_time_stamp();
    line->state = state;
    line->tokencount = tokencount;
    line->ntokenline = tokenline;
    line->ntokenrem = tokenrem;
    line->breserved = reserved;
    line->priority = priority;
    line->grouppriority = grouppriority;
}

void DirectoryTOK::UpdateRequest(ST_request* req, dir_line_t* line, unsigned int tokenacquired, bool priority)
{
    assert(line != NULL);
    assert(req != NULL);

    // address and type will not change for a request
    req->tokenacquired = tokenacquired;
    req->bpriority = priority;
}

void DirectoryTOK::Update_RequestRipsLineTokens(bool requestbelongstolocal, bool requestfromlocal, bool requesttolocal, ST_request* req, dir_line_t* line, int increqin, int increqout)
{
    // make increment on request in and out
    line->nrequestin += increqin;
    line->nrequestout += increqout;

    if (requestfromlocal)
    {
        if (requestbelongstolocal)
        {
            if (requesttolocal)
            {
                if (!req->btransient)
                {
                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "request acquired " <<  line->tokencount << " tokens from the local directory" << endl;
                    LOG_VERBOSE_END

                    if (req->type == REQUEST_DISSEMINATE_TOKEN_DATA)
                    {
                        int newlinetokenline = line->tokencount + line->ntokenline - req->tokenacquired;
                        int newlinetokenrem = line->ntokenrem + req->tokenacquired;
                        bool newgrouppriority = req->bpriority || line->priority || line->grouppriority;

                        UpdateRequest(req, line, req->tokenacquired + line->tokencount, req->bpriority||line->priority);
                        UpdateDirLine(line, req, line->state, 0, newlinetokenline, newlinetokenrem, false, newgrouppriority, line->breserved);

                        // even stay in local, it will be regarded as a remote request from now
                        ADD_INITIATOR_NODE(req, this);
                    }
                    else
                    {
                        int newlinetokenline = line->tokencount + line->ntokenline;
                        bool newgrouppriority = req->bpriority || line->priority || line->grouppriority;

                        UpdateRequest(req, line, req->tokenacquired + line->tokencount, req->bpriority||line->priority);
                        UpdateDirLine(line, req, line->state, 0, newlinetokenline, line->ntokenrem, false, newgrouppriority, line->breserved);
                    }
                }
                else
                {
                    // nothing to be done
                    if (line->priority)
                    {
                        assert(false);  // could this really happen?
                        line->priority = false;
                        req->btransient = false;
                        req->bpriority = true;
                    }
                    unsigned int newlinetoken = (req->btransient?line->tokencount:0);
                    unsigned int newreqtoken = (req->btransient?(line->tokencount+req->tokenacquired):req->tokenacquired);
                    int newlinetokenline = (req->btransient?line->ntokenline:(line->ntokenline+line->tokencount));

                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "request acquired " <<  line->tokencount << " tokens from the local directory" << endl;
                    LOG_VERBOSE_END

                    bool newgrouppriority = (!(req->bpriority || line->priority)) && line->grouppriority;

                    UpdateRequest(req, line, newreqtoken, req->bpriority||line->priority);
                    UpdateDirLine(line, req, line->state, newlinetoken, newlinetokenline, line->ntokenrem, false, newgrouppriority, line->breserved);

                }

            }
            else    // if (requesttolocal)
            {
                if (!req->btransient)
                {
                    int newlinetokenline = line->ntokenline - req->gettokenpermanent();
                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "request acquired " <<  line->tokencount << " tokens from the local directory" << endl;
                    LOG_VERBOSE_END

                    bool newgrouppriority = (!(req->bpriority || line->priority)) && line->grouppriority;

                    UpdateRequest(req, line, req->tokenacquired + line->tokencount, req->bpriority||line->priority);
                    UpdateDirLine(line, req, line->state, 0, newlinetokenline, line->ntokenrem, false, newgrouppriority, line->breserved);

                }
                else
                {
                    if (line->priority)
                    {
                        line->priority = false;
                        req->btransient = false;
                        req->bpriority = true;
                    }

                    int newlinetokenline = line->ntokenline - req->gettokenpermanent();
                    LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                        clog << LOG_HEAD_OUTPUT << "request acquired " <<  line->tokencount << " tokens from the local directory" << endl;
                    LOG_VERBOSE_END

                    bool newgrouppriority = (!(req->bpriority || line->priority)) && line->grouppriority;

                    UpdateRequest(req, line, req->tokenacquired + line->tokencount, req->bpriority||line->priority);
                    UpdateDirLine(line, req, line->state, 0, newlinetokenline, line->ntokenrem, line->priority, newgrouppriority, line->breserved);

                }

                // add initiator for naive solution
                ADD_INITIATOR_NODE(req, this);

            }
        }
        else    // if (requestbelongstolocal)
        {
            // always send the request to upper level
            assert(requesttolocal == false);

            if (!req->btransient)
            {
                unsigned int newreqtoken = req->tokenacquired + line->tokencount;
                //int newlinetokenrem = line->ntokenrem - req->gettokenpermanent();
                int newlinetokenline = line->ntokenline - req->gettokenpermanent();

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "request acquired " <<  line->tokencount << " tokens from the local directory and reduce " << req->gettokenpermanent() << " tokens from local group" << endl;
                LOG_VERBOSE_END

                bool newgrouppriority = (!(req->bpriority||line->priority)) && line->grouppriority;

                UpdateRequest(req, line, newreqtoken, req->bpriority||line->priority);
                //UpdateDirLine(line, req, line->state, 0, line->ntokenline, newlinetokenrem, false, newgrouppriority, line->breserved);
                UpdateDirLine(line, req, line->state, 0, newlinetokenline, line->ntokenrem, false, newgrouppriority, line->breserved);
                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "request acquired " <<  line->tokencount << " tokens from the local directory and reduce " << req->gettokenpermanent() << " tokens from local group" << endl;
                    clog << "xxxxxxxxxxxxxxxxxxxxxx " << line->tokencount << "|" << line->ntokenline << "|" << line->ntokenrem << endl;
                LOG_VERBOSE_END


            }
            else
            {
                assert(req->gettokenpermanent() == 0);
                int newlinetokenrem = line->ntokenrem - req->gettokenpermanent();
                bool newgrouppriority = (!(req->bpriority||line->priority)) && line->grouppriority;
                UpdateDirLine(line, req, line->state, line->tokencount, line->ntokenline, newlinetokenrem, line->priority, newgrouppriority, line->breserved);
            }

            // pop out the initiator
            pop_initiator_node(req);
        }

    }
    else    // if (requestfromlocal)
    {
        if (requestbelongstolocal)
        {
            // always send the request to local
            assert(requesttolocal == true);
            if (!req->btransient)
            {
                // rips the token off the line
                unsigned int newreqtoken = req->tokenacquired + line->tokencount;
                // group token count
                int newlinetokenline = line->ntokenline + req->gettokenpermanent();

                bool newgrouppriority = req->bpriority || line->priority || line->grouppriority;

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "request acquired " <<  line->tokencount << " tokens from the local directory and added " << req->gettokenpermanent() << " tokens to local group" << endl;
                LOG_VERBOSE_END

                UpdateRequest(req, line, newreqtoken, req->bpriority||line->priority);
                UpdateDirLine(line, req, line->state, 0, newlinetokenline, line->ntokenrem, false, newgrouppriority, line->breserved);
            }
            else
            {
                if (line->priority)
                {
                    line->priority = false;
                    req->btransient = false;
                    req->bpriority = true;
                }

                // rips the token off the line
                unsigned int newreqtoken = req->tokenacquired + line->tokencount;
                // group token count
                int newlinetokenline = line->ntokenline + req->gettokenpermanent();

                bool newgrouppriority = req->bpriority || line->priority || line->grouppriority;

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "request acquired " <<  line->tokencount << " tokens from the local directory and added " << req->gettokenpermanent() << " tokens to local group" << endl;
                LOG_VERBOSE_END

                UpdateRequest(req, line, newreqtoken, req->bpriority||line->priority);
                UpdateDirLine(line, req, line->state, 0, newlinetokenline, line->ntokenrem, false, newgrouppriority, line->breserved);

            }

            pop_initiator_node(req);
        }
        else    // if (requestbelongstolocal)
        {
            if (requesttolocal)
            {
                if (req->btransient)
                {
                    if (line->priority)
                    {
                        line->priority = false;
                        req->btransient = false;
                        req->bpriority = true;
                    }
                }

                unsigned int newreqtoken = req->tokenacquired + (req->btransient?0:line->tokencount);
                unsigned int newlinetoken = (req->btransient?line->tokencount:0);
                int newlinetokenrem = line->ntokenrem + req->gettokenpermanent() + (req->btransient?0:line->tokencount);
                bool newgrouppriority = line->priority || req->bpriority || line->grouppriority;
                bool newreqpriority = req->bpriority || line->priority;

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "request acquired " <<  newreqtoken - req->tokenacquired << " tokens from the directory line and added " << req->gettokenpermanent() << " tokens to local group" << endl;
                LOG_VERBOSE_END

                UpdateRequest(req, line, newreqtoken, newreqpriority);
                UpdateDirLine(line, req, line->state, newlinetoken, line->ntokenline, newlinetokenrem, false, newgrouppriority, line->breserved);

                ADD_INITIATOR_NODE(req, this);
                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "request acquired " <<  newreqtoken - req->tokenacquired << " tokens from the directory line and added " << req->gettokenpermanent() << " tokens to local group" << endl;
                LOG_VERBOSE_END

            }
            else    // if (requesttolocal)
            {
                if (req->btransient)
                {
                    if (line->priority)
                    {
                        line->priority = false;
                        req->btransient = false;
                        req->bpriority = true;
                    }
                }
            
                unsigned int newreqtoken = req->tokenacquired + (req->btransient?0:line->tokencount);
                unsigned int newlinetoken = (req->btransient?line->tokencount:0);
                bool newreqpriority = req->bpriority || line->priority;

                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
                    clog << LOG_HEAD_OUTPUT << "request acquired " <<  newreqtoken - req->tokenacquired << " tokens from the local directory." << endl;
                LOG_VERBOSE_END

                UpdateRequest(req, line, newreqtoken, newreqpriority);
                UpdateDirLine(line, req, line->state, newlinetoken, line->ntokenline, line->ntokenrem, false, line->grouppriority, line->breserved);

            }
        }
    }

    PostUpdateDirLine(line, req, requestbelongstolocal, requestfromlocal, requesttolocal);
}


void DirectoryTOK::PostUpdateDirLine(dir_line_t* line, ST_request* req, bool belongstolocal, bool fromlocal, bool tolocal)
{
//    if ((line->tokencount == GetTotalTokenNum()) && (nrquestin == 0))
//    {
//        line->state = DLS_INVALID;
//
//    }

    // REVISIT
    if (line->ntokenrem < 0)
    {
        assert(line->ntokenline > 0);
        line->ntokenline += line->ntokenrem;
        line->ntokenrem = 0;
    }

    // means that some previous DD has been absorbed by lines, 
    // current states are not so precise, thus reorganize
    if (line->ntokenline < 0)
    {
        assert(line->ntokenrem > 0);

        line->ntokenrem += line->ntokenline;
        line->ntokenline = 0;

//        if ((line->ntokenrem == 0)&&(line->nrequestin != 0))
//        {
////            clog << line->tokencount << "|" << line->ntokenline << "|" << line->ntokenrem << endl;
//            // just to check, simply alert
////            assert(false);
//        }
    }
    assert(line->ntokenline >= 0);
   
    // do not care when remote request come in, mind only the cases that local to local or local to global
    if ((!fromlocal)&&tolocal)
    {
        if ((line->nrequestin == 0)&&(line->nrequestout == 0)
            &&(line->tokencount == 0)&&(line->ntokenline == 0)&&(line->ntokenrem == 0))
        {
            line->state = DLS_INVALID;
            line->tokencount = 0;
            line->ntokenrem = 0;
            line->ntokenline = 0;
            line->counter = 0;
            line->priority = false;
            line->nrequestin = 0;
            line->nrequestout = 0;
            line->grouppriority = false;
        }
        return;
    }
    if ((line->ntokenline == 0) && (line->nrequestout == 0))
    {
        if ((line->ntokenrem > 0)||(line->nrequestin > 0))
        {
            // need to check out and deal with it, REVISIT
            if (belongstolocal)
            {
                assert(line->nrequestout == 0);
                // evict the line, and put the info in evicted line buffer in advance
                //assert((req->type == REQUEST_DISSEMINATE_TOKEN_DATA)||(req->type == REQUEST_ACQUIRE_TOKEN));
                //assert(line->nrequestin > 0);
                // add the information to the evicted line buffer
                bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());
		(void)evictedhit;
                assert(evictedhit == false);
                m_evictedlinebuffer.AddEvictedLine(req->getlineaddress(), line->nrequestin, line->ntokenrem, line->grouppriority);

                // evict line
                assert(line->tokencount == 0);
                line->state = DLS_INVALID;
                line->tokencount = 0;
                line->ntokenrem = 0;
                line->ntokenline = 0;
                line->counter = 0;
                line->priority = false;
                line->nrequestin = 0;
                line->nrequestout = 0;
                line->grouppriority = false;
            }
            else        // remote request
            {
                bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());
		(void)evictedhit;
                assert(evictedhit == false);
                m_evictedlinebuffer.AddEvictedLine(req->getlineaddress(), 
						   line->nrequestin, line->ntokenrem, line->grouppriority);

                // evict line
                assert(line->tokencount == 0);
                line->state = DLS_INVALID;
                line->tokencount = 0;
                line->ntokenrem = 0;
                line->ntokenline = 0;
                line->counter = 0;
                line->priority = false;
                line->nrequestin = 0;
                line->nrequestout = 0;
                line->grouppriority = false;
                // *** sometoken token could be in EV, this could lead to insufficient line problem in directory
//                // if no remote request are in, all the rest tokens should belong to line, EV? 
//                if (line->nrequestin == 0)
//                {
//                    line->ntokenline = line->ntokenrem;
//                    line->ntokenrem = 0;
//                }
            }
        }
        else if (line->ntokenrem < 0)
        {
            // forbiddden
            assert(false);
        }
        else
        {
            assert(line->nrequestout == 0);
            assert(line->tokencount == 0);
            line->state = DLS_INVALID;
            line->tokencount = 0;
            line->ntokenrem = 0;
            line->ntokenline = 0;
            line->counter = 0;
            line->priority = false;
            line->nrequestin = 0;
            line->nrequestout = 0;
            line->grouppriority = false;
        }
    }
    else if ((line->ntokenline == 0) && (line->nrequestout == 1))
    {
        // this is to transfer remtoken to line token if possible.
//        if ((line->ntokenrem > 0) && (line->nrequestin == 0))
//        {
//            line->ntokenline = line->ntokenrem;
//            line->ntokenrem = 0;
//        }
    }
}

#endif


// this probably only works with current naive configuration
bool DirectoryTOK::IsRequestLocal(ST_request* req, bool recvfrombelow)
{
    if (recvfrombelow)
    {
        if (IS_NODE_INITIATOR(req, this))
        {
            return false;
        }
        else
        {
//            assert( ((SetAssociativeProp*)get_initiator_node(req))->IsDirectory() == false );
            return true;
        }
    }
    else
    {
//        assert( ((SetAssociativeProp*)get_initiator_node(req))->IsDirectory() == true );
        if (IS_NODE_INITIATOR(req, this))
        {
            return true;
        }
        else
        {
            return false;
        }
    }
}

bool DirectoryTOK::SendRequestABOtoBelow(ST_request* req)
{
    // send reply through the network below interface
    if (!GetBelowIF().RequestNetwork(req))
    {
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "Above protocol: Send request through A/B interface failed, buffer full" << endl;
        LOG_VERBOSE_END

        return false;
    }

    // feedback ? alert
    LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "Above protocol: Send request through A/B interface succeed." << endl;
    LOG_VERBOSE_END

    return true;
}

bool DirectoryTOK::SendRequestABOtoAbove(ST_request* req)
{
    // send reply through the network above interface
    if (!GetAboveIF().RequestNetwork(req))
    {
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "Above protocol: Send request through A/A interface failed, buffer full" << endl;
        LOG_VERBOSE_END

        return false;
    }

    // feedback ? alert
    LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "Above protocol: Send request through A/A interface succeed." << endl;
    LOG_VERBOSE_END

    return true;
}

void DirectoryTOK::SendRequestFromABO()
{
    bool allsent = true;

    if (m_pReqCurABO2Above)
    {
        if (SendRequestABOtoAbove(m_pReqCurABO2Above))
        {
            // clear
            m_pReqCurABO2Above = NULL;
        }
        else
        {
            allsent &= false;
        }
    }

    if (m_pReqCurABO2Below)
    {
        if (SendRequestABOtoBelow(m_pReqCurABO2Below))
        {
            // clear
            m_pReqCurABO2Below = NULL;
        }
        else
        {
            allsent &= false;
        }
    }

    if (allsent) 
    {
        m_nStateABO =  STATE_ABOVE_PROCESSING;
    }
    else
    {
        m_nStateABO =  STATE_ABOVE_RETRY;
    }
}




//////////////////////////////////////////////////////////////////////////
// common handler

bool DirectoryTOK::MayHandle(ST_request* req)
{

	return true;
}

bool DirectoryTOK::CanHandleNetRequest(ST_request* request)
{
	// 
	return true;

}


dir_line_t* DirectoryTOK::LocateLine(__address_t address)
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

dir_line_t* DirectoryTOK::LocateLineEx(__address_t address)
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

dir_line_t* DirectoryTOK::GetReplacementLine(__address_t address)
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

//dir_line_t* DirectoryTOK::GetEmptyLine(__address_t address)
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

unsigned int DirectoryTOK::DirIndex(__address_t address)
{
	uint64 addr = address;
	return (unsigned int)( (addr>>s_nLineBits) & (m_nSet-1) );
}

uint64 DirectoryTOK::DirTag(__address_t address)
{
	uint64 addr = address;
	return (uint64)((addr) >> (s_nLineBits + m_nSetBits));
}

#endif
