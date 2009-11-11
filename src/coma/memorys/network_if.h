#ifndef _NETWORK_IF_H
#define _NETWORK_IF_H

#include "predef.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class Network_if : public sc_interface
{
public:
	sc_fifo<ST_request*> m_fifoinNetwork;
	sc_fifo_out<ST_request*> m_fifooutNetwork;
	sc_out<bool> port_net_forward;

protected:
	bool m_bBelow;	// avoid runtime judgment of the class type

public:
	// sc_event m_eFeedbackEvent;

	// Slave interface
	virtual bool RequestNetwork(ST_request *req)
	{
		return (m_fifooutNetwork->nb_write(req));
	}

	// decide whether the request can be directly forwarded to the next node

	virtual bool DirectForward(ST_request* req){return false;};
	virtual bool MayHandle(ST_request* req) = 0;

	bool IsBelowIF(){return m_bBelow;}

};

//////////////////////////////
//} memory simulator namespace
}

#endif
