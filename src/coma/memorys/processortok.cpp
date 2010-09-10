#include "processortok.h"
#include "cachel2tok.h"
#include "predef.h"

namespace MemSim
{

// Issue a new request from the processor
void ProcessorTOK::PutRequest(ST_request* req)
{
    // Make sure the size matches
    assert(req->offset + req->nsize <= g_nCacheLineSize);
    assert(req->nsize <= ST_request::s_nRequestAlignedSize);

    req->Conform2BitVecFormat();
    
    m_cache.m_requests.push(req);
}

// check the request and give a reply is available
ST_request* ProcessorTOK::GetReply()
{
    if (m_responses.empty())
    {
        return NULL;
    }
	return m_responses.front();
}

void ProcessorTOK::RemoveReply()
{
    assert(!m_responses.empty());
    ST_request* req = m_responses.front();
    if (--req->refcount == 0)
    {
        delete req;
    }
    m_responses.pop();
    m_popped = true;
}

ProcessorTOK::ProcessorTOK(CacheL2TOK& cache)
  : m_cache(cache), m_pushed(false)
{
    m_cache.RegisterProcessor(*this);
}

ProcessorTOK::~ProcessorTOK()
{
    m_cache.UnregisterProcessor(*this);
}

}
