#include "directorytok.h"
using namespace std;

namespace MemSim
{

void DirectoryTOK::SendRequestFromBEL()
{
    if (m_pReqCurBEL2a != NULL)
    {
        if (NetworkAbove_Node::SendRequest(m_pReqCurBEL2a))
        {
            m_pReqCurBEL2a = NULL;
        }
    }

    if (m_pReqCurBEL2b != NULL)
    {
        if (NetworkBelow_Node::SendRequest(m_pReqCurBEL2b))
        {
            m_pReqCurBEL2b = NULL;
        }
    }

    m_nStateBEL = (m_pReqCurBEL2a == NULL && m_pReqCurBEL2b == NULL)
        ? STATE_BELOW_PROCESSING
        : STATE_BELOW_RETRY;
}


void DirectoryTOK::ProcessRequestBEL()
{
    Message* req = m_pReqCurBEL;
    assert(req->bqueued == false);

    switch(req->type)
    {
    case Message::ACQUIRE_TOKEN:
        OnBELAcquireToken(req);
        break;
        
    case Message::ACQUIRE_TOKEN_DATA:
        OnBELAcquireTokenData(req);
        break;
        
    case Message::DISSEMINATE_TOKEN_DATA:
        OnBELDisseminateTokenData(req);
        break;
        
    case Message::LOCALDIR_NOTIFICATION:
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
    Message* req_incoming = NULL;

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
        req_incoming = NetworkBelow_Node::ReceiveRequest();
        m_pReqCurBEL = m_pPipelineBEL.shift(req_incoming);

        if (m_pReqCurBEL != NULL)
        {
            ProcessRequestBEL();
        }

        if (m_pReqCurBEL2a != NULL || m_pReqCurBEL2b != NULL)
            SendRequestFromBEL();

        break;

    case STATE_BELOW_RETRY:
        if (m_pPipelineBEL.top() == NULL)
        {
            // fetch request from incoming buffer
            req_incoming = NetworkBelow_Node::ReceiveRequest();

            // only shift
            m_pPipelineBEL.shift(req_incoming);

        }

        assert(m_pReqCurBEL2b != NULL || m_pReqCurBEL2a != NULL);
        SendRequestFromBEL();
        break;

    default:
        abort();
        break;
    }
}

void DirectoryTOK::ProcessRequestABO()
{
    Message* req = m_pReqCurABO;
    assert(req->bqueued == false);

    switch (req->type)
    {
    case Message::ACQUIRE_TOKEN:
        OnABOAcquireToken(req);
        break;
        
    case Message::ACQUIRE_TOKEN_DATA:
        OnABOAcquireTokenData(req);
        break;
        
    case Message::DISSEMINATE_TOKEN_DATA:
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
    Message* req_incoming = NULL;

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
        req_incoming = NetworkAbove_Node::ReceiveRequest();
        m_pReqCurABO = m_pPipelineABO.shift(req_incoming);

        if (m_pReqCurABO != NULL)
            ProcessRequestABO();

        if (m_pReqCurABO2a != NULL || m_pReqCurABO2b != NULL)
            SendRequestFromABO();

        break;

    case STATE_ABOVE_RETRY:
        if (m_pPipelineABO.top() == NULL)
        {
            // fetch request from incoming buffer
            req_incoming = NetworkAbove_Node::ReceiveRequest();

            // only shift
            m_pPipelineABO.shift(req_incoming);
        }

        assert(m_pReqCurABO2b != NULL || m_pReqCurABO2a != NULL);
        SendRequestFromABO();
        break;

    default:
        abort();
        break;
    }
}

// this probably only works with current naive configuration
bool DirectoryTOK::IsBelow(CacheID id) const
{
    return (id >= m_firstCache) && (id <= m_lastCache);
}

void DirectoryTOK::SendRequestFromABO()
{
    if (m_pReqCurABO2a != NULL)
    {
        if (NetworkAbove_Node::SendRequest(m_pReqCurABO2a))
        {
            m_pReqCurABO2a = NULL;
        }
    }

    if (m_pReqCurABO2b != NULL)
    {
        if (NetworkBelow_Node::SendRequest(m_pReqCurABO2b))
        {
            m_pReqCurABO2b = NULL;
        }
    }

    m_nStateABO = (m_pReqCurABO2a == NULL && m_pReqCurABO2b == NULL) 
        ? STATE_ABOVE_PROCESSING
        : STATE_ABOVE_RETRY;
}

DirectoryTOK::dir_line_t* DirectoryTOK::LocateLine(MemAddr address)
{
    unsigned int index = (address / m_nLineSize) % m_nSet;
    MemAddr  tag   = (address / m_nLineSize) / m_nSet;

    dir_line_t* set = &m_pSet[index].lines[0];
    for (unsigned int i = 0; i < m_nAssociativity; ++i)
    {
    	if (set[i].valid && set[i].tag == tag)
    	{
    		return &set[i];
        }
    }
    return NULL;
}

DirectoryTOK::dir_line_t* DirectoryTOK::GetEmptyLine(MemAddr address)
{
    unsigned int index = (address / m_nLineSize) % m_nSet;

    dir_line_t* set = &m_pSet[index].lines[0];
    for (unsigned int i = 0; i < m_nAssociativity; ++i)
    {
    	if (!set[i].valid)
    	{
    		return &set[i];
        }
    }
    return NULL;
}

void DirectoryTOK::OnBELAcquireTokenData(Message* req)
{
    // locate certain set
    dir_line_t* line = LocateLine(req->address);

    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address);

