#include "directorytok.h"

namespace MemSim
{

void DirectoryTOK::InitializeDirLines()
{
	// allocate sets
	m_pSet = (dir_set_t*)malloc(m_nSet * sizeof(dir_set_t));

	// Allocate lines
	for (unsigned int i=0;i<m_nSet;i++)
	{
		m_pSet[i].lines = (dir_line_t*)malloc(m_nAssociativity * sizeof(dir_line_t));
		for (unsigned int j=0;j<m_nAssociativity;j++)
		{
            m_pSet[i].lines[j].state = DLS_INVALID;
            m_pSet[i].lines[j].tokencount = 0;
            m_pSet[i].lines[j].tokengroup = 0;
            m_pSet[i].lines[j].counter = 0;
            m_pSet[i].lines[j].queuehead = SuspendedRequestQueue::EOQ;
            m_pSet[i].lines[j].queuetail = SuspendedRequestQueue::EOQ;
            m_pSet[i].lines[j].breserved = false;
            m_pSet[i].lines[j].priority = false;

            m_pSet[i].lines[j].bdataavailable = false;

            m_pSet[i].lines[j].setid = i;

            m_pSet[i].lines[j].nrequestin = 0;
            m_pSet[i].lines[j].nrequestout = 0;
            m_pSet[i].lines[j].grouppriority = false;
            m_pSet[i].lines[j].ntokenline = 0;
            m_pSet[i].lines[j].ntokenrem = 0;
		}
	}
}

bool DirectoryTOK::SendRequestBELtoBelow(ST_request* req)
{
    NetworkBelow_Node::SendRequest(req);
    return true;
}

// send reply through the network above interface
bool DirectoryTOK::SendRequestBELtoAbove(ST_request* req)
{
    // send reply through the network above interface
    NetworkAbove_Node::SendRequest(req);
    return true;
}

void DirectoryTOK::SendRequestFromBEL()
{
    bool allsent = true;
    if (m_pReqCurBEL2a != NULL)
    {
        if (SendRequestBELtoAbove(m_pReqCurBEL2a))
        {
            // succeed

            // clear
            m_pReqCurBEL2a = NULL;
        }
        else
        {
            allsent = false;
        }
    }

    if (m_pReqCurBEL2b != NULL)
    {
        if (SendRequestBELtoBelow(m_pReqCurBEL2b))
        {
            // succeed

            // clear
            m_pReqCurBEL2b = NULL;
        }
        else
        {
            allsent = false;
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
    ST_request* req = m_pReqCurBEL;
    assert(req->bqueued == false);

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
        
    case Request_LOCALDIR_NOTIFICATION:
        OnBELDirNotification(req);
        break;

    default:
        // Error
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
        m_pReqCurBEL2a = NULL;
        m_pReqCurBEL2b = NULL;

        if (!m_lstReqB2a.empty())
        {
            m_pReqCurBEL2a = m_lstReqB2a.front();
            m_lstReqB2a.pop_front();
        }

        if (!m_lstReqB2b.empty())
        {
            m_pReqCurBEL2b = m_lstReqB2b.front();
            m_lstReqB2b.pop_front();
        }

        // fetch the request from the correct interface
        req_incoming = FetchRequestNet(true); 
        m_pReqCurBEL = m_pPipelineBEL.shift(req_incoming);

        if (m_pReqCurBEL != NULL)
            ProcessRequestBEL();

        if (m_pReqCurBEL2a != NULL || m_pReqCurBEL2b != NULL)
            SendRequestFromBEL();

        break;

    case STATE_BELOW_RETRY:
        if (m_pPipelineBEL.top() == NULL)
        {
            // fetch request from incoming buffer
            req_incoming = FetchRequestNet(true);

            // only shift
            m_pPipelineBEL.shift(req_incoming);

        }

        assert( (m_pReqCurBEL2b != NULL)||(m_pReqCurBEL2a != NULL) );
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
    if (below) {
        return NetworkBelow_Node::ReceiveRequest();
    }
    return NetworkAbove_Node::ReceiveRequest();
}

void DirectoryTOK::ProcessRequestABO()
{
    ST_request* req = m_pReqCurABO;
    assert(req->bqueued == false);

    switch (req->type)
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

    default:
        // Error
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
        m_pReqCurABO2a = NULL;
        m_pReqCurABO2b = NULL;

        if (!m_lstReqA2a.empty())
        {
            m_pReqCurABO2a = m_lstReqA2a.front();
            m_lstReqA2a.pop_front();
        }

        if (!m_lstReqA2b.empty())
        {
            m_pReqCurABO2b = m_lstReqA2b.front();
            m_lstReqA2b.pop_front();
        }

        // fetch the request from the correct interface
        req_incoming = FetchRequestNet(false); 
        m_pReqCurABO = m_pPipelineABO.shift(req_incoming);

        if (m_pReqCurABO != NULL)
            ProcessRequestABO();

        if ((m_pReqCurABO2a != NULL)||(m_pReqCurABO2b != NULL))
            SendRequestFromABO();

        break;

    case STATE_ABOVE_RETRY:
        if (m_pPipelineABO.top() == NULL)
        {
            // fetch request from incoming buffer
            req_incoming = FetchRequestNet(false);

            // only shift
            m_pPipelineABO.shift(req_incoming);

        }

        assert( (m_pReqCurABO2b != NULL)||(m_pReqCurABO2a != NULL) );
        SendRequestFromABO();
        break;

    default:
      abort();
        break;
    }
}

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
            return line->ntokenline + line->ntokenrem <= 0 || req->bprocessed;
        }
        
