#ifndef _PROCESSORTOK_H
#define _PROCESSORTOK_H

#include "../simlink/linkmgs.h"
#include "../../storage.h"
#include <queue>

namespace MemSim
{

class CacheL2TOK;
class ST_request;

class ProcessorTOK : public LinkMGS
{
    CacheL2TOK&             m_cache;        // The cache we're connected to
    std::queue<ST_request*> m_responses;    // Responses from cache
    bool                    m_pushed;
    bool                    m_popped;

public:
    Simulator::SingleFlag*  m_active;

	ProcessorTOK(CacheL2TOK& cache);
	~ProcessorTOK();
	
	void OnCycleStart()
	{
	    m_pushed = false;
	}
	
	void OnCycleEnd()
	{
	    if (m_responses.empty() && m_popped && !m_pushed)
	    {
	        m_active->Clear();
	    }
	    m_popped = false;
	}

    bool CanSendFeedback() const
    {
        return true;
    }

    bool SendFeedback(ST_request* req)
    {
        m_responses.push(req);
        if (!m_pushed)
        {
            m_active->Set();
            m_pushed = true;
        }
        return true;
    }

    // issue a new reqeuest
    void PutRequest(ST_request* req);
    
    size_t NumReplies() const
    {
        return m_responses.size();
    }

    // check the request and give a reply is available
    ST_request* GetReply();

	// remove the replied request
	void RemoveReply();
};

}
#endif