    if (IsBelow(req->source))
    {
        if (req->tokenacquired > 0)
        {
            assert(line != NULL || evictedhit);
        }
        
        if (line == NULL)
        {
            // need to fetch a outside the subring

            // Allocate a space, must succeed
            line = GetEmptyLine(req->address);
            assert(line != NULL);

            // update line info
            line->tag = (req->address / m_nLineSize) / m_nSet;
            line->valid = true;
            line->tokencount = 0;
            line->ntokenline = 0;
            line->ntokenrem = 0;
            line->nrequestin = 0;
            line->nrequestout = 0;
            line->priority = false;

            if (evictedhit)
            {
                // Merge with the evicted line
                unsigned int nrequestin = 0, ntokenrem = 0;
                
                m_evictedlinebuffer.DumpEvictedLine2Line(req->address, nrequestin, ntokenrem);
                
                line->nrequestin += nrequestin;
                line->ntokenrem  += ntokenrem;
            }

            // save the request
            m_lstReqB2a.push_back(req);
            line->nrequestout++;
            return;
        }

        // make sure that no buffer hit
        assert(evictedhit == false);

        if (req->tokenrequested < GetTotalTokenNum())  // read: RS, SR
        {
            if (line->ntokenline + line->ntokenrem <= 0 || req->bprocessed)
            {
                // no token in local level, the line must be acquiring tokens from somewhere else

                // transfer tokens to request, if any.
                line->nrequestout++;
                if (req->btransient && line->priority)
                {
                    req->btransient = false;
                    req->bpriority  = true;
                    line->priority  = false;
                }

                int  newlinetokenline = line->ntokenline - req->gettokenpermanent();

                req->tokenacquired += line->tokencount;
                req->bpriority = req->bpriority || line->priority;

                line->tokencount    = 0;
                line->ntokenline    = newlinetokenline;
                line->priority      = false;

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

                // Do not care when remote request come in, mind only the cases that local to local or local to global
                if (line->ntokenline == 0 && line->nrequestout == 0)
                {
                    // Evict line
                    assert(line->ntokenrem >= 0);
                    assert(line->nrequestout == 0);

                    if (line->ntokenrem > 0 || line->nrequestin > 0)
                    {
                        // Need to check out and deal with it, REVISIT
                        // Put the information to the evicted line buffer.
                        assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                        m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                    }

                    line->valid = false;
                }

                // send the request to upper level
                m_lstReqB2a.push_back(req);
            }
            else
            {
                // If there are lines in the same level then let the request stay in the same level
                
                // if directory has tokens, hand them over to the reqeust.
                if (line->tokencount > 0)
                {
		            assert(!req->btransient);  // RS/SR cannot be transient request

                    line->ntokenline += line->tokencount;

                    req->tokenacquired += line->tokencount;
                    req->bpriority      = req->bpriority || line->priority;

                    line->tokencount    = 0;
                    line->priority      = false;

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

                    if (line->ntokenline == 0 && line->nrequestout == 0)
                    {
                        // Evict line
                        assert(line->ntokenrem >= 0);
                        assert(line->nrequestout == 0);

                        if (line->ntokenrem > 0 || line->nrequestin > 0)
                        {
                            // need to check out and deal with it, REVISIT
                            // Put the information in the evicted line buffer.
                            assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                            m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                        }

                        line->valid = false;
                    }
                }

                // save the reqeust 
                m_lstReqB2b.push_back(req);
            }
        }
        // RE, ER
        else if (line->tokencount + line->ntokenline + line->ntokenrem != GetTotalTokenNum() || line->nrequestin != 0 || line->nrequestout != 0)
        {
            // need to go out the local level

            // Update request and line
            line->nrequestout++;

            if (req->btransient && line->priority)
            {
                req->btransient = false;
                req->bpriority  = true;
                line->priority  = false;
            }
            
            int newlinetokenline = line->ntokenline - req->gettokenpermanent();

            req->tokenacquired += line->tokencount;
            req->bpriority      = req->bpriority || line->priority;

            line->tokencount    = 0;
            line->ntokenline    = newlinetokenline;
            line->priority      = false;

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

            if (line->ntokenline == 0 && line->nrequestout == 0)
            {
                // Evict line
                assert(line->ntokenrem >= 0);
                assert(line->nrequestout == 0);
                
                if (line->ntokenrem > 0 || line->nrequestin > 0)
                {
                    // need to check out and deal with it, REVISIT

                    // Put the information in the evicted line buffer
                    assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                    m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                }

                line->valid = false;
            }

            m_lstReqB2a.push_back(req);
        }
        else
        {
            // All tokens are in local level; not necessary to go outside.
            
            // Make sure at least some cache has the data.
            assert(line->ntokenline + line->ntokenrem > 0);

            req->tokenacquired += line->tokencount;
            req->bpriority      = req->bpriority || line->priority;
            
            if (!req->btransient)
            {
                line->ntokenline += line->tokencount;
                line->tokencount  = 0;
            }
            line->priority = false;

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

            if (line->ntokenline == 0 && line->nrequestout == 0)
            {
                // Evict line
                assert(line->ntokenrem >= 0);
                assert(line->nrequestout == 0);
                
                if (line->ntokenrem > 0 || line->nrequestin > 0)
                {
                    // need to check out and deal with it, REVISIT

                    // Put the information in the evicted line buffer
                    assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                    m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                }

                line->valid = false;
            }

            m_lstReqB2b.push_back(req);
        }
    }
    else if (line == NULL)
    {
        // Probably there should be remote request inside local level in this case
        assert(evictedhit);
        m_evictedlinebuffer.UpdateEvictedLine(req->address, false, req->gettokenpermanent());

        // Send the request to the upper level
        m_lstReqB2a.push_back(req);
    }
    else
    {
        assert(evictedhit == false);

        // get token from the directory if any        
        line->nrequestin--;

        // Send the request to upper level
        if (!req->btransient)
        {
            int newlinetokenline = line->ntokenline - req->tokenacquired;

            req->tokenacquired += line->tokencount;
            req->bpriority      = req->bpriority || line->priority;

            line->tokencount = 0;
            line->ntokenline = newlinetokenline;
            line->priority = false;
        }

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

        if (line->ntokenline == 0 && line->nrequestout == 0)
        {
            // Evict line
            assert(line->ntokenrem >= 0);
            assert(line->nrequestout == 0);
            
            if (line->ntokenrem > 0 || line->nrequestin > 0)
            {
                assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
            }

            line->valid = false;
        }

        // remote request is going out anyway
        m_lstReqB2a.push_back(req);
    }
}


