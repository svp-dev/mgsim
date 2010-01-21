#ifndef _BUSSWITCH_H
#define _BUSSWITCH_H

#include "predef.h"

#include "busst_if.h"
#include "busst_slave_if.h"
#include "busst_master.h"

#include <map>
#include <set>

namespace MemSim{
//{ memory simulator namespace

class BusSwitch : public BusST_if, public sc_module
{
    enum STATE_INI{
        STATE_INI_AVAILABLE,
        STATE_INI_CONGEST
    };

    enum STATE_MEM{
        STATE_MEM_AVAILABLE,
        STATE_MEM_CONGEST
    };

    std::map<SimObj*, sc_fifo<ST_request*>*> m_mapfifoMasterIn;

    std::vector<ST_request*>   m_ppReqCurINI;
    std::vector<ST_request*>   m_ppReqCurMEM;
    std::vector<STATE_INI>     m_vecnStateINI;
    std::vector<STATE_MEM>     m_vecnStateMEM;
    std::set<BusST_Slave_if*>  m_setSlaveIF;
    //std::vector<BusST_Master*> m_vecBCMasters;

	sc_port<BusST_Slave_if, 0> port_slave;
	sc_fifo_in<ST_request*>*   pport_fifo_slave_in;

    int m_nMaster;
    int m_nSlave;
    int m_nAddrSkipWidth;
    int m_nChannelSplitBitWidth;

	BusST_Slave_if* GetSlave(__address_t address);
	void DispatchRequest(int masterindex, ST_request *req);
    void SendFeedbackToMaster(int slaveindex, ST_request* req);

public:
	SC_HAS_PROCESS(BusSwitch);

	// constructor
	BusSwitch(sc_module_name name, sc_clock& clock, int nMaster, int nSlave, uint64_t nChannelSelMode)
		: sc_module(name),
          m_ppReqCurINI(nMaster),
          m_ppReqCurMEM(nSlave),
          m_vecnStateINI(nMaster, STATE_INI_AVAILABLE),
          m_vecnStateMEM(nSlave,  STATE_MEM_AVAILABLE),
		  m_nMaster(nMaster),
		  m_nSlave(nSlave)
	{
		// process declaration
		SC_METHOD(BehaviorMaster);
		sensitive << clock.negedge_event();
		dont_initialize();

		SC_METHOD(BehaviorSlave);
		sensitive << clock.posedge_event();
		dont_initialize();

        pport_fifo_slave_in = new sc_fifo_in<ST_request*>[nSlave];

        // channel select mode:
        // 0-7:  Address Skip width (block width)
        // 8-11: Address select bits
        m_nAddrSkipWidth = (int)(nChannelSelMode & 0xff);
        m_nChannelSplitBitWidth = (int)((nChannelSelMode >> 8) & 0x0f);
	}

    ~BusSwitch(){
        delete[] pport_fifo_slave_in;
    }

	// process
	void BehaviorMaster();
	void BehaviorSlave();

	// direct BUS interface
    bool request(ST_request* req);

    void BindSlave(BusST_Slave_if& slave)
    {
        if (m_setSlaveIF.find(&slave) == m_setSlaveIF.end())
        {
            port_slave(slave);
            pport_fifo_slave_in[ m_setSlaveIF.size() ](slave.channel_fifo_slave);
            m_setSlaveIF.insert(&slave);
        }
    }

    void BindMaster(BusST_Master& master)
    {
        master.port_bus(*this);
        if (m_mapfifoMasterIn.find((SimObj*)(&master)) == m_mapfifoMasterIn.end())
        {
            sc_fifo<ST_request*>* fifomaster = new sc_fifo<ST_request*>();
            m_mapfifoMasterIn.insert(pair<SimObj*, sc_fifo<ST_request*>*>(&master, fifomaster));
        }
    }
};

}
#endif

