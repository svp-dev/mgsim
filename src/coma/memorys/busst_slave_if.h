#ifndef _BUSST_SLAVE_IF_H
#define _BUSST_SLAVE_IF_H

#include "predef.h"
#include "busst_if.h"

namespace MemSim
{

class BusST_Slave_if : public sc_interface
{
protected:
	sc_fifo<ST_request*> m_pfifoReqIn;

public:
    sc_fifo<ST_request*> channel_fifo_slave;	// as a slave

	// Slave interface
	bool request(ST_request *req)
	{
		return m_pfifoReqIn.nb_write(req);
	}
};

}
#endif

