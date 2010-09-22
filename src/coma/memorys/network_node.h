#ifndef _NETWORK_NODE_H
#define _NETWORK_NODE_H

#include "predef.h"
#include <queue>

namespace MemSim
{

class Network_Node
{
	std::queue<Message*> m_fifoinNetwork;   // Input buffer from previous node
	std::queue<Message*> m_fifooutNetwork;  // Output buffer for next node
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
	
	bool SendRequest(Message* req)
	{
	    m_fifooutNetwork.push(req);
	    return true;
	}
	
	Message* ReceiveRequest()
	{
	    if (m_fifoinNetwork.empty())
	    {
	        return NULL;
	    }
	    Message* req = m_fifoinNetwork.front();
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
