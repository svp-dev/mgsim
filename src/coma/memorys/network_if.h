#ifndef _NETWORK_IF_H
#define _NETWORK_IF_H

#include "predef.h"

namespace MemSim
{

class Network_if : public sc_interface
{
    sc_fifo<ST_request*>     m_fifoinNetwork;
    sc_fifo_out<ST_request*> m_fifooutNetwork;

public:
    virtual bool RequestNetwork(ST_request *req)
    {
        return (m_fifooutNetwork->nb_write(req));
    }

    virtual sc_fifo<ST_request*>& GetNetworkFifo()
    {
        return m_fifoinNetwork;
    }

    virtual sc_fifo_out<ST_request*>& GetNetworkFifoOut()
    {
        return m_fifooutNetwork;
    }
};

}
#endif
