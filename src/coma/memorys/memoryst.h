#ifndef _MEMORYST_H
#define _MEMORYST_H

#include "predef.h"
#include "busst_slave_if.h"
#include "../simlink/memorydatacontainer.h"

#include <map>

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////

class MemoryST : public sc_module, public MemoryState, public BusST_Slave_if, virtual public SimObj
{
public:

	sc_in<bool> port_clk;
	sc_fifo<ST_request*> channel_fifo_slave;

private:
	UINT    m_nDelay;

	__address_t m_nStartAddress;
	__address_t m_nEndAddress;

	//sc_fifo<ST_request*> *m_pfifoReqIn;

    ST_request* m_pReqCur;

	enum STATE{
        STATE_PROCESSING,
        STATE_RETRY_BUS
    };

    STATE m_nState;

//    UINT  m_nWaitCount;
    pipeline_t*  m_pPipeLine;

#ifdef SIMULATE_DATA_TRANSACTION
    MemoryDataContainer *m_pMemoryDataContainer;
#endif

#ifdef MEM_MODULE_STATISTICS
    //////////////////////////////////////////////////////////////////////////
    // statistics on different components
    ST_request* m_pStatCurReq;                      // backup the current request for statistics
    ST_request* m_pStatReqStore;
    unsigned int m_nStatReqNo;
    map<double, stat_stru_size_t>              *m_pmmStatRequestNo;
    map<double, stat_stru_request_t>           *m_pmmStatProcessing;
    map<double, stat_stru_request_t>           *m_pmmStatReqStore;
    map<double, stat_stru_request_list_t>      *m_pmmStatPipeline;
    map<double, stat_stru_size_t>              *m_pmmStatIncomingFIFO;
#endif

public:
	SC_HAS_PROCESS(MemoryST);

    MemoryST(sc_module_name nm
#ifdef SIMULATE_DATA_TRANSACTION
        , MemoryDataContainer *pMemoryDataContainer
#endif
        , __address_t startaddr=0, __address_t endaddr=MEMORY_SIZE, UINT delay = 30)
        : SimObj(nm), sc_module(nm), m_nDelay(delay), m_nStartAddress(startaddr), m_nEndAddress(endaddr)
#ifdef SIMULATE_DATA_TRANSACTION
        , m_pMemoryDataContainer(pMemoryDataContainer)
#endif
    {
        SC_METHOD(Behavior);
        sensitive << port_clk.pos();
        dont_initialize();

        // size should be defined in creator
        //m_pfifoReqIn = new sc_fifo<ST_request*>();

        // initialize parameters
        //m_nWaitCount = 0;
        m_nState = STATE_PROCESSING;
        m_pReqCur = NULL;

        // the pipe line stages is set the same as the delay
        m_pPipeLine = new pipeline_t(m_nDelay-1);

		LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
		    clog << LOG_HEAD_OUTPUT << dec << "with delay of " << m_nDelay << endl;
		LOG_VERBOSE_END

#ifdef MEM_MODULE_STATISTICS
        // statistics
        m_pStatCurReq = NULL;
        m_pStatReqStore = NULL;
        m_nStatReqNo = 0;
        m_pmmStatRequestNo = NULL;
        m_pmmStatProcessing = NULL;
        m_pmmStatReqStore = NULL;
        m_pmmStatPipeline = NULL;
        m_pmmStatIncomingFIFO = NULL;
#endif
    }

    ~MemoryST(){
        delete m_pPipeLine;
    }

private:
    void Behavior();

    // read request handler
	void FunRead(ST_request* req);
    // write request handler
	void FunWrite(ST_request* req);
    // Process the memory request
    void ProcessRequest();
    // resend request again 
    void ResendRequest();

public:
    // inherited from BusST_Slave_if
    // request function will put the request in to request buffer
	//bool request(ST_request *req);

    // return start address that can be handled in this memory module
	__address_t StartAddress() const;
    // return end address that can be handled in this memory module
	__address_t EndAddress() const;

#ifdef MEM_MODULE_STATISTICS
    virtual void InitializeStatistics(unsigned int components);
    virtual void Statistics(STAT_LEVEL lev);
    virtual void DumpStatistics(ofstream& outfile, unsigned int component, unsigned int type);
#endif
};

//const __address_t MemoryST::MEMORY_SIZE = 0x1000;

//////////////////////////////
//} memory simulator namespace
}

#endif

