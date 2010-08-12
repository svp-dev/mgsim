#ifndef _PROCESSORTOK_H
#define _PROCESSORTOK_H

#include "../simlink/linkmgs.h"
#include <queue>

namespace MemSim
{

class CacheL2TOK;
class ST_request;

class ProcessorTOK : public LinkMGS
{
    CacheL2TOK&             m_cache;        // The cache we're connected to
    std::queue<ST_request*> m_responses;    // Responses from cache

public:
	ProcessorTOK(CacheL2TOK& cache);
	~ProcessorTOK();

    bool CanSendFeedback() const
    {
        return true;
    }

    bool SendFeedback(ST_request* req)
    {
        m_responses.push(req);
        return true;
    }

    // issue a new reqeuest
    void PutRequest(ST_request* req);

    // check the request and give a reply is available
    ST_request* GetReply();

	// remove the replied request
	void RemoveReply();
};

}
#endif

