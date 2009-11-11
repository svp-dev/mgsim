#ifndef _BUSST_MASTER_H
#define _BUSST_MASTER_H

#include "predef.h"

#include "busst_if.h"
#include "simcontrol.h"

namespace MemSim{
//{ memory simulator namespace

class BusST_Master : virtual public SimObj
{
public:
	// ports
	sc_port<BusST_if> port_bus;

	sc_fifo<ST_request*> *m_pfifoFeedback;

private:

	sc_event m_eBusFeedback;

public:
    BusST_Master()
    {
        SetBusMaster(this);
    }

	sc_event* GetBusFeedbackEvent()
	{
		return &m_eBusFeedback;
	}

	bool GetFeedback(ST_request* req)
	{
        assert(m_pfifoFeedback != NULL);
		return (m_pfifoFeedback->nb_write(req));
	}

	void NotifyBusFeedbackEvent()
	{
		m_eBusFeedback.notify();
LOG_VERBOSE_BEGIN(VERBOSE_ALL)
		clog << "bus notified \n";
LOG_VERBOSE_END
	}

};

//} memory simulator namespace
}
#endif

