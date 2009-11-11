#ifndef _PROCESSORTOK_H
#define _PROCESSORTOK_H

#include "predef.h"
#include "busst_master.h"

#include "memoryst.h"

#include "../simlink/linkmgs.h"

#include "../simlink/memorydatacontainer.h"
#include <list>

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////

//#define LIMITTED_REQUEST_TESTING

//////////////////////////////////////////////////////////////////////////
//
// the processor implementation is designed to link with MG simulator
// it will be connected directly with L2 cache instead of L1 cache
// the result is the processor will not check the data availability 
// in L2 cache. 
//
//////////////////////////////////////////////////////////////////////////

class ProcessorTOK : public sc_module, public BusST_Master, public LinkMGS, virtual public SimObj
{
public:
	enum INSTRUCTION{
		INSTRUCTION_NON_MEMORY,
		INSTRUCTION_READ,
		INSTRUCTION_WRITE
	};

	sc_in<bool> port_clk;

private:
    unsigned int m_nPID;

	bool m_bSplitTransaction;
	sc_event m_eWaitingForMemory;
	sc_event m_eContinueProcessing;

	// link buffer as input request buffer	-- soly for simulation linking purpose
	//                                      -- no real connection with the reality
	list<ST_request*> m_lstReqLinkIn;
	list<ST_request*> m_lstReqLinkOut;

    // initiative states
    enum STATE{
        STATE_IDLE,
        STATE_RETRY_BUS,
#if defined(LIMITTED_REQUEST_TESTING) || ( defined(SIMULATE_DRIVEN_MODE) && (SIMULATE_DRIVEN_MODE == CONST_DRIVEN_MODE_TRACE) )
        STATE_HALT      // TESTING PURPOSE
#endif

    };

    STATE m_nState;

    ST_request* m_pReqCur;

#ifdef LIMITTED_REQUEST_TESTING
    //////////////////////////////////////////////////////////////////////////
    // testing
    UINT m_nMemReqTotal;
    UINT m_nMemReq;
#endif

#ifdef SIMULATE_DATA_TRANSACTION
    MemoryDataContainer *m_pMemoryDataContainer;
#endif

#ifdef MEM_MODULE_STATISTICS
    //////////////////////////////////////////////////////////////////////////
    // statistics on different components
    double m_dStatCurReplyLatency;              // save reply latency
    stat_stru_req_sent_t m_tStatReqSent;     // the requests sent
    ST_request*  m_pStatCurReplyReq;            // save reply request

    map<double, stat_stru_req_latency_t>             *m_pmmStatLatency;
    map<double, stat_stru_req_sent_t>                *m_pmmStatReqSent;

    vector<__address_t> m_vecTEST;
#endif


public:
	SC_HAS_PROCESS(ProcessorTOK);

	ProcessorTOK(sc_module_name nm, unsigned int pid
#ifdef SIMULATE_DATA_TRANSACTION
        , MemoryDataContainer *pMemoryDataContainer
#endif
#ifdef LIMITTED_REQUEST_TESTING
        , UINT nMemReqTotal = 10 
#endif
        , bool bSplit = false) 
		: SimObj(nm), sc_module(nm), m_nPID(pid), m_bSplitTransaction(bSplit)
#ifdef SIMULATE_DATA_TRANSACTION
        , m_pMemoryDataContainer(pMemoryDataContainer)
#endif

#ifdef LIMITTED_REQUEST_TESTING
        , m_nMemReqTotal(nMemReqTotal), m_nMemReq(0)
#endif
	{
		SC_METHOD(BehaviorIni);
		sensitive << port_clk.pos();
		dont_initialize();

		SC_METHOD(BehaviorMem);
		sensitive << port_clk.pos();
		dont_initialize();

		// check percentage

		// default value set

        //////////////////////////////////////////////////////////////////////////
        // initialize parameters
        m_pReqCur = NULL;
        // initialize state
        m_nState = STATE_IDLE;

		m_pfifoFeedback = new sc_fifo<ST_request*>();

		m_pReqLinkDone = NULL;
		m_pReqLinkIni = NULL;

#ifdef MEM_MODULE_STATISTICS
        // statistics
        m_dStatCurReplyLatency = 0;
        m_tStatReqSent.read = 0;
        m_tStatReqSent.write = 0;

        m_pmmStatLatency = NULL;
        m_pmmStatReqSent = NULL;
#endif
	}
	~ProcessorTOK(){
        delete m_pfifoFeedback;
    }

    unsigned int GetProcessorID(){return m_nPID;};

private:

    // execute request issued from initiator
	void BehaviorIni();

    // handle request replied from slaves
    void BehaviorMem();

    // non memory instruction handling
	void FunNonMemory();

    // execute some instructions
    void Execute();

    // try to send the request
    void SendRequestBus();

public:
    // issue a new reqeuest
    void PutRequest(uint64_t address, bool write, uint64_t size, void* data, unsigned long* ref);

    // check the request and give a reply is available
     unsigned long* GetReply(uint64_t &address, void* data, uint64_t &size, int &extcode);

	// remove the replied request
	 bool RemoveReply();

#ifdef MEM_MODULE_STATISTICS
    //////////////////////////////////////////////////////////////////
    // statistics
    void InitializeStatistics(unsigned int components);

    void Statistics(STAT_LEVEL lev);

    void DumpStatistics(ofstream& outfile, unsigned int component, unsigned int type);
#endif

private:
    ST_request* m_pReqLinkIni;
    ST_request* m_pReqLinkDone;
};

//////////////////////////////
//} memory simulator namespace
}

#endif  // _PROCESSORSTNB_H