void DirectoryTOK::OnBELAcquireToken(Message* req)
{
    assert(req->tokenrequested == GetTotalTokenNum());

    dir_line_t* line = LocateLine(req->address);

    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address);

    if (IsBelow(req->source))
    {
        if (line == NULL)
        {
            assert(req->tokenacquired == 0);
            // need to fetch a outside the subring

            // Allocate a space, must succeed
            line = GetEmptyLine(req->address);
            assert(line != NULL);

            // update line info
            line->tag = (req->address / m_nLineSize) / m_nSet;
            line->valid = true;
            line->tokencount = 0;
            line->ntokenline = 0;
            line->ntokenrem = 0;
            line->nrequestin = 0;
            line->nrequestout = 0;
            line->priority = false;

            if (evictedhit)
            {
                unsigned int nrequestin = 0, ntokenrem = 0;
                
                m_evictedlinebuffer.DumpEvictedLine2Line(req->address, nrequestin, ntokenrem);
                
                line->nrequestin += nrequestin;
                line->ntokenrem += ntokenrem;
            }

            // save the request
            m_lstReqB2a.push_back(req);

            line->nrequestout++;
        }
        else
        {
            assert(evictedhit == false);
            assert(req->tokenacquired > 0);

            // request is IV
            if (line->tokencount + line->ntokenline + line->ntokenrem != GetTotalTokenNum() || line->nrequestin != 0 || line->nrequestout != 0)
            {
                // need to go out the local level

                // Update request and line
                line->nrequestout++;
                if (req->btransient && line->priority)
                {
                    req->btransient = false;
                    req->bpriority  = true;
                    line->priority  = false;
                }
                int newlinetokenline = line->ntokenline - req->gettokenpermanent();

                req->tokenacquired += line->tokencount;
                req->bpriority      = req->bpriority || line->priority;

                line->tokencount    = 0;
                line->ntokenline    = newlinetokenline;
                line->priority      = false;

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

                if (line->ntokenline == 0 && line->nrequestout == 0)
                {
                    if (line->ntokenrem > 0 || line->nrequestin > 0)
                    {
                        // need to check out and deal with it, REVISIT
                        assert(line->nrequestout == 0);

                        // evict the line, and put the info in evicted line buffer in advance
                        // add the information to the evicted line buffer
                        bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address);
                        assert(evictedhit == false);
                        m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                    }
                    else
                    {
                        assert(line->ntokenrem >= 0);
                        assert(line->nrequestout == 0);
                    }

                    // evict line
                    line->valid = false;
                }

                m_lstReqB2a.push_back(req);
            }
            else // all tokens are in local level
            {
                //no necessary to go outside

                // make sure at least some cache has the data
                assert(line->ntokenline + line->ntokenrem > 0);

                // Update request and line
                req->tokenacquired += line->tokencount;
                req->bpriority      = req->bpriority || line->priority;
                    
                if (!req->btransient)
                {
                    line->ntokenline += line->tokencount;                    
                    line->tokencount  = 0;
                }
                line->priority = false;

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

                if (line->ntokenline == 0 && line->nrequestout == 0)
                {
                    // Evict line
                    assert(line->ntokenrem >= 0);
                    assert(line->nrequestout == 0);
                    
                    if (line->ntokenrem > 0 || line->nrequestin > 0)
                    {
                        // need to check out and deal with it, REVISIT

                        // Put the information in the evicted line buffer
                        assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                        m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                    }

                    line->valid = false;
                }

                m_lstReqB2b.push_back(req);
            }
        }
    }
    else
    {
        if (line == NULL)
        {
            // prepare the request to send to upper level

            // just go out
            m_lstReqB2a.push_back(req);

            assert (evictedhit);
	        m_evictedlinebuffer.UpdateEvictedLine(req->address, false, req->gettokenpermanent());
        }
        else
        {
            assert(evictedhit == false);

            // get token from the directory if any        
            line->nrequestin--;

            // Send the request to upper level
            if (!req->btransient)
            {
                int newlinetokenline = line->ntokenline - req->tokenacquired;

                req->tokenacquired += line->tokencount;
                req->bpriority      = req->bpriority || line->priority;

                line->tokencount = 0;
                line->ntokenline = newlinetokenline;
                line->priority = false;
            }

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

            if (line->ntokenline == 0 && line->nrequestout == 0)
            {
                // Evict line
                assert(line->ntokenrem >= 0);
                assert(line->nrequestout == 0);
                
                if (line->ntokenrem > 0 || line->nrequestin > 0)
                {
                    // need to check out and deal with it, REVISIT
                    assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                    m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                }

                line->valid = false;
            }

            // remote request is going out anyway
            m_lstReqB2a.push_back(req);
        }
    }
}