        // RE, ER
        return (unsigned)tokenlocalgroup != GetTotalTokenNum() || line->nrequestin != 0 || line->nrequestout != 0;
    }
    
    // IV
    assert(req->type == MemoryState::REQUEST_ACQUIRE_TOKEN);
    return (unsigned)tokenlocalgroup != GetTotalTokenNum() || line->nrequestin != 0 || line->nrequestout != 0;
}

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
                    if (req->type == REQUEST_DISSEMINATE_TOKEN_DATA)
                    {
                        int newlinetokenline = line->tokencount + line->ntokenline - req->tokenacquired;
                        int newlinetokenrem = line->ntokenrem + req->tokenacquired;
                        bool newgrouppriority = req->bpriority || line->priority || line->grouppriority;

                        UpdateRequest(req, line, req->tokenacquired + line->tokencount, req->bpriority||line->priority);
                        UpdateDirLine(line, req, line->state, 0, newlinetokenline, newlinetokenrem, false, newgrouppriority, line->breserved);

                        // even stay in local, it will be regarded as a remote request from now
                        ADD_INITIATOR(req, this);
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

                    bool newgrouppriority = (!(req->bpriority || line->priority)) && line->grouppriority;

                    UpdateRequest(req, line, newreqtoken, req->bpriority||line->priority);
                    UpdateDirLine(line, req, line->state, newlinetoken, newlinetokenline, line->ntokenrem, false, newgrouppriority, line->breserved);

                }

            }
            else
            {
                if (!req->btransient)
                {
                    int newlinetokenline = line->ntokenline - req->gettokenpermanent();
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
                    bool newgrouppriority = (!(req->bpriority || line->priority)) && line->grouppriority;

                    UpdateRequest(req, line, req->tokenacquired + line->tokencount, req->bpriority||line->priority);
                    UpdateDirLine(line, req, line->state, 0, newlinetokenline, line->ntokenrem, line->priority, newgrouppriority, line->breserved);

                }

                // add initiator for naive solution
                ADD_INITIATOR(req, this);

            }
        }
        else
        {
            // always send the request to upper level
            assert(requesttolocal == false);

            if (!req->btransient)
            {
                unsigned int newreqtoken = req->tokenacquired + line->tokencount;
                int newlinetokenline = line->ntokenline - req->gettokenpermanent();

                bool newgrouppriority = (!(req->bpriority||line->priority)) && line->grouppriority;

                UpdateRequest(req, line, newreqtoken, req->bpriority||line->priority);
                UpdateDirLine(line, req, line->state, 0, newlinetokenline, line->ntokenrem, false, newgrouppriority, line->breserved);
            }
            else
            {
                assert(req->gettokenpermanent() == 0);
                int newlinetokenrem = line->ntokenrem - req->gettokenpermanent();
                bool newgrouppriority = (!(req->bpriority||line->priority)) && line->grouppriority;
                UpdateDirLine(line, req, line->state, line->tokencount, line->ntokenline, newlinetokenrem, line->priority, newgrouppriority, line->breserved);
            }

            // pop out the initiator
            pop_initiator(req);
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

                UpdateRequest(req, line, newreqtoken, req->bpriority||line->priority);
                UpdateDirLine(line, req, line->state, 0, newlinetokenline, line->ntokenrem, false, newgrouppriority, line->breserved);

            }

            pop_initiator(req);
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

                UpdateRequest(req, line, newreqtoken, newreqpriority);
                UpdateDirLine(line, req, line->state, newlinetoken, line->ntokenline, newlinetokenrem, false, newgrouppriority, line->breserved);

                ADD_INITIATOR(req, this);
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

                UpdateRequest(req, line, newreqtoken, newreqpriority);
                UpdateDirLine(line, req, line->state, newlinetoken, line->ntokenline, line->ntokenrem, false, line->grouppriority, line->breserved);

            }
        }
    }

    PostUpdateDirLine(line, req, requestbelongstolocal, requestfromlocal, requesttolocal);
}


