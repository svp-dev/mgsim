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

class BusSwitch : public BusST_if, public sc_module, virtual public SimObj
{
public:
	// ports
	sc_in<bool> clk;

	sc_port<BusST_Slave_if, 0> port_slave;
	sc_fifo_in<ST_request*>* pport_fifo_slave_in;

    vector<BusST_Master*> m_vecBusMaster;
private:
	sc_event m_eMasterEvent;
	sc_event m_eSlaveEvent;

	//sc_fifo<ST_request*> **m_ppfifoMasterIn; // depending on how many masters are there
    map<SimObj*, sc_fifo<ST_request*>*> m_mapfifoMasterIn;

    ST_request** m_ppReqCurINI;
    ST_request** m_ppReqCurMEM;

    enum STATE_INI{
        STATE_INI_AVAILABLE,
        STATE_INI_CONGEST
    };

    enum STATE_MEM{
        STATE_MEM_AVAILABLE,
        STATE_MEM_CONGEST
    };

    vector<STATE_INI> m_vecnStateINI;
    vector<STATE_MEM> m_vecnStateMEM;

//    STATE_INI m_nStateINI;
//    STATE_MEM m_nStateMEM;

    int m_nMaster;
    int m_nSlave;

    int m_nAddrSkipWidth;
    int m_nChannelSplitBitWidth;
    int m_nInterleavingMethod;
    int m_nInterleavingParam;

    set<BusST_Slave_if*> m_setSlaveIF;

public:
	SC_HAS_PROCESS(BusSwitch);

	// constructor
	BusSwitch(sc_module_name name, int nMaster, int nSlave, uint64_t nChannelSelMode)
		: SimObj(name), sc_module(name), m_nMaster(nMaster), m_nSlave(nSlave)
	{
		// process declaration
		SC_METHOD(BehaviorMaster);
		sensitive << clk.neg();
		dont_initialize();

		SC_METHOD(BehaviorSlave);
		sensitive << clk.pos();
		dont_initialize();

        pport_fifo_slave_in = new sc_fifo_in<ST_request*>[nSlave];

//		m_ppfifoMasterIn = (sc_fifo<ST_request**>)malloc(sizeof(sc_fifo<ST_request*>)*nMaster);
//        for (int i=0;i<nMaster;i++)
//    		m_ppfifoMasterIn[i] = new sc_fifo<ST_request*>();

        m_ppReqCurINI = new ST_request* [nMaster];
        m_ppReqCurMEM = new ST_request* [nSlave];


        for (int i=0;i<nMaster;i++)
            m_vecnStateINI.push_back(STATE_INI_AVAILABLE);
        for (int i=0;i<nSlave;i++)
            m_vecnStateMEM.push_back(STATE_MEM_AVAILABLE);

        // channel select mode:
        // 0-7:  Address Skip width (block width)
        // 8-11: Address select bits
        // 12-15: Interleaving Method
        // 16-31: Interleaving parameters
        m_nAddrSkipWidth = (int)(nChannelSelMode & 0xff);
        m_nChannelSplitBitWidth = (int)((nChannelSelMode >> 8) & 0x0f);
        m_nInterleavingMethod = (int)((nChannelSelMode >> 12) & 0x0f);
        m_nInterleavingParam = (int)((nChannelSelMode >> 16) & 0xffff);

        if ((m_nInterleavingMethod != 0) && (m_nInterleavingMethod != 1))
        {
            clog << "warning: Only direct mappings and 1-skew are supported in the current version." << endl;
        }
	}

    ~BusSwitch(){
        delete[] pport_fifo_slave_in;
//        for (int i=0;i<m_nMaster;i++)
//            delete m_ppfifoMasterIn[i];
//        free(m_ppfifoMasterIn);


        delete m_ppReqCurINI;
        delete m_ppReqCurMEM;

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
            int ind = m_setSlaveIF.size();
            port_slave(slave);
            pport_fifo_slave_in[ind](slave.channel_fifo_slave);
//            port_fifo_slave_in(slave.channel_fifo_slave);
            slave.m_pBusST = this;

            m_setSlaveIF.insert(&slave);

        }
    }

    void BindMaster(BusST_Master& master)
    {
        master.port_bus(*this);
        //m_vecBusMaster.push_back(&master);

        if (m_mapfifoMasterIn.find((SimObj*)(&master)) == m_mapfifoMasterIn.end())
        {
            sc_fifo<ST_request*>* fifomaster = new sc_fifo<ST_request*>();
            m_mapfifoMasterIn.insert(pair<SimObj*, sc_fifo<ST_request*>*>(&master, fifomaster));

            m_vecnStateINI.push_back(STATE_INI_AVAILABLE);
        }
    }

    void MasterNotify()
    {
    }

    void SlaveNotify()
    {
    }


private:
    vector<BusST_Master*> m_vecBCMasters;

	BusST_Slave_if* GetSlave(__address_t address);

	void DispatchRequest(int masterindex, ST_request *req);

    void SendFeedbackToMaster(int slaveindex, ST_request* req);

public:
};

//} memory simulator namespace
}
#endif

