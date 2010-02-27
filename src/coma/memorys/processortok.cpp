#include "processortok.h"
using namespace MemSim;

namespace MemSim{
double g_fLatency = 0.0;
unsigned int g_uTotalL = 0;
unsigned int g_uTotalS = 0;
}

// issue a new reqeuest
void ProcessorTOK::PutRequest(uint64_t address, bool write, uint64_t size, void* data, unsigned long* ref)
{
    ST_request *preq = new ST_request();
    preq->pid        = m_nPID;
    preq->addresspre = address / g_nCacheLineSize;
    preq->offset     = address % g_nCacheLineSize;
    preq->nsize      = size;
    preq->Conform2BitVecFormat();
    preq->ref        = ref;
    preq->type       = write ? MemoryState::REQUEST_WRITE : MemoryState::REQUEST_READ;
    ADD_INITIATOR(preq, this);

    if (write)
        g_uTotalS++;
    else
        g_uTotalL++;

    // make sure the size matches
    assert(preq->offset + size <= g_nCacheLineSize);
    assert(size <= ST_request::s_nRequestAlignedSize);

    if (write)
    {
        memcpy(&preq->data[preq->offset], data, size);
    }

    if (m_eRequestIni == LF_AVAILABLE)
    {
        // insert the newly added request to the link request buffer
        m_lstReqLinkIn.push_back(preq);
    }
    else
    {
        // update linking register and flag
        m_eRequestIni = LF_AVAILABLE;
        m_pReqLinkIni = preq;
    }
}

// check the request and give a reply is available
// extcode: extension code for reply
//  0 : normal read/write reply request
//  1 : backward broadcast invalidation request
//  2 : update request (write reply from different processors sharing the same l2 cache)
unsigned long* ProcessorTOK::GetReply(uint64_t &address, void* data, uint64_t &size, int &extcode)
{
	if (m_pReqLinkDone == NULL)
	{
    	if (m_lstReqLinkOut.empty())
    	{
            return NULL;
        }
		m_pReqLinkDone = m_lstReqLinkOut.front();
		m_lstReqLinkOut.pop_front();
	}

    if (m_pReqLinkDone->type == MemoryState::REQUEST_INVALIDATE_BR)
    {
        extcode = 1;        // IB
        address = m_pReqLinkDone->getlineaddress(); // line address
        return NULL;
    }
    
    if (m_pReqLinkDone->type == MemoryState::REQUEST_READ_REPLY)
    {
        extcode = 0;        // normal reply
        address = m_pReqLinkDone->getlineaddress();
        memcpy(data, &m_pReqLinkDone->data[m_pReqLinkDone->offset], m_pReqLinkDone->nsize);
        return m_pReqLinkDone->ref;
    }
    
    assert(m_pReqLinkDone->type == MemoryState::REQUEST_WRITE_REPLY);

    address = m_pReqLinkDone->getlineaddress();
    memcpy(data, &m_pReqLinkDone->data[m_pReqLinkDone->offset], m_pReqLinkDone->nsize);
        
    if (m_pReqLinkDone->pid == m_nPID)
    {
        extcode = 0;        // normal reply
        address = m_pReqLinkDone->getlineaddress();
        return m_pReqLinkDone->ref;
    }
        
    extcode = 1;    // IB
    size = 0;
    return NULL;
}

bool ProcessorTOK::RemoveReply()
{
    assert(m_pReqLinkDone != NULL);

    // clean up the line-done request
    // if broadcasted
    if ((m_pReqLinkDone->type == MemoryState::REQUEST_INVALIDATE_BR)||( (m_pReqLinkDone->type == MemoryState::REQUEST_WRITE_REPLY)&&(m_pReqLinkDone->bbackinv) ))
    {
        m_pReqLinkDone->refcount--;

        if (m_pReqLinkDone->refcount == 0)
        {
            delete m_pReqLinkDone;
        }
    }
    else
    {
        delete m_pReqLinkDone;
    }

    // update the link-done request
	if (!m_lstReqLinkOut.empty())
	{
		m_pReqLinkDone = m_lstReqLinkOut.front();
		m_lstReqLinkOut.pop_front();
	}
	else
	{
		m_pReqLinkDone = NULL;
	}

	return true;
}

void ProcessorTOK::SendRequestBus()
{
    if (!port_bus->request(m_pReqCur))
    {
        m_nState = STATE_RETRY_BUS;
        return;
    }

    m_nState = STATE_IDLE;
    m_eRequestIni = LF_NONE;
    m_pReqCur->starttime = sc_time_stamp().to_seconds();

	// begin: deal with possible suspended request in lstReqLink
    if (!m_lstReqLinkIn.empty())
    {
        m_eRequestIni = LF_AVAILABLE;
        m_pReqLinkIni = m_lstReqLinkIn.front();
        m_lstReqLinkIn.pop_front();
    }

	// end  : deal with possible suspended request in lstReqLink
}

void ProcessorTOK::BehaviorIni()
{
    switch (m_nState)
    {
        case STATE_IDLE:
            if (m_eRequestIni != LF_AVAILABLE)
            {
                return;
            }

            // Save the request 
            m_pReqCur = m_pReqLinkIni;

            // Try to send the request to bus
            SendRequestBus();
            break;

        // retry request since bus was busy
        case STATE_RETRY_BUS:
            SendRequestBus();
            break;

        default:
            break;
    }
}

void ProcessorTOK::BehaviorMem()
{
    // check whether there are any replies from the memory hierarchy
    if (m_pfifoFeedback.num_available_fast() <= 0)
    {
        return;
    }

    // get feedback
    ST_request* req = 0;
    if (!m_pfifoFeedback.nb_read(req))
    {
        abort();
        return;
    }

    if (req->type == MemoryState::REQUEST_INVALIDATE_BR ||
       (req->type == MemoryState::REQUEST_WRITE_REPLY && req->bbackinv && req->pid != m_nPID))
    {
		if (m_pReqLinkDone != NULL)
        {
			m_lstReqLinkOut.push_back(req);
        }
		else
        {
	        m_pReqLinkDone = req;   // req is on heap
        }

        // The request can be invalidation as well
        return;
    }

    if (req->type == MemoryState::REQUEST_READ_REPLY || req->type == MemoryState::REQUEST_WRITE_REPLY)
    {
        // pop the initiator if the WR is actually broadcasted, instead of directly sent
        if (req->type == MemoryState::REQUEST_WRITE_REPLY && req->bbackinv && req->pid == m_nPID)
        {
            pop_initiator(req);
        }

        // *** the request can be invalidation as well

        // return requests to simulator
		if (m_pReqLinkDone != NULL)
        {
			m_lstReqLinkOut.push_back(req);
        }
		else
        {
	        m_pReqLinkDone = req;   // req is on heap
        }
    }
}