void DirectoryTOK::PostUpdateDirLine(dir_line_t* line, ST_request* req, bool belongstolocal, bool fromlocal, bool tolocal)
{
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
    }
    assert(line->ntokenline >= 0);
   
    // do not care when remote request come in, mind only the cases that local to local or local to global
    if (!fromlocal && tolocal)
    {
        if (line->nrequestin == 0 && line->nrequestout == 0 &&
            line->tokencount == 0 && line->ntokenline  == 0 && line->ntokenrem == 0)
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
            }
        }
        else 
        {
            assert(line->ntokenrem >= 0);
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
}

// this probably only works with current naive configuration
bool DirectoryTOK::IsRequestLocal(ST_request* req, bool recvfrombelow)
{
    return (recvfrombelow)
        ? !(IS_INITIATOR(req, this))
        :   IS_INITIATOR(req, this);
}

bool DirectoryTOK::SendRequestABOtoBelow(ST_request* req)
{
    // send reply through the network below interface
    NetworkBelow_Node::SendRequest(req);
    return true;
}

bool DirectoryTOK::SendRequestABOtoAbove(ST_request* req)
{
    // send reply through the network above interface
    NetworkAbove_Node::SendRequest(req);
    return true;
}

void DirectoryTOK::SendRequestFromABO()
{
    bool allsent = true;

    if (m_pReqCurABO2a)
    {
        if (SendRequestABOtoAbove(m_pReqCurABO2a))
        {
            // clear
            m_pReqCurABO2a = NULL;
        }
        else
        {
            allsent = false;
        }
    }

    if (m_pReqCurABO2b)
    {
        if (SendRequestABOtoBelow(m_pReqCurABO2b))
        {
            // clear
            m_pReqCurABO2b = NULL;
        }
        else
        {
            allsent = false;
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

dir_line_t* DirectoryTOK::LocateLine(__address_t address)
{
    unsigned int index = DirIndex(address);
    uint64 tag = DirTag(address);

    dir_line_t* set = &m_pSet[index].lines[0];
    for (unsigned int i = 0; i < m_nAssociativity; ++i)
    {
    	if (set[i].state != DLS_INVALID && set[i].tag == tag)
    	{
    		return &set[i];
        }
    }
    return NULL;
}

dir_line_t* DirectoryTOK::GetReplacementLine(__address_t address)
{
    unsigned int index = DirIndex(address);

    dir_line_t* set = &m_pSet[index].lines[0];
    for (unsigned int i = 0; i < m_nAssociativity; ++i)
    {
    	// return the first found empty one
    	if (set[i].state == DLS_INVALID)
    	{
    		return &set[i];;
        }
    }
    return NULL;
}

unsigned int DirectoryTOK::DirIndex(__address_t address)
{
    return (address / m_nLineSize) % m_nSet;
}

uint64 DirectoryTOK::DirTag(__address_t address)
{
    return (address / m_nLineSize) / m_nSet;
}

void DirectoryTOK::OnBELAcquireTokenData(ST_request* req)
{
    __address_t address = req->getreqaddress();
    
    // locate certain set
    dir_line_t* line = LocateLine(address);

    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());

    if (IsRequestLocal(req, true))
    {
        if (req->tokenacquired > 0)
        {
            assert(line != NULL || evictedhit);
        }
        
        if (line == NULL)
        {
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

            if (evictedhit)
                m_evictedlinebuffer.DumpEvictedLine2Line(req->getlineaddress(), line);

            // prepare the request to send to upper level
            ADD_INITIATOR(req, this);

            // save the request
            m_lstReqB2a.push_back(req);

            line->nrequestout++;

            return;
        }

        // make sure that no buffer hit
        assert(evictedhit == false);

        if (req->tokenrequested < GetTotalTokenNum())  // read: RS, SR
        {
            if (ShouldLocalReqGoGlobal(req, line))
            {
                // no token in local leve, the line must be acquiring tokens from somewhere else

                 // transfer tokens to request, if any.
                Update_RequestRipsLineTokens(true, true, false, req, line, 0, 1);

                // send the request to upper level
                m_lstReqB2a.push_back(req);
            }
            else
            {
                // if there are lines in the same level
                // then let the request stay in the same level

                // UpdateDirLine();
                line->time = sc_time_stamp();

                // if directory has token in hand, then hand it over to the reqeust.
                if (line->tokencount > 0)
                {
		            assert (!req->btransient);  // RS/SR cannot be transient request
                    Update_RequestRipsLineTokens(true, true, true, req, line);
                }

                // save the reqeust 
                m_lstReqB2b.push_back(req);
            }
        }
        // RE, ER
        else if (ShouldLocalReqGoGlobal(req, line))
        {
            // need to go out the local level

            // Update request and line
            Update_RequestRipsLineTokens(true, true, false, req, line, 0, 1);

            m_lstReqB2a.push_back(req);
        }
        else // all tokens are in local level
        {
            //no necessary to go outside

            // make sure at least some cache has the data
            assert((line->ntokenline+line->ntokenrem)> 0);

            // Update request and line
            Update_RequestRipsLineTokens(true, true, true, req, line);

            m_lstReqB2b.push_back(req);
        }
    }
    else if (line == NULL)
    {
        // probably there should be remote reuqest inside local level in this case
        // prepare the request to send to upper level
        // just go out
        m_lstReqB2a.push_back(req);

        assert (evictedhit);
        m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), false, req->gettokenpermanent(), req->bpriority);

        // pop initiator
        pop_initiator(req);
    }
    else
    {
        assert(evictedhit == false);

        // get token from the directory if any        
        Update_RequestRipsLineTokens(false, true, false, req, line, -1);

        // remote request is going out anyway
        m_lstReqB2a.push_back(req);
    }
}


