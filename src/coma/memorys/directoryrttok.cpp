#include "directoryrttok.h"
using namespace std;

namespace MemSim
{

void DirectoryRTTOK::BehaviorNET()
{
    // Possibly set by ProcessRequestNET
    m_pReqCurNET2Bus = NULL;
    m_pReqCurNET2Net = NULL;

    // Fetch request from incoming buffer
    Message* req_incoming = ReceiveRequest();
    if (req_incoming != NULL)
    {
        ProcessRequestNET(req_incoming);
    }
    
    // Make sure about the sequence:
    // process the requests from the incoming buffer first
    // then process the previously queued requests.
    if (m_pReqCurNET2Net != NULL)
    {
        if (SendRequest(m_pReqCurNET2Net))
        {
            m_pReqCurNET2Net = NULL;
        }
    }
    else if (!m_activelines.empty())
    {
        // Send a queued request
        dir_line_t* line = m_activelines.front();
        assert(!line->requests.empty());

        if (SendRequest( line->requests.front() ))
        {
            // pop deferred request from the queue
            m_activelines.front()->requests.pop();
            if (m_activelines.front()->requests.empty())
            {
                // Line has no more requests, pop it
                m_activelines.pop();
            }
        }
    }
    
    if (m_pReqCurNET2Bus != NULL)
    {
        if (SendRequestNETtoBUS(m_pReqCurNET2Bus))
        {
            m_pReqCurNET2Bus = NULL;
        }
    }
}

// CHKS: optimization can be added, add the location or at least part of 
// the location (if the rest can be resolved from the request address)
// then the line can be appended without going through the pipeline stages
void DirectoryRTTOK::BehaviorBUS()
{
    if (m_pfifoFeedback.empty())
    {
        return;
    }
    
    // Get the next request from memory
    Message* req = m_pfifoFeedback.front();
    m_pfifoFeedback.pop();

    // Locate the line
    dir_line_t* line = LocateLine(req->address);
    assert(line != NULL);

    // Activate the line    
    m_activelines.push(line);
        
    // Send the request
    SendRequest(req);
}

// send net request to memory
bool DirectoryRTTOK::SendRequestNETtoBUS(Message* req)
{
    m_pfifoMemory.push(req);
    return true;
}

DirectoryRTTOK::dir_line_t* DirectoryRTTOK::LocateLine(MemAddr address)
{
    const unsigned int index = DirIndex(address);
    const uint64       tag   = DirTag(address);

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

// replace only invalid lines otherwise NULL
DirectoryRTTOK::dir_line_t* DirectoryRTTOK::GetEmptyLine(MemAddr address)
{
    unsigned int index = DirIndex(address);

    dir_line_t* set = &(m_pSet[index].lines[0]);
    for (unsigned int i = 0; i < m_nAssociativity; i++)
    {
    	if (!set[i].valid)
    	{
    		return &set[i];
        }
    }

    return NULL;
}

unsigned int DirectoryRTTOK::DirIndex(MemAddr address)
{
    return (address / m_nLineSize) % m_nSet;
}

uint64 DirectoryRTTOK::DirTag(MemAddr address)
{
    return (address / m_nLineSize) / m_nSet;
}

void DirectoryRTTOK::ProcessRequestNET(Message* req)
{
    // Find the line for the request
    dir_line_t* line = LocateLine(req->address);
    
    switch (req->type)
    {
    case Message::ACQUIRE_TOKEN_DATA:
        // Request for tokens and data
        if (line == NULL)
        {
            // Need to fetch a line off-chip
            line = GetEmptyLine(req->address);
            assert(line != NULL);

            // Initialize line
            line->tag      = DirTag(req->address);
            line->valid    = true;
            line->tokens   = 0;
            line->priority = false;
            line->reserved = true;
            
            // JOYING revisit, this is to resolve the reload bug
            req->bprocessed = false;
            m_pReqCurNET2Bus = req;
        }
        else
        {
            // Line can be found in the group, just pass the request.
        
            // Transfer any tokens that we have to the request only when
            // the request is not a request with transient tokens.
            if (!req->btransient)
            {
                assert(req->tokenacquired + line->tokens <= GetTotalTokenNum());
            
                req->tokenacquired += line->tokens;
                req->bpriority      = req->bpriority || line->priority;
                line->tokens        = 0;
                line->priority      = false;
            }

            // REVISIT, will this cause too much additional traffic?
            if (!line->reserved && !req->dataavailable && (req->gettokenpermanent() == GetTotalTokenNum() || req->bprocessed))
            {
                // Send request off-chip
                line->reserved = true;
                m_pReqCurNET2Bus = req;
            }
            else if (line->reserved)
            {
                // Append request to line
                line->requests.push(req);
            }
            else
            {
                // Forward request on network
                m_pReqCurNET2Net = req;
            }
        }
        break;

    case Message::ACQUIRE_TOKEN:
        // Request for tokens. We should have the line.
        assert(line != NULL);
    
        // Transfer any tokens that we have to the request only when
        // the request is not a request with transient tokens.
        if (!req->btransient)
        {
            assert(req->tokenacquired + line->tokens <= GetTotalTokenNum());
        
            req->tokenacquired += line->tokens;
            req->bpriority      = req->bpriority || line->priority;
            line->tokens        = 0;
            line->priority      = false;
        }

        line->tokens = 0;

        if (line->reserved)
        {
            // Append request to line
            line->requests.push(req);
        }
        else
        {
            // Forward request on network
            m_pReqCurNET2Net = req;
        }
        break;

    case Message::DISSEMINATE_TOKEN_DATA:
        // Eviction. We should have the line
        assert(line != NULL);
    
        assert(req->tokenacquired > 0);

        // Evictions always terminate at the root directory.
        if (line->reserved)
        {
            line->requests.push(req);
        }
        else
        {
            // Add the tokens in the eviction to the line
            line->tokens  += req->tokenacquired;
            line->priority = line->priority || req->bpriority;

            if (line->tokens == GetTotalTokenNum())
            {
                // We have all the tokens now; clear the line
                line->valid = false;
            }

            if (req->tokenrequested == 0)
            {
                // Non-dirty data; we don't have to write back
                assert(req->btransient == false);
                delete req;
            }
            else 
            {
                // Dirty data; write back the data to memory
                assert(req->tokenrequested == GetTotalTokenNum());
                m_pReqCurNET2Bus = req;
            }
        }
        break;
        
    default:
        assert(false);
        break;
    }
}

}
