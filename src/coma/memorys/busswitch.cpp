#include "busswitch.h"
//#include "busst_master.h"
using namespace MemSim;



bool BusSwitch::request(ST_request *req)
{
LOG_VERBOSE_BEGIN(VERBOSE_MOST)
    clog << LOG_HEAD_OUTPUT << "read/write request available"  << endl;
LOG_VERBOSE_END

    SimObj* master = get_initiator_node(req);

    map<SimObj*, sc_fifo<ST_request*>*>::iterator iter = m_mapfifoMasterIn.find(master);
    if (iter == m_mapfifoMasterIn.end())
    {
        assert(false);
    }

    return iter->second->nb_write(req);
//    return (m_pfifoMasterIn->nb_write(req));
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
			LOG_VERBOSE_BEGIN(VERBOSE_MOST)
				clog << LOG_HEAD_OUTPUT << "Bus Congested when sending request to slave"  << endl;
			LOG_VERBOSE_END

			LOG_VERBOSE_BEGIN(VERBOSE_MOST)
				clog << LOG_HEAD_OUTPUT << "The following blocked request will be sent again."  << endl;
				print_request(req);
			LOG_VERBOSE_END

            stateini = STATE_INI_CONGEST;
            return;
        }

	    stateini = STATE_INI_AVAILABLE;
	}
	else
	{
    	cerr << ERR_HEAD_OUTPUT << "error - request can not be handled"  << endl;
	}
}


BusST_Slave_if* BusSwitch::GetSlave(__address_t address)
{
    if (m_nInterleavingMethod == 0) // direct
    {
        // get the bit mask
        static int bitmask = (1 << m_nChannelSplitBitWidth)-1;

        // geting bits
        int maskedid = (address >> m_nAddrSkipWidth)&bitmask;
        int channelnum = port_slave.size();
        int selid = maskedid % channelnum;

        BusST_Slave_if *slave = port_slave[selid];
	
        return slave;
    }
    else if (m_nInterleavingMethod == 1)    // k-skew
    {
        // get the bit mask
        static int bitmask = (1 << m_nChannelSplitBitWidth)-1;

        // geting bits
        int maskedid = (address >> m_nAddrSkipWidth)&bitmask;
        int channelnum = port_slave.size();

        int selid = (maskedid/channelnum + maskedid*m_nInterleavingParam)%channelnum;

        BusST_Slave_if *slave = port_slave[selid];
	
        return slave;
    }

    return NULL;
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
                cerr << ERR_HEAD_OUTPUT << "should not get failed in this nb_read request" << endl;
                continue;
            }

    LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "Master " << masterindex << " event from " << /*((sc_module*)(req->initiator))->name() <<*/ endl
                << "\treq: type(" << reqini->type << "); address(" << reqini->getreqaddress() << ")" << endl;
    LOG_VERBOSE_END

            // dispatch request
            DispatchRequest(masterindex, reqini);

            break;          // could remove this "break" and above call alert

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
    for (int i=0;i<m_nSlave;i++)
    {
        STATE_MEM &statemem = m_vecnStateMEM[i];

        ST_request*& reqmem = m_ppReqCurMEM[i];

        switch (statemem)
        {
        // when the bus is free
        case STATE_MEM_AVAILABLE:
            // check if there is any requests available from the slave side

            {
            sc_fifo<MemSim::_ST_request*> * p = static_cast<sc_fifo<MemSim::_ST_request*>* >(pport_fifo_slave_in[i].get_interface_fast());
            if (p->num_available_fast()<=0)
                continue;
            }
    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "Slave event "  << endl;
    LOG_VERBOSE_END

            if (!pport_fifo_slave_in[i].nb_read(reqmem))
            {
                cerr << ERR_HEAD_OUTPUT << "should not get failed in this nb_read request" << endl;

                continue;
            }

    LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "Dealing req with address " << FMT_ADDR(reqmem->getreqaddress()) << endl;
    LOG_VERBOSE_END

    LOG_VERBOSE_BEGIN(VERBOSE_ALL)
            print_request(reqmem);
    LOG_VERBOSE_END

            // send the feedback to master
            SendFeedbackToMaster(i, reqmem);
            break;
            // when the bus is in congestion


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

//LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
//    	clog << LOG_HEAD_OUTPUT <<  get_initiator_bus(req) << endl;
//        print_request(req);
//        SimObj* obj = get_initiator_bus(req);
//        clog << obj->GetBusMaster() << endl;
//LOG_VERBOSE_END

    if ( !get_initiator_bus(req)->GetBusMaster()->GetFeedback(req) )
    {
        statemem = STATE_MEM_CONGEST;
        return;
    }

    // pop when succeed
    pop_initiator_bus(req);

    statemem = STATE_MEM_AVAILABLE;
}


