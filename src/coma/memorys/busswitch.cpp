#include "busswitch.h"
using namespace MemSim;

bool BusSwitch::request(ST_request *req)
{
    SimObj* master = get_initiator(req);

    map<SimObj*, sc_fifo<ST_request*>*>::iterator iter = m_mapfifoMasterIn.find(master);
    assert(iter != m_mapfifoMasterIn.end());

    return iter->second->nb_write(req);
}

void BusSwitch::DispatchRequest(int nBusMasterIndex, ST_request *req)
{
	BusST_Slave_if* slave = GetSlave(req->getreqaddress());
    STATE_INI& stateini = m_vecnStateINI[nBusMasterIndex];

	if (slave != NULL) 
	{
        // if failed, stall and try again next cycle
		if (!slave->request(req))
        {
            stateini = STATE_INI_CONGEST;
            return;
        }
	    stateini = STATE_INI_AVAILABLE;
	}
}


BusST_Slave_if* BusSwitch::GetSlave(__address_t address)
{
    unsigned int bitmask = (1U << m_nChannelSplitBitWidth) - 1;
    unsigned int maskedid = (address >> m_nAddrSkipWidth) & bitmask;

    return port_slave[maskedid % port_slave.size()];
}

void BusSwitch::BehaviorMaster()
{
    map<SimObj*, sc_fifo<ST_request*>*>::iterator iter;
    int masterindex = 0;

    for (iter=m_mapfifoMasterIn.begin();iter!=m_mapfifoMasterIn.end();iter++,masterindex++)
    {
        STATE_INI &stateini = m_vecnStateINI[masterindex];
        ST_request*& reqini = m_ppReqCurINI[masterindex];
        sc_fifo<ST_request*>* fifomasterin = iter->second;

        switch (stateini)
        {
        case STATE_INI_AVAILABLE:
            // check if there is any request available from master side
            if (fifomasterin->num_available_fast() <= 0)
            {
                continue;
            }

            if (!fifomasterin->nb_read(reqini))
            {
                continue;
            }

            // dispatch request
            DispatchRequest(masterindex, reqini);
            break;

        // congest last time, try again this cycle
        case STATE_INI_CONGEST:

            // dispatch request
            DispatchRequest(masterindex, reqini);
            break;

        default:
            break;
        }

    }
}


void BusSwitch::BehaviorSlave()
{
    for (int i = 0; i < m_nSlave; ++i)
    {
        STATE_MEM &statemem = m_vecnStateMEM[i];

        ST_request*& reqmem = m_ppReqCurMEM[i];

        switch (statemem)
        {
        // when the bus is free
        case STATE_MEM_AVAILABLE:
        {
            // check if there is any requests available from the slave side
            sc_fifo<MemSim::ST_request*> * p = static_cast<sc_fifo<MemSim::ST_request*>* >(pport_fifo_slave_in[i].get_interface_fast());
            if (p->num_available_fast()<=0)
                continue;
            
            if (!pport_fifo_slave_in[i].nb_read(reqmem))
            {
                continue;
            }

            // send the feedback to master
            SendFeedbackToMaster(i, reqmem);
            break;
        }

        case STATE_MEM_CONGEST:
            // send the feedback again
            SendFeedbackToMaster(i, reqmem);
            break;

        default:
            break;
        }

    }
}

void BusSwitch::SendFeedbackToMaster(int nSlaveIndex, ST_request* req)
{
    STATE_MEM &statemem = m_vecnStateMEM[nSlaveIndex];

    if ( !get_initiator(req)->GetBusMaster()->GetFeedback(req) )
    {
        statemem = STATE_MEM_CONGEST;
        return;
    }

    // pop when succeed
    pop_initiator(req);

    statemem = STATE_MEM_AVAILABLE;
}