void DirectoryTOK::OnBELDisseminateTokenData(Message* req)
{
    // EV request will always terminate at directory
    assert(req->tokenacquired > 0);

    dir_line_t* line = LocateLine(req->address);

    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address);

    if (line == NULL)
    {
        // send the request to upper level
        assert(evictedhit);
        m_evictedlinebuffer.UpdateEvictedLine(req->address, false, req->gettokenpermanent(), true);
        m_lstReqB2a.push_back(req);
        return;
    }
    assert(!evictedhit);

    if (IsBelow(req->source))
    {
        // issue: the eviction request might go around the local network, in turn change the request sequence.
        // this might generate leave the root directory without sufficient space
        // solution: stack the request tokens on the directory line 
        // if there are request in or out, then go global, since stacking the token might lost data
        // if nothing in or out, and there are tokens locally, then stack them on the line
        // if all the tokens are stacked on the line, then request will be delievered to outside 
        // otherwise deleted
        // NOT IMPLEMENTED YET
        //
        // Possible issue: sacrifice locality. for example, if locally another line is request, while all the
        // tokens are evicted to outside. without evicted to the same level and informing the request line,
        // locality might be suffered. JXXX, REVISIT
        //
        // possibly, deal with rquest out differently as well, REVISIT JXXX
        if (line->nrequestin == 0 && line->nrequestout == 0 && (int)req->tokenacquired < line->ntokenline)
        {
            if (req->tokenrequested == 0)   // EV
            {
                // just stack, no ripping
                line->ntokenline -= req->tokenacquired;
                line->tokencount += req->tokenacquired;
                line->priority    = line->priority || req->bpriority;
                delete req;
                return;
            }

            // WB
            assert(req->tokenrequested == GetTotalTokenNum());
        }

        // send out
        // should always go global
        if (req->btransient && line->priority)
        {
            req->btransient = false;
            req->bpriority = true;
            line->priority = false;
        }

        int newlinetokenline = line->ntokenline - req->gettokenpermanent();

        req->tokenacquired += line->tokencount;
        req->bpriority      = req->bpriority || line->priority;

        line->tokencount    = 0;
        line->ntokenline    = newlinetokenline;
        line->priority      = false;
    }
    // Send the request to upper level
    else if (!req->btransient)
    {
        int newlinetokenline = line->ntokenline - req->gettokenpermanent();
        
        req->tokenacquired += line->tokencount;
        req->bpriority      = req->bpriority || line->priority;

        line->tokencount    = 0;
        line->ntokenline    = newlinetokenline;
        line->priority      = false;
    }
 
    m_lstReqB2a.push_back(req);

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

    if (line->ntokenline == 0 && line->nrequestout == 0)
    {
        // Evict line
        assert(line->ntokenrem >= 0);
        assert(line->nrequestout == 0);
        
        if (line->ntokenrem > 0 || line->nrequestin > 0)
        {
            // need to check out and deal with it, REVISIT
            m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
        }

        line->valid = false;
    }
}


