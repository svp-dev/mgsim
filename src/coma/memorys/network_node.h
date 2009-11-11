#ifndef _NETWORK_NODE_H
#define _NETWORK_NODE_H

#include "predef.h"

#include "network_if.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class Network_Node : public sc_module, public SimObj
{
public:
	// ports
	// clock
	sc_in<bool> port_clk;

	// port for input and output
	sc_fifo<ST_request*> m_fifoIn;




	//sc_fifo_out<ST_request*> m_fifoOut;




	sc_fifo<ST_request*> &m_fifoNetIn;
	sc_fifo_out<ST_request*> m_fifoNetOut;

	sc_in<bool> port_forward;
	sc_signal<bool> signal_forward;

	// port for ring communication
	//	sc_port<BusST_Master> port_bus;

	// sc_fifo<ST_request*> *m_pfifoFeedback;

private:
	Network_if * m_pCache;
	// sc_event m_eBusFeedback;

	//	__address_t m_nAddress;

    ST_request* m_pReqCurINI;
    ST_request* m_pReqCurPAS;

    enum STATE_INI{
        STATE_INI_AVAILABLE,
        STATE_INI_CONGEST
    };

    enum STATE_PAS{
        STATE_PAS_AVAILABLE,
        STATE_PAS_CONGEST_NET,  // net congest mask
        STATE_PAS_CONGEST_NODE  // node congest mask
    };
    
    STATE_INI m_nStateINI;
    UINT m_nStatePAS;

public:
	SC_HAS_PROCESS(Network_Node);
	Network_Node(sc_module_name nm, sc_fifo<ST_request*> &fifonetin, bool bBlock = true) : sc_module(nm), m_fifoNetIn(fifonetin)
	{
		// process for requests from CPU
		SC_METHOD(BehaviorIni);
		sensitive << port_clk.neg();
		dont_initialize();

//		SC_METHOD(BehaviorNet);
//		sensitive << port_clk.pos();
//		dont_initialize();

        // initialize parameter
        m_nStateINI = STATE_INI_AVAILABLE;
//        m_nStatePAS = STATE_PAS_AVAILABLE;
	}

	void BehaviorIni();
	void BehaviorNet();

	void SetCache(Network_if* pCache){m_pCache = pCache;};

private:
    void SendRequestINI();

    void SendRequestPASNET();
    void SendRequestPASNODE();
};

//////////////////////////////
//} memory simulator namespace
}

#endif