void DirectoryTOK::OnBELAcquireToken(ST_request* req)
{
    assert(req->tokenrequested == GetTotalTokenNum());

    __address_t address = req->getreqaddress();
    
    // locate certain set
    dir_line_t* line = LocateLine(address);

    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());


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

            if (evictedhit)
                m_evictedlinebuffer.DumpEvictedLine2Line(req->getlineaddress(), line);

            // prepare the request to send to upper level
            ADD_INITIATOR(req, this);

            // save the request
            m_lstReqB2a.push_back(req);

            line->nrequestout++;

            return;

        }

        assert(evictedhit == false);

        // request is IV
        if (ShouldLocalReqGoGlobal(req, line))
        {
            // need to go out the local level

            // Update request and line
            Update_RequestRipsLineTokens(true, true, false, req, line, 0, 1);

            m_lstReqB2a.push_back(req);

        }
        else // all tokens are in local level
        {
            //no necessary to go outside

            // make sure at least some cache has the data
            assert((line->ntokenline+line->ntokenrem) > 0);

            // Update request and line
            Update_RequestRipsLineTokens(true, true, true, req, line);

            m_lstReqB2b.push_back(req);
        }

        return;
    }
    else
    {
        if (line == NULL)
        {
            // prepare the request to send to upper level

            // just go out
            m_lstReqB2a.push_back(req);

            assert (evictedhit);
	        m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), false, req->gettokenpermanent(), req->bpriority);

            // pop initiator
            pop_initiator(req);

            return;
        }

        assert(evictedhit == false);

        // get token from the directory if any        
        Update_RequestRipsLineTokens(false, true, false, req, line, -1, 0);

        // remote request is going out anyway
        m_lstReqB2a.push_back(req);
    }
}

