#ifndef _NETWORK_H
#define _NETWORK_H

#include "predef.h"
#include "network_node.h"

namespace MemSim
{

class Network
{
    sc_clock& m_clock;
    
protected:
    std::vector<Network_Node*> m_vecLpNode;

public:
    Network(sc_clock& clock) : m_clock(clock)
    {
    }

    void ConnectNetwork()
    {
        for (size_t i = 1; i < m_vecLpNode.size(); ++i)
        {
            m_vecLpNode[i - 1]->m_fifoNetOut(m_vecLpNode[i]->m_fifoNetIn);
        }
        m_vecLpNode.back()->m_fifoNetOut(m_vecLpNode[0]->m_fifoNetIn);
    }

    void operator () (Network_if& interface_)
    {
        // bind interface with network port
        char sname[100];
        sprintf(sname, "%p_node%lu", this, (unsigned long)m_vecLpNode.size());
        m_vecLpNode.push_back(
            new Network_Node(sname, m_clock, interface_.GetNetworkFifo(), interface_.GetNetworkFifoOut())
        );
    }
};

}
#endif
