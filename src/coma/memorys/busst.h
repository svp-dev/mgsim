#ifndef _BUSST_H
#define _BUSST_H

#include "predef.h"

#include "busst_if.h"
#include "busst_slave_if.h"
#include "busst_master.h"

namespace MemSim{
//{ memory simulator namespace

class BusST : public BusST_if, public sc_module, virtual public SimObj
{
public:
	// ports
	sc_in<bool> clk;

	sc_port<BusST_Slave_if, 0> port_slave;
	sc_fifo_in<ST_request*> port_fifo_slave_in;

    vector<BusST_Master*> m_vecBusMaster;
private:
	sc_event m_eMasterEvent;
	sc_event m_eSlaveEvent;

	sc_fifo<ST_request*> *m_pfifoMasterIn;

    ST_request* m_pReqCurINI;
    ST_request* m_pReqCurMEM;

    enum STATE_INI{
        STATE_INI_AVAILABLE,
        STATE_INI_CONGEST
    };

    enum STATE_MEM{
        STATE_MEM_AVAILABLE,
        STATE_MEM_CONGEST
    };

    STATE_INI m_nStateINI;
    STATE_MEM m_nStateMEM;

public:
	SC_HAS_PROCESS(BusST);

	// constructor
	BusST(sc_module_name name)
		: SimObj(name), sc_module(name)
	{
		// process declaration
		SC_METHOD(BehaviorMaster);
		sensitive << clk.neg();
		dont_initialize();

		SC_METHOD(BehaviorSlave);
		sensitive << clk.pos();
		dont_initialize();

		m_pfifoMasterIn = new sc_fifo<ST_request*>();

        m_nStateINI = STATE_INI_AVAILABLE;
        m_nStateMEM = STATE_MEM_AVAILABLE;
	}

    ~BusST(){
        delete m_pfifoMasterIn;
    }

	// process
	void BehaviorMaster();
	void BehaviorSlave();

	// direct BUS interface
    bool request(ST_request* req);

	void MasterNotify();
	void SlaveNotify();

    void BindSlave(BusST_Slave_if& slave)
    {
        port_slave(slave);
        port_fifo_slave_in(slave.channel_fifo_slave);
        slave.m_pBusST = this;
    }

    void BindMaster(BusST_Master& master)
    {
        master.port_bus(*this);
        m_vecBusMaster.push_back(&master);
    }


private:
    vector<BusST_Master*> m_vecBCMasters;

	BusST_Slave_if* GetSlave(__address_t address);

	void DispatchRequest(ST_request *req);

    void SendFeedbackToMaster(ST_request* req);

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
    void RandomFeedbackDispatch(ST_request* req);
#else
    void BroadCastFeedback(ST_request* req);
#endif
	
public:
};

//} memory simulator namespace
}
#endif