void DirectoryTOK::OnBELDisseminateTokenData(ST_request* req)
{
    // EV request will always terminate at directory
    __address_t address = req->getreqaddress();

    assert(req->tokenacquired > 0);

    // locate certain set
    dir_line_t* line = LocateLine(address);

    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());
    (void)evictedhit;


    if (IsRequestLocal(req, true))
    {
        if (line == NULL)
        {
            // send the request to upper level
            m_lstReqB2a.push_back(req);

            assert(evictedhit);
	        m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), false, req->gettokenpermanent(), req->bpriority, true);
            return;
        }

        assert(evictedhit == false);

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
            Update_RequestRipsLineTokens(true, true, false, req, line);

            // save the request
            m_lstReqB2a.push_back(req);
        }
	    else if ((int)req->tokenacquired < line->ntokenline)
        {
            if (req->tokenrequested == 0)   // EV
            {
                // just stack, no ripping
                line->ntokenline -= req->tokenacquired;
                line->tokencount += req->tokenacquired;
                line->priority |= req->bpriority;
                delete req;
            }
            else if (req->tokenrequested == GetTotalTokenNum()) // WB
            {
                Update_RequestRipsLineTokens(true, true, false, req, line);

                // save the request
                m_lstReqB2a.push_back(req);
            }
            else
                abort();
        }
        else
        {
            // send out
            // should always go global
            Update_RequestRipsLineTokens(true, true, false, req, line);

            // save the request
            m_lstReqB2a.push_back(req);
        }
    }
    // global request
    else if (line == NULL)
    {
        // just dispatch to the upper level
        m_lstReqB2a.push_back(req);

        assert (evictedhit);
        m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), false, req->gettokenpermanent(), req->bpriority, true);
    }
    else
    {
        assert(evictedhit == false);

        // update the directory
        Update_RequestRipsLineTokens(false, true, false, req, line);

        // remote request is going out anyway
        m_lstReqB2a.push_back(req);
    }
}


void DirectoryTOK::OnBELDirNotification(ST_request* req)
{
    // address
    __address_t address  = req->getreqaddress();

    // locate certain line
    dir_line_t* line = LocateLine(address);

    // evicted line buffer

    // the line must be existing
    assert(line != NULL);

    line->ntokenline += req->tokenacquired;

    // terminate the request
    delete req;
}

//////////////////////////////////////////////////////////////////////////
// ABOVE PROTOCOL TRANSACTION HANDLER

