#include "busst.h"
#include "busst_master.h"
using namespace MemSim;

void BusST::BehaviorMaster()
{
    if (m_pReqCurINI == NULL)
    {
        // check if there is any request available from master side
        if (m_pfifoMasterIn.num_available_fast() > 0)
		{
            m_pfifoMasterIn.nb_read(m_pReqCurINI);
        }
    }
    
    if (m_pReqCurINI != NULL)
    {
	    // Dispatch request
        // If failed, stall and try again next cycle
    	if (m_port_slave.request(m_pReqCurINI))
        {
            m_pReqCurINI = NULL;
	    }
	}
}

void BusST::BehaviorSlave()
{
    switch (m_nStateMEM)
    {
    // when the bus is free
    case STATE_MEM_AVAILABLE:
    {
        // check if there is any requests available from the slave side
	    sc_fifo<MemSim::ST_request*> * p = static_cast<sc_fifo<MemSim::ST_request*>* >(m_port_fifo_slave_in.get_interface_fast());
        if (p->num_available_fast() > 0 &&
	        m_port_fifo_slave_in.nb_read(m_pReqCurMEM))
        {
            if ((m_pReqCurMEM->type == MemoryState::REQUEST_INVALIDATE_BR)||( (m_pReqCurMEM->type == MemoryState::REQUEST_WRITE_REPLY)&&(m_pReqCurMEM->bbackinv) ))
            {
                m_vecBCMasters = m_vecBusMaster;
                m_pReqCurMEM->refcount = m_vecBCMasters.size();
                BroadCastFeedback(m_pReqCurMEM);
            }
            else
            {
                // send the feedback to master
                SendFeedbackToMaster(m_pReqCurMEM);
            }
        }
        break;
    }
    
    case STATE_MEM_CONGEST:
        if (m_pReqCurMEM->type == MemoryState::REQUEST_INVALIDATE_BR ||
           (m_pReqCurMEM->type == MemoryState::REQUEST_WRITE_REPLY && m_pReqCurMEM->bbackinv))
        {
            BroadCastFeedback(m_pReqCurMEM);
        }
        else
        {
            SendFeedbackToMaster(m_pReqCurMEM);
        }
        break;

    default:
        break;
    }
}

void BusST::SendFeedbackToMaster(ST_request* req)
{
    if ( !get_initiator(req)->GetBusMaster()->GetFeedback(req) )
    {
        m_nStateMEM = STATE_MEM_CONGEST;
        return;
    }

    // pop when succeed
    pop_initiator(req);

    m_nStateMEM = STATE_MEM_AVAILABLE;
}


// CHKS : for WR request, this maybe unsafe, 
// should let all the WR(IB) go back first before WR reply to the initiator
void BusST::BroadCastFeedback(ST_request* req)
{
    for (vector<BusST_Master*>::iterator iter = m_vecBCMasters.begin(); iter != m_vecBCMasters.end(); ++iter)
    {
        if ((*iter)->GetFeedback(req))
        {
            m_vecBCMasters.erase(iter);
            iter = m_vecBCMasters.begin();
            if (iter == m_vecBCMasters.end())
            {
                break;
            }
        }
    }
    
    m_nStateMEM = (m_vecBCMasters.empty()) ? STATE_MEM_AVAILABLE : STATE_MEM_CONGEST;
}

bool BusST::request(ST_request *req)
{
    return m_pfifoMasterIn.nb_write(req);
}