void DirectoryTOK::OnBELDirNotification(Message* req)
{
    // We should have this line
    dir_line_t* line = LocateLine(req->address);
    assert(line != NULL);
    
    // Add the tokens
    line->ntokenline += req->tokenacquired;

    // Terminate the request
    delete req;
}

//////////////////////////////////////////////////////////////////////////
// ABOVE PROTOCOL TRANSACTION HANDLER

void DirectoryTOK::OnABOAcquireTokenData(Message* req)
{
    dir_line_t* line = LocateLine(req->address);

    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address);

    if (IsBelow(req->source))
    {
        assert (line != NULL);

        // Update the dir
        line->nrequestout--;

        // Send the request to local
        if (!req->btransient)
        {
            int newlinetokenline = line->ntokenline + req->tokenacquired;

            req->tokenacquired += line->tokencount;
            req->bpriority      = req->bpriority || line->priority;

            line->ntokenline    = newlinetokenline;
        }
        else
        {
            if (line->priority)
            {
                req->btransient = false;
                req->bpriority  = true;
                line->priority  = false;
            }

            int newlinetokenline  = line->ntokenline + (req->btransient ? 0 : req->tokenacquired);

            req->tokenacquired += line->tokencount;
            req->bpriority      = req->bpriority || line->priority;

            line->ntokenline    = newlinetokenline;
        }

        line->priority = false;
        line->tokencount = 0;

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

        if (line->nrequestin == 0 && line->nrequestout == 0 && line->tokencount == 0 && line->ntokenline  == 0 && line->ntokenrem == 0)
        {
            line->valid = false;
        }

        // always go local
        m_lstReqA2b.push_back(req);
    }
    // remote request
    // somehting inside lower level, just always get in
    else if (evictedhit)
    {
        // get in lower level, but update the evicted buffer
        m_evictedlinebuffer.UpdateEvictedLine(req->address, true, req->gettokenpermanent());

        // get in lower level
        m_lstReqA2b.push_back(req);
    }
    // as long as the line exist, the requet, no matter RS or RE, has to get in
    else if (line == NULL)
    {
        // This line is not below this directory; forward request onto upper ring
        m_lstReqA2a.push_back(req);
    }
    else
    {
        line->nrequestin++;

        if (req->btransient && line->priority)
        {
            line->priority = false;
            req->btransient = false;
            req->bpriority = true;
        }

        if (!req->btransient)
        {
            req->tokenacquired += line->tokencount;
            line->tokencount    = 0;
            line->ntokenrem    += req->tokenacquired;
        }

        req->bpriority = req->bpriority || line->priority;
        line->priority = false;

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

        if (line->nrequestin == 0 && line->nrequestout == 0 && line->tokencount == 0 && line->ntokenline == 0 && line->ntokenrem == 0)
        {
            line->valid = false;
        }

        // get in lower level
        m_lstReqA2b.push_back(req);
    }
}


