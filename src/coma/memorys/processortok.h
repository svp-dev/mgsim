#ifndef _PROCESSORTOK_H
#define _PROCESSORTOK_H

#include "predef.h"
#include "busst_master.h"
#include "../simlink/linkmgs.h"
#include <list>

namespace MemSim
{

//////////////////////////////////////////////////////////////////////////
//
// the processor implementation is designed to link with MG simulator
// it will be connected directly with L2 cache instead of L1 cache
// the result is the processor will not check the data availability 
// in L2 cache. 
//
//////////////////////////////////////////////////////////////////////////

class ProcessorTOK : public sc_module, public BusST_Master, public LinkMGS
{
    unsigned int m_nPID;

	// link buffer as input request buffer	-- soly for simulation linking purpose
	//                                      -- no real connection with the reality
	list<ST_request*> m_lstReqLinkIn;
	list<ST_request*> m_lstReqLinkOut;

    // initiative states
    enum STATE {
        STATE_IDLE,
        STATE_RETRY_BUS,
    };

    STATE       m_nState;
    ST_request* m_pReqCur;
    ST_request* m_pReqLinkIni;
    ST_request* m_pReqLinkDone;

    // execute request issued from initiator
	void BehaviorIni();

    // handle request replied from slaves
    void BehaviorMem();

    // try to send the request
    void SendRequestBus();

public:
	SC_HAS_PROCESS(ProcessorTOK);

	ProcessorTOK(sc_module_name nm, sc_clock& clock, unsigned int pid)
      : sc_module(nm), m_nPID(pid),
        m_nState(STATE_IDLE),
        m_pReqCur(NULL),
		m_pReqLinkIni(NULL),
		m_pReqLinkDone(NULL)
	{
		SC_METHOD(BehaviorIni);
		sensitive << clock.posedge_event();
		dont_initialize();

		SC_METHOD(BehaviorMem);
		sensitive << clock.posedge_event();
		dont_initialize();
	}
	
    // issue a new reqeuest
    void PutRequest(uint64_t address, bool write, uint64_t size, void* data, unsigned long* ref);

    // check the request and give a reply is available
    unsigned long* GetReply(uint64_t &address, void* data, uint64_t &size, int &extcode);

	// remove the replied request
	bool RemoveReply();
};

}
#endif

