#ifndef _PROCESSORTOK_H
#define _PROCESSORTOK_H

#include "../simlink/linkmgs.h"
#include "../../storage.h"
#include <queue>

namespace MemSim
{

class CacheL2TOK;
class Message;

class ProcessorTOK : public LinkMGS
{
    CacheL2TOK&             m_cache;        // The cache we're connected to
    std::queue<Message*> m_responses;    // Responses from cache
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

    bool SendFeedback(Message* req)
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
    void PutRequest(Message* req);
    
    size_t NumReplies() const
    {
        return m_responses.size();
    }

    // check the request and give a reply is available
    Message* GetReply();

	// remove the replied request
	void RemoveReply();
};

}
#endif

