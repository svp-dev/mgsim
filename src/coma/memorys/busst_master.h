#ifndef _BUSST_MASTER_H
#define _BUSST_MASTER_H

#include "predef.h"

#include "busst_if.h"
#include "simcontrol.h"

namespace MemSim
{

class BusST_Master : virtual public SimObj
{
public:
	sc_port<BusST_if>    port_bus;
	sc_fifo<ST_request*> m_pfifoFeedback;

    BusST_Master()
    {
        SetBusMaster(this);
    }

	bool GetFeedback(ST_request* req)
	{
		return (m_pfifoFeedback.nb_write(req));
	}
};

}
#endif

