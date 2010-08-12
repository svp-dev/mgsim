#ifndef _NETWORK_NODE_H
#define _NETWORK_NODE_H

#include "predef.h"
#include <queue>

namespace MemSim
{

class Network_Node
{
	std::queue<ST_request*> m_fifoinNetwork;   // Input buffer from previous node
	std::queue<ST_request*> m_fifooutNetwork;  // Output buffer for next node
	Network_Node*           m_next;
	
public:
	void BehaviorNode()
	{
	    // Send outgoing requests to next node
	    if (!m_fifooutNetwork.empty())
	    {
            m_next->m_fifoinNetwork.push(m_fifooutNetwork.front());
	        m_fifooutNetwork.pop();
        }
	}
	
	void SendRequest(ST_request* req)
	{
	    m_fifooutNetwork.push(req);
	}
	
	ST_request* ReceiveRequest()
	{
	    if (m_fifoinNetwork.empty())
	    {
	        return NULL;
	    }
	    ST_request* req = m_fifoinNetwork.front();
	    m_fifoinNetwork.pop();
	    return req;
	}
	
	Network_Node()//sc_module_name nm, sc_clock& clock)
	  : /*sc_module(nm),*/ m_next(NULL)
	{
		//SC_METHOD(NetworkNode::BehaviorIni);
		//sensitive << clock.negedge_event();
		//dont_initialize();
	}

	void SetNext(Network_Node* next)
	{
	    assert(m_next == NULL);
	    m_next = next;
	}	
};

}
#endif