void DirectoryTOK::OnABOAcquireToken(Message* req)
{
    dir_line_t* line = LocateLine(req->address);

    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address);

    if (IsBelow(req->source))
    {
        assert(line != NULL);

        // Update the dir
        line->nrequestout--;

        // Send the request to local
        if (req->btransient && line->priority)
        {
            req->btransient = false;
            req->bpriority = true;
            line->priority = false;
        }

        line->ntokenline   += (req->btransient ? 0 : req->tokenacquired);

        req->bpriority      = req->bpriority || line->priority;        
        req->tokenacquired += line->tokencount;

        line->priority = false;
        line->tokencount = 0;

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

        if (line->nrequestin == 0 && line->nrequestout == 0 && line->tokencount == 0 && line->ntokenline  == 0 && line->ntokenrem == 0)
        {
            line->valid = false;
        }

        // always go local
        m_lstReqA2b.push_back(req);
    }
    // remote request
    else if (evictedhit)
    {
        // Update the evicted buffer
        m_evictedlinebuffer.UpdateEvictedLine(req->address, true, req->gettokenpermanent());

        // Put request on lower ring
        m_lstReqA2b.push_back(req);
    }
    else if (line == NULL)
    {
        // The line does not exist below this directory; forward message on upper ring
        m_lstReqA2a.push_back(req);
    }
    else    // somehting inside lower level, just always get in
    {
        line->nrequestin++;

        if (req->btransient && line->priority)
        {
            line->priority = false;
            req->btransient = false;
            req->bpriority = true;
        }

        if (!req->btransient)
        {
            req->tokenacquired += line->tokencount;
            line->ntokenrem    += req->tokenacquired;
            line->tokencount    = 0;
        }
        
        req->bpriority = req->bpriority || line->priority;

        line->priority = false;

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

        if (line->nrequestin == 0 && line->nrequestout == 0 && line->tokencount == 0 && line->ntokenline  == 0 && line->ntokenrem == 0)
        {
            line->valid = false;
        }

        // Put message on lower ring
        m_lstReqA2b.push_back(req);
    }
}


void DirectoryTOK::OnABODisseminateTokenData(Message* req)
{
    // EV request will always terminate at directory
    assert(req->tokenacquired > 0);

    dir_line_t* line = LocateLine(req->address);

    // evicted line buffer
    unsigned int requestin = 0;
    unsigned int tokenrem;
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address, requestin, tokenrem);

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
            m_lstReqA2a.push_back(req);
        }
        else
        {
            // get in lower level, but update the evicted buffer
            m_evictedlinebuffer.UpdateEvictedLine(req->address, true, req->gettokenpermanent(), true);

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
        if (req->btransient && line->priority)
        {
            line->priority = false;
            req->btransient = false;
            req->bpriority = true;
        }

        if (!req->btransient)
        {
            req->tokenacquired += line->tokencount;
            line->tokencount    = 0;
            line->ntokenrem    += req->tokenacquired;
        }

        req->bpriority = req->bpriority || line->priority;
        line->priority = false;

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

        if (line->nrequestin == 0 && line->nrequestout == 0 && line->tokencount == 0 && line->ntokenline  == 0 && line->ntokenrem == 0)
        {
            line->valid = false;
        }
    }
    else
    {
        assert(line->ntokenline + line->ntokenrem > 0);
        // leave the tokens on the line. without getting in or send to the next node

        // notgoing anywhere, just terminate the request
        assert(req->tokenacquired < GetTotalTokenNum());
        line->tokencount += req->tokenacquired;
        line->priority = line->priority || req->bpriority;

        delete req;
    }
}

}
