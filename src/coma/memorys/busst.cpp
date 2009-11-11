#include "busst.h"
#include "busst_master.h"
using namespace MemSim;

void BusST::BehaviorMaster()
{

    switch (m_nStateINI)
    {
    case STATE_INI_AVAILABLE:
        // check if there is any request available from master side
        if (m_pfifoMasterIn->num_available_fast() <= 0)
		{
            return;
		}

		
        if (!m_pfifoMasterIn->nb_read(m_pReqCurINI))
        {
            cerr << ERR_HEAD_OUTPUT << "should not get failed in this nb_read request" << endl;
            return;
        }

LOG_VERBOSE_BEGIN(VERBOSE_MOST)
    	clog << LOG_HEAD_OUTPUT << "Master event from " << /*((sc_module*)(req->initiator))->name() <<*/ endl
            << "\treq: type(" << m_pReqCurINI->type << "); address(" << m_pReqCurINI->getreqaddress() << ")" << endl;
LOG_VERBOSE_END

	    // dispatch request
	    DispatchRequest(m_pReqCurINI);

        break;          // could remove this "break" and above call alert

    // congest last time, try again this cycle
    case STATE_INI_CONGEST:

        // dispatch request
        DispatchRequest(m_pReqCurINI);

        break;

    default:
        break;
    }
	
}

void BusST::BehaviorSlave()
{

    switch (m_nStateMEM)
    {
    // when the bus is free
    case STATE_MEM_AVAILABLE:
        // check if there is any requests available from the slave side

	{sc_fifo<MemSim::_ST_request*> * p = static_cast<sc_fifo<MemSim::_ST_request*>* >(port_fifo_slave_in.get_interface_fast());
        if (p->num_available_fast()<=0)
            return;
        }
LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
    	clog << LOG_HEAD_OUTPUT << "Slave event "  << endl;
LOG_VERBOSE_END

	    if (!port_fifo_slave_in.nb_read(m_pReqCurMEM))
        {
            cerr << ERR_HEAD_OUTPUT << "should not get failed in this nb_read request" << endl;
            return;
        }

LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "Dealing req with address " << FMT_ADDR(m_pReqCurMEM->getreqaddress()) << endl;
LOG_VERBOSE_END

LOG_VERBOSE_BEGIN(VERBOSE_ALL)
	    print_request(m_pReqCurMEM);
LOG_VERBOSE_END


#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
        if (m_pReqCurMEM->type == MemoryState::REQUEST_INVALIDATE_BR)
        {
            RandomFeedbackDispatch(m_pReqCurMEM);
        }
#else
        if ((m_pReqCurMEM->type == MemoryState::REQUEST_INVALIDATE_BR)||( (m_pReqCurMEM->type == MemoryState::REQUEST_WRITE_REPLY)&&(m_pReqCurMEM->bbackinv) ))
        {
            m_vecBCMasters = m_vecBusMaster;
            m_pReqCurMEM->refcount = m_vecBCMasters.size();
            BroadCastFeedback(m_pReqCurMEM);
        }
#endif
        else
        {
            // send the feedback to master
            SendFeedbackToMaster(m_pReqCurMEM);
        }

        break;          // could remove this "break" and above call alert
        // when the bus is in congestion

    case STATE_MEM_CONGEST:

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
        if (m_pReqCurMEM->type == MemoryState::REQUEST_INVALIDATE_BR)
        {
            RandomFeedbackDispatch(m_pReqCurMEM);
        }
#else
        if ((m_pReqCurMEM->type == MemoryState::REQUEST_INVALIDATE_BR)||( (m_pReqCurMEM->type == MemoryState::REQUEST_WRITE_REPLY)&&(m_pReqCurMEM->bbackinv) ))
        {
            BroadCastFeedback(m_pReqCurMEM);
        }
#endif
        else
        {
            // send the feedback again
            SendFeedbackToMaster(m_pReqCurMEM);
        }

        break;

    default:
        break;
    }
}

void BusST::SendFeedbackToMaster(ST_request* req)
{
//LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
//    	clog << LOG_HEAD_OUTPUT <<  get_initiator_bus(req) << endl;
//        print_request(req);
//        SimObj* obj = get_initiator_bus(req);
//        clog << obj->GetBusMaster() << endl;
//LOG_VERBOSE_END

    if ( !get_initiator_bus(req)->GetBusMaster()->GetFeedback(req) )
    {
        m_nStateMEM = STATE_MEM_CONGEST;
        return;
    }

    // pop when succeed
    pop_initiator_bus(req);

    m_nStateMEM = STATE_MEM_AVAILABLE;
}


#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
void BusST::RandomFeedbackDispatch(ST_request* req)
{
    // use vecBCMasters to resend
    if (m_nStateMEM == STATE_MEM_CONGEST)
    {
        assert(m_vecBCMasters.size() == 1);
        if (m_vecBCMasters[0]->GetFeedback(req))
        {
            m_vecBCMasters.clear();
            m_nStateMEM = STATE_MEM_AVAILABLE;
        }

        return;
    }
    // can chooss random one to boardcast to
    unsigned int num = m_vecBusMaster.size();
    unsigned int fbid = (((unsigned int)req) >> 6)%num;

    if ( m_vecBusMaster[fbid]->GetFeedback(req) )
    {
        m_nStateMEM = STATE_MEM_AVAILABLE;
    }
    else
    {
        m_vecBCMasters.push_back(m_vecBusMaster[fbid]);
        m_nStateMEM = STATE_MEM_CONGEST;
    }
}
#else
// CHKS : for WR request, this maybe unsafe, 
// should let all the WR(IB) go back first before WR reply to the initiator
void BusST::BroadCastFeedback(ST_request* req)
{
    vector<BusST_Master*>::iterator iter;

    for (iter=m_vecBCMasters.begin();iter!=m_vecBCMasters.end();iter++)
    {
        if ( (*iter)->GetFeedback(req) )
        {
            m_vecBCMasters.erase(iter);
            iter = m_vecBCMasters.begin();
            if (iter == m_vecBCMasters.end())
                break;
        }
    }
    
    if (m_vecBCMasters.size() == 0)
        m_nStateMEM = STATE_MEM_AVAILABLE;
    else
        m_nStateMEM = STATE_MEM_CONGEST;
}
#endif


bool BusST::request(ST_request *req)
{
LOG_VERBOSE_BEGIN(VERBOSE_MOST)
    clog << LOG_HEAD_OUTPUT << "read/write request available"  << endl;
LOG_VERBOSE_END

    return (m_pfifoMasterIn->nb_write(req));
}

void BusST::MasterNotify()
{
	m_eMasterEvent.notify();
}

void BusST::SlaveNotify()
{
	m_eSlaveEvent.notify();
}

void BusST::DispatchRequest(ST_request *req)
{
	BusST_Slave_if* slave = GetSlave(req->getreqaddress());

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

            m_nStateINI = STATE_INI_CONGEST;
            return;
        }

		m_nStateINI = STATE_INI_AVAILABLE;
	}
	else
	{
    	cerr << ERR_HEAD_OUTPUT << "error - request can not be handled"  << endl;
	}
}

BusST_Slave_if* BusST::GetSlave(__address_t address)
{
  for (int i = 0; i < port_slave.size(); ++i)
    {
      BusST_Slave_if *slave = port_slave[i];

      // check the address range
//      if ((slave->StartAddress() <= address) && (address <= slave->EndAddress()))
	return slave;
    }
  
  return (BusST_Slave_if *)NULL;	
}

