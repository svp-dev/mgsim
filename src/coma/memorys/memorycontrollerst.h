#ifndef _MEMORYCONTROLLERST_H
#define _MEMORYCONTROLLERST_H

#include "predef.h"

#include "memorybankst.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////

class MemoryControllerST : public sc_module, public MemoryState, public BusST_Slave_if, virtual public SimObj
{
public:
	sc_in<bool> port_clk;
	sc_fifo<ST_request*> channel_fifo_slave;

	// try bind ports to multiple memorybanks.
	// ***

protected:

	// maybe latency
	// sc_time m_tLatency;

	__address_t m_nStartAddress;
	__address_t m_nEndAddress;

	unsigned int m_nBanks;

	//sc_fifo<ST_request*> *m_pfifoReqIn;
	sc_fifo<ST_request*> *m_pfifoReplyIn;

	unsigned int (*m_pMappingFunc)(MemoryControllerST* pthis, __address_t address);

	MemoryBankST** m_ppBanks;


#ifdef SIMULATE_DATA_TRANSACTION
    MemoryDataContainer *m_pMemoryDataContainer;
#endif

public:
	SC_HAS_PROCESS(MemoryControllerST);

    MemoryControllerST(sc_module_name nm, unsigned int nBanks
#ifdef SIMULATE_DATA_TRANSACTION
        , MemoryDataContainer *pMemoryDataContainer
#endif
        , __address_t startaddr=0, __address_t endaddr=MEMORY_SIZE)
        : sc_module(nm), m_nBanks(nBanks), m_nStartAddress(startaddr), m_nEndAddress(endaddr), SimObj(nm)
#ifdef SIMULATE_DATA_TRANSACTION
        , m_pMemoryDataContainer(pMemoryDataContainer)
#endif
    {
        SC_THREAD(Behavior_MAS);
        sensitive << port_clk.pos();
        dont_initialize();

        SC_THREAD(Behavior_REP);
        sensitive << port_clk.pos();
        dont_initialize();

        // size should be defined in creator
        //m_pfifoReqIn = new sc_fifo<ST_request*>();
        m_pfifoReplyIn = new sc_fifo<ST_request*>();

        // initialize memory banks
        Initialize();

        m_pMappingFunc = NULL;

        // initialized verbose with true
    }

	// add destructor
	// ***

	void Initialize()
	{
		// make new name
		size_t len = strlen(name());
		char *pName = (char*)malloc(len+10);
		
		
		m_ppBanks = (MemoryBankST**)malloc(sizeof(MemoryBankST*)*m_nBanks);
		for (unsigned int i=0;i<m_nBanks;i++)
		{
			// assume bank number less than 100
			sprintf(pName, "%s_Bank%d", name(), i);
#ifndef SIMULATE_DATA_TRANSACTION
			m_ppBanks[i] = new MemoryBankST(pName, m_pfifoReplyIn);
#else
		    m_ppBanks[i] = new MemoryBankST(pName, m_pfifoReplyIn, m_pMemoryDataContainer);
#endif
			m_ppBanks[i]->port_clk(port_clk);
		}

		// free name
		free(pName);
	}

	void Behavior_MAS()
	{
		while(true)
		{
			if (m_pfifoReqIn->num_available_fast() <= 0)
			{
				wait(port_clk.posedge_event());
				continue;
			}

			//wait(SC_ZERO_TIME);
			//wait(m_pfifoReqIn->data_written_event());
			ST_request *req = m_pfifoReqIn->read();

LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
			clog << LOG_HEAD_OUTPUT << "MAS REQ in " << FMT_ADDR(req->address) << endl;
			clog << "\t"; print_request(req);
LOG_VERBOSE_END

			// delay 
			// ***

			// dispatch request
			DispatchRequest(req);

			wait(port_clk.posedge_event());
		}

	}

	void Behavior_REP()
	{
		while(true)
		{
			if (m_pfifoReplyIn->num_available_fast() <= 0)
			{
				wait(port_clk.posedge_event());
				continue;
			}

			//wait(SC_ZERO_TIME);
			//wait(m_pfifoReqIn->data_written_event());
			ST_request *req = m_pfifoReplyIn->read();

LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
			clog << LOG_HEAD_OUTPUT << "REP REQ in " << FMT_ADDR(req->address) << endl;
			clog << "\t"; print_request(req);
LOG_VERBOSE_END

			// delay 
			// ***

			channel_fifo_slave.write(req);
LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
			clog << LOG_HEAD_OUTPUT << "feedback transaction sent" << endl;
LOG_VERBOSE_END

			wait(port_clk.posedge_event());
		}

	}


	virtual MemoryBankST* GetBank(__address_t address)
	{
		unsigned int ind;
		if (m_pMappingFunc == NULL)
		{
			// find exact memory bank over mapping function 
			ind = address& 1;
		}
		else
		{
			ind = address& 1;
		}
		return m_ppBanks[ind];
	}

	void DispatchRequest(ST_request *req)
	{
		MemoryBankST *bank = GetBank(req->address);
		bank->request(req);
	}


	__address_t StartAddress() const {return m_nStartAddress;}
	__address_t EndAddress() const {return m_nEndAddress;}

//	bool request(ST_request *req)
//	{
//		m_pfifoReqIn->write(req);
//LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
//		clog << LOG_HEAD_OUTPUT << "request received"  << endl;
//LOG_VERBOSE_END
//		return true;
//	}

	bool read(ST_request *req)
	{
		m_pfifoReqIn->write(req);
		return true;
	}

	bool write(ST_request *req)
	{
		m_pfifoReqIn->write(req);
		return true;
	}


	void SetMappingFunc(unsigned int (*mappingFunc)(MemoryControllerST*, __address_t))
	{
		m_pMappingFunc = mappingFunc;
	}

	//////////////////////////////////////////////////////////////////////////
	// mapping functions

	// 1-skew mapping
	static unsigned int OneSkewMapping(MemoryControllerST* pThis, __address_t address)
	{
		return (address+(address/pThis->m_nBanks))%pThis->m_nBanks;
	}

	void Set2OneSkewMapping(){m_pMappingFunc = MemoryControllerST::OneSkewMapping;}

	//////////////////////////////////////////////////////////////////////////
	

    // inherited from SimObj
    virtual void InitializeLog(const char* logName, LOG_LOCATION ll, VERBOSE_LEVEL verbose = VERBOSE_ALL)
    {
        SimObj::InitializeLog(logName, ll, verbose);

        for (unsigned int i=0;i<m_nBanks;i++)
        {
            m_ppBanks[i]->InitializeLog(logName, ll, verbose);
        }
    }
};

//////////////////////////////
//} memory simulator namespace
}

#endif
