#ifndef _BUSST_SLAVE_IF_H
#define _BUSST_SLAVE_IF_H

#include "predef.h"
#include "busst_if.h"

namespace MemSim{
//{ memory simulator namespace

class BusST_Slave_if : public sc_interface
{

public:
    sc_fifo<ST_request*> channel_fifo_slave;	// as a slave
	sc_event m_eFeedbackEvent;
    BusST_if* m_pBusST;
protected:
	sc_fifo<ST_request*> *m_pfifoReqIn;

public:

	BusST_Slave_if()
	{
		m_pfifoReqIn = new sc_fifo<ST_request*>();
	}

	~BusST_Slave_if()
	{
		delete m_pfifoReqIn;
	}

	// Slave interface
	virtual bool request(ST_request *req)
	{
		return (m_pfifoReqIn->nb_write(req));
	}

	virtual __address_t StartAddress() const = 0;
	virtual __address_t EndAddress() const = 0;
};

//} memory simulator namespace
}
#endif