void DirectoryTOK::OnABOAcquireTokenData(ST_request* req)
{
    // correct the counter and state before the 

    __address_t address = req->getreqaddress();
    
    // locate certain set
    dir_line_t* line = LocateLine(address);

    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());

    if (IsRequestLocal(req, false))
    {
        assert (line != NULL);

        if (req->tokenrequested < GetTotalTokenNum())  // read: RS, SR
        {
            // always go local
            m_lstReqA2b.push_back(req);

            // pop the initiator/dir in the update function
            // Update the dir
            Update_RequestRipsLineTokens(true, false, true, req, line, 0, -1);
        }
        else
        {
            // always go local
            m_lstReqA2b.push_back(req);

            // pop the initiator/dir in the update function
            // Update the dir
            Update_RequestRipsLineTokens(true, false, true, req, line, 0, -1);
        }
    }
    // remote request
    // as long as the line exist, the requet, no matter RS or RE, has to get in
    else if ((line == NULL)&&(!evictedhit))
    {
        // just go to above level
        m_lstReqA2a.push_back(req);
    }
    else    // somehting inside lower level, just always get in
    {
        if (evictedhit)
        {
            // get in lower level, but update the evicted buffer
            m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), true, req->gettokenpermanent(), req->bpriority);
            ADD_INITIATOR(req, this);
        }
        else
        {
            Update_RequestRipsLineTokens(false, false, true, req, line, 1);
        }

        // get in lower level
        m_lstReqA2b.push_back(req);
    }
}


void DirectoryTOK::OnABOAcquireToken(ST_request* req)
{
    // correct the counter and state before the 
    __address_t address = req->getreqaddress();
    
    // locate certain set
    dir_line_t* line = LocateLine(address);

    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress());

    if (IsRequestLocal(req, false))
    {
        if (req->tokenacquired > 0)
            assert(line != NULL);

        assert (line != NULL);


        // always go local
        m_lstReqA2b.push_back(req);

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
                m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), true, req->gettokenpermanent(), req->bpriority);

                ADD_INITIATOR(req, this);

                // get in local level
                m_lstReqA2b.push_back(req);
            }
            else
            {
                // just go to above level
                m_lstReqA2a.push_back(req);
            }
        }
        else    // somehting inside lower level, just always get in
        {
            if (evictedhit)
            {
                // get in lower level, but update the evicted buffer
                m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), true, req->gettokenpermanent(), req->bpriority);

                ADD_INITIATOR(req, this);
            }
            else
            {
                Update_RequestRipsLineTokens(false, false, true, req, line, 1);
            }

            // get in lower level
            m_lstReqA2b.push_back(req);
        }
    }
}


void DirectoryTOK::OnABODisseminateTokenData(ST_request* req)
{
    // EV request will always terminate at directory
    __address_t address = req->getreqaddress();

    assert(req->tokenacquired > 0);

    // locate certain set
    dir_line_t* line = LocateLine(address);

    // evicted line buffer
    unsigned int requestin = 0;
    unsigned int tokenrem;
    bool grouppriority;
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->getlineaddress(), requestin, tokenrem, grouppriority);

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

    if (evictedhit) // REVIST, JXXX, this may not be necessary
    {
        if (requestin == 0)
        {
            // skip the local group to next group
            // get in lower level, but update the evicted buffer
            // to above level
            m_lstReqA2a.push_back(req);
        }
        else
        {
            // get in lower level, but update the evicted buffer
            m_evictedlinebuffer.UpdateEvictedLine(req->getlineaddress(), true, req->gettokenpermanent(), req->bpriority, true);

            // lower level
            m_lstReqA2b.push_back(req);
        }
    }
    else if (line == NULL)
    {
        // skip the local level and pass it on
        m_lstReqA2a.push_back(req);
    }
    else if (line->nrequestin != 0 || line->nrequestout != 0)
    {
        // lower level
        m_lstReqA2b.push_back(req);

        // assume it's from outside not local,
        Update_RequestRipsLineTokens(false, false, true, req, line);
    }
    else
    {
        assert(line->ntokenline + line->ntokenrem > 0);
        // leave the tokens on the line. without getting in or send to the next node

        // notgoing anywhere, just terminate the request
        assert(req->tokenacquired < GetTotalTokenNum());
        line->tokencount += req->tokenacquired;
        line->priority |= req->bpriority;

        delete req;
    }
}

}
