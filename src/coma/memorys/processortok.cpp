#include "processortok.h"
using namespace MemSim;

namespace MemSim{
double g_fLatency = 0.0;
unsigned int g_uTotalL = 0;
unsigned int g_uTotalS = 0;
}

// link to MG simulator
void ProcessorTOK::Execute()
{
    if (m_eRequestIni != LF_AVAILABLE)
    {
        return;
    }

    // save the request 
    m_pReqCur = m_pReqLinkIni;

    // try to send the request to bus
    SendRequestBus();

#ifdef LIMITTED_REQUEST_TESTING
    // make increment on current memory request number
    m_nMemReq ++;
#endif
}

// issue a new reqeuest
void ProcessorTOK::PutRequest(uint64_t address, bool write, uint64_t size, void* data, unsigned long* ref)
{
    ST_request *preq = new ST_request();
    preq->pid = m_nPID;
    preq->addresspre = address >> g_nCacheLineWidth;
    preq->offset = address & ( ~((~0) << g_nCacheLineWidth) );
    preq->nsize = size;
    preq->Conform2BitVecFormat();
    preq->ref = ref;
    ADD_INITIATOR_BUS(preq, this);

    if (write)
        g_uTotalS++;
    else
        g_uTotalL++;

//LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
//    clog << LOG_HEAD_OUTPUT << "\t 0 ";
////    print_request(m_pReqCur);
//LOG_VERBOSE_END
//
    // make sure the size matches
    assert((preq->offset+size) <= g_nCacheLineSize);

    //////////////////////////////////////////////////////////////////////////
    assert(size <= ST_request::s_nRequestAlignedSize);

    // request type
    preq->type = write?MemoryState::REQUEST_WRITE:MemoryState::REQUEST_READ;

    if (write)
    {
        //memcpy(preq->data, data, sizeof(long)*size);
        memcpy(&preq->data[preq->offset], data, size);
#ifdef MEM_MODULE_STATISTICS
        m_tStatReqSent.write++;
#endif

#ifdef MEM_DATA_VERIFY
        m_pMemoryDataContainer->VerifyUpdate(address, size, (char*)data);     // 32 bit alert
#endif
//        m_pMemoryDataContainer->UpdateAllocate(address, size);
    }
    else
    {
#ifdef MEM_MODULE_STATISTICS
        m_tStatReqSent.read++;
#endif
    }

    if (m_eRequestIni == LF_AVAILABLE)
    {
        LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
            clog << LOG_HEAD_OUTPUT << "*** warning *** : another request is already in the link" << endl;
        LOG_VERBOSE_END

        // insert the newly added request to the link request buffer
        m_lstReqLinkIn.push_back(preq);

        return;
    }

    // update linking register and flag
    m_eRequestIni = LF_AVAILABLE;
    m_pReqLinkIni = preq;
}

// check the request and give a reply is available
// extcode: extension code for reply
//  0 : normal read/write reply request
//  1 : backward broadcast invalidation request
//  2 : update request (write reply from different processors sharing the same l2 cache)
unsigned long* ProcessorTOK::GetReply(uint64_t &address, void* data, uint64_t &size, int &extcode)
{
/*
    if (m_nPID == 3)
    {
        clog << sc_time_stamp() << " " << m_pReqLinkDone << " # " << m_nPID << endl;
        if (m_pReqLinkDone != NULL)
            clog << m_pReqLinkDone->getlineaddress() << endl;
    }
*/

//    LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
//        clog << LOG_HEAD_OUTPUT << "call from cmlink inside " << endl;
//    LOG_VERBOSE_END

	if ((m_pReqLinkDone == NULL)&&(m_lstReqLinkOut.empty()))
        return NULL;

    //address = m_pReqLinkDone->getreqaddress();

	if (m_pReqLinkDone == NULL)
	{
		m_pReqLinkDone = m_lstReqLinkOut.front();
		m_lstReqLinkOut.pop_front();
	}


	//cout << sc_time_stamp() << hex << ": " << address << " ";
	//print_request_type(m_pReqLinkDone);
	//cout << (int)(((unsigned char*)data)[0]) << dec << endl;

    unsigned long* ret;

//    LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
//        clog << LOG_HEAD_OUTPUT << "check replyd " << endl;
//        print_request(m_pReqLinkDone, true);
//    LOG_VERBOSE_END


    if (m_pReqLinkDone->type == MemoryState::REQUEST_INVALIDATE_BR)
    {
        extcode = 1;        // IB
        address = m_pReqLinkDone->getlineaddress(); // line address
        ret = NULL;
    }
    else if (m_pReqLinkDone->type == MemoryState::REQUEST_READ_REPLY)
    {
        extcode = 0;        // normal reply
        address = m_pReqLinkDone->getlineaddress();
        memcpy(data, &m_pReqLinkDone->data[m_pReqLinkDone->offset], m_pReqLinkDone->nsize);
        ret = m_pReqLinkDone->ref;
    }
    else if (m_pReqLinkDone->type == MemoryState::REQUEST_WRITE_REPLY)
    {
        if (m_pReqLinkDone->pid == m_nPID)
        {
            extcode = 0;        // normal reply
            address = m_pReqLinkDone->getlineaddress();
            ret = m_pReqLinkDone->ref;
        }
        else
        {
#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
	  abort();
#endif

#ifndef MEM_BACKWARD_INVALIDATION_UPDATE
            extcode = 1;    // IB
            address = m_pReqLinkDone->getlineaddress();
            size = 0;
            ret = NULL;
#else
            extcode = 2;
            address = m_pReqLinkDone->getreqaddress(); // line address
            ret = NULL;
            size = m_pReqLinkDone->nsize;
#endif
        }
        memcpy(data, &m_pReqLinkDone->data[m_pReqLinkDone->offset], m_pReqLinkDone->nsize);
    }
    else
    {
      abort();
    }

    //// ??? maybe more than one request is done
    //m_eRequestDone = LF_NONE;
    // delete m_pReqLinkDone;
    //return m_pReqLinkDone->ref;
    return ret;
}

bool ProcessorTOK::RemoveReply()
{
    assert(m_pReqLinkDone != NULL);

#ifdef MEM_MODULE_STATISTICS
    // statistics
    if (m_pReqLinkDone->type == MemoryState::REQUEST_READ_REPLY)
    {
        m_tStatReqSent.read--;
    }
    else if ((m_pReqLinkDone->type == MemoryState::REQUEST_WRITE_REPLY)&&(m_pReqLinkDone->pid == m_nPID))
    {
        m_tStatReqSent.write--;
    }
#endif

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
    LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
        clog << "request to be deleted " << endl;
        print_request(m_pReqLinkDone, true);
    LOG_VERBOSE_END

    delete m_pReqLinkDone;
#else
    // clean up the line-done request
    // if broadcasted
    if ((m_pReqLinkDone->type == MemoryState::REQUEST_INVALIDATE_BR)||( (m_pReqLinkDone->type == MemoryState::REQUEST_WRITE_REPLY)&&(m_pReqLinkDone->bbackinv) ))
    {
        m_pReqLinkDone->refcount--;

        //cout << m_pReqLinkDone->starttime << " ^ " << m_pReqLinkDone << " # " << m_pReqLinkDone->getlineaddress() << " @ " << m_nPID << " : " << (unsigned int)m_pReqLinkDone->refcount << endl;

        if (m_pReqLinkDone->refcount == 0)
        {
            /*
            if (m_pReqLinkDone->type == MemoryState::REQUEST_INVALIDATE_BR)
            {
                nib--;
                vector<ST_request*>::iterator iter;
                for (iter=vecpib.begin();iter!=vecpib.end();iter++)
                {
                    ST_request* reqxt = *iter;
                    if (reqxt == m_pReqLinkDone)
                    {
                        vecpib.erase(iter);
                        break;
                    }
                }
//                cout << m_pReqLinkDone << " # " << m_pReqLinkDone->getlineaddress() << " @ " << m_nPID << " : " << (unsigned int)m_pReqLinkDone->refcount << " nib " << dec << nib << endl;
            }
            */

            delete m_pReqLinkDone;
        }
    }
    else // otherwise
    {
//        LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
//            clog << "request to be deleted " << endl;
//            print_request(m_pReqLinkDone, true);
//        LOG_VERBOSE_END


        delete m_pReqLinkDone;
    }
#endif

    // update the link-done request
	if (!m_lstReqLinkOut.empty())
	{
		m_pReqLinkDone = m_lstReqLinkOut.front();
		m_lstReqLinkOut.pop_front();
		m_eRequestDone = LF_PROCESSED;
	}
	else
	{
		m_pReqLinkDone = NULL;
		m_eRequestDone = LF_NONE;
	}

	return true;
}

void ProcessorTOK::SendRequestBus()
{
    if (!port_bus->request(m_pReqCur))
    {
LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "bus busy, cannot send request " << m_pReqCur->getreqaddress() << endl;
LOG_VERBOSE_END

        m_nState = STATE_RETRY_BUS;
        return;
    }

    m_nState = STATE_IDLE;
    m_eRequestIni = LF_NONE;
    m_pReqCur->starttime = sc_time_stamp().to_seconds();

//    if (m_pReqCur->type == MemoryState::REQUEST_WRITE)
//    {
//        m_vecTEST.push_back(m_pReqCur->getreqaddress());
//    }
//
#ifdef MEM_MODULE_STATISTICS
    if (m_pReqCur->type == MemoryState::REQUEST_READ)
//    if (m_pReqCur->type == MemoryState::REQUEST_WRITE)
    {
        m_vecTEST.push_back(m_pReqCur->getreqaddress());
    }
#endif
	// begin: deal with possible suspended request in lstReqLink
    if (!m_lstReqLinkIn.empty())
    {
        m_eRequestIni = LF_AVAILABLE;
        m_pReqLinkIni = m_lstReqLinkIn.front();
        m_lstReqLinkIn.pop_front();
    }

	// end  : deal with possible suspended request in lstReqLink

LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
    clog << endl << TMP_REQBEG_PUTPUT << "\t";
    print_request(m_pReqCur);
    clog << TMP_REQBEG_PUTPUT;
LOG_VERBOSE_END

LOG_VERBOSE_BEGIN(VERBOSE_MOST)
    clog << LOG_HEAD_OUTPUT << "suspended on memory request " << m_pReqCur->getreqaddress() << endl;
LOG_VERBOSE_END
}

void ProcessorTOK::BehaviorIni()
{
    switch (m_nState){
        // only do initiative jobs, when the processor is free
        case STATE_IDLE:
#ifdef LIMITTED_REQUEST_TESTING
            if (m_nMemReq >= m_nMemReqTotal)
            {
                m_nState = STATE_HALT;
LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
                clog << LOG_HEAD_OUTPUT << "processor halt" << endl;
LOG_VERBOSE_END
                return;
            }
#endif

//LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
//    clog << LOG_HEAD_OUTPUT << "\t 1 ";
////    print_request(m_pReqCur);
//LOG_VERBOSE_END
//

            Execute();
            break;

        // retry request since bus was busy
        case STATE_RETRY_BUS:
            SendRequestBus();
            break;

#ifdef LIMITTED_REQUEST_TESTING
        // halt the processor
        case STATE_HALT:
            break;
#endif
        default:
            break;
    }
}

void ProcessorTOK::FunNonMemory()
{
    // delay

LOG_VERBOSE_BEGIN(VERBOSE_ALL)
    clog << LOG_HEAD_OUTPUT << "non-memory instruction execution" << endl;
LOG_VERBOSE_END

    // add some delays
}

void ProcessorTOK::BehaviorMem()
{
/*    
    if (m_pReqLinkDone!=NULL)
        clog << hex << "$" << m_nPID << " @" << m_pReqLinkDone->getlineaddress() << endl;

    list<ST_request*>::iterator iter;
    for (iter = m_lstReqLinkOut.begin();iter!=m_lstReqLinkOut.end();iter++)
    {
        clog << hex << "#" << m_nPID << " @" << (*iter)->getlineaddress() << "  ";
    }
    if (!m_lstReqLinkOut.empty())
        clog << endl;
*/


#ifdef MEM_MODULE_STATISTICS
    // statistics
    if (m_pmmStatLatency!=NULL)
        m_pStatCurReplyReq = NULL;
#endif

//    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
//        clog << LOG_HEAD_OUTPUT << "fifosize: " << m_pfifoFeedback->num_available_fast() << endl;
//    LOG_VERBOSE_END

    // check whether there are any replies from the memory hierarchy
    if (m_pfifoFeedback->num_available_fast() <= 0)
    {
        //m_eContinueProcessing.notify();
        //wait(m_eWaitingForMemory);
        //wait(port_clk.posedge_event());
        //continue;
        return;
    }

    // get feedback
    ST_request* req;
    if (!m_pfifoFeedback->nb_read(req))
    {
        cerr << ERR_HEAD_OUTPUT << "should not get failed in this nb_read request" << endl;
        abort();
        return;
    }

#ifdef SIMULATE_DATA_TRANSACTION
    // check if the data matches, if the read quest get answered
    if (req->type == MemoryState::REQUEST_READ_REPLY)
    {
        #ifdef MEM_DATA_VERIFY
        bool match = m_pMemoryDataContainer->Verify(req->getlineaddress(), req->nsize, (char*)req->data);     // 32 bit alert
        if (match)
        {
LOG_VERBOSE_BEGIN(VERBOSE_ALL)
            clog << LOG_HEAD_OUTPUT << "Read request@" << FMT_ADDR(req->getreqaddress()) << " return correct value with size " << req->nsize << " : [";
            for (unsigned i=0;i<req->nsize;i++)
            {
                clog << hex << setw(2) << setfill('0') << (unsigned int)(unsigned char)req->data[req->offset+i];

                if (i != req->nsize - 1)
                    clog << " ";
            }
            clog << "]" << endl;
LOG_VERBOSE_END

/*
            if (req->getlineaddress() == 0x112000)
            {

LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << "yyyyyyyyyyyyyyyy" << endl;
            clog << LOG_HEAD_OUTPUT << "Read request@" << FMT_ADDR(req->getreqaddress()) << " return correct value with size " << req->nsize << " : [";
            for (unsigned i=0;i<req->nsize;i++)
            {
                clog << hex << setw(2) << setfill('0') << (unsigned int)(unsigned char)req->data[req->offset+i];

                if (i != req->nsize - 1)
                    clog << " ";
            }
            clog << "]" << endl;
LOG_VERBOSE_END

            }
*/

        }
        else
        {
// the error will be print out to error console already from FlatMemoryDataSim class
            cerr << ERR_HEAD_OUTPUT << "Read request@" << FMT_ADDR(req->getreqaddress()) << " return wrong value - " << FMT_DTA(req->data[0]) << endl;
LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "Read request@" << FMT_ADDR(req->getreqaddress()) << " return wrong value - " << FMT_DTA(req->data[0]) << endl;
LOG_VERBOSE_END
        }
        #endif
    }
#endif

#ifdef MEM_MODULE_STATISTICS
//if (req->type == MemoryState::REQUEST_READ_REPLY)
//{
//        vector<__address_t>::iterator iter;
//        for (iter=m_vecTEST.begin();iter!=m_vecTEST.end();iter++)
//        {
//            if (*iter == req->getreqaddress())
//            {
//                m_vecTEST.erase(iter);
//                break;
//            }
//        }
//}
//
if (req->type == MemoryState::REQUEST_READ_REPLY)
//if (req->type == MemoryState::REQUEST_WRITE_REPLY)
{
        vector<__address_t>::iterator iter;
        for (iter=m_vecTEST.begin();iter!=m_vecTEST.end();iter++)
        {
            if (*iter == req->getreqaddress())
            {
                m_vecTEST.erase(iter);
                break;
            }
        }
}
#endif

//LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
//    clog << LOG_HEAD_OUTPUT << "bus feedback@" << FMT_ADDR(req->getreqaddress()) << " details:" << endl;
//LOG_VERBOSE_END
//
//LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
//    clog << "\t"; print_request(req, true);
//LOG_VERBOSE_END

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
    if (req->type == MemoryState::REQUEST_INVALIDATE_BR)
#else
    if ( (req->type == MemoryState::REQUEST_INVALIDATE_BR)||((req->type == MemoryState::REQUEST_WRITE_REPLY)&&(req->bbackinv)&&(req->pid != m_nPID)) )
#endif
    {
        m_eRequestDone = LF_PROCESSED;
		if (m_pReqLinkDone != NULL)
        {
			m_lstReqLinkOut.push_back(req);
        }
		else
        {
	        m_pReqLinkDone = req;   // req is on heap
        }

        //////////////////////////////////////////////////////////////////////////
        // *** the request can be invalidation as well

        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "IB \t";
            print_request(req, true);
            clog << LOG_HEAD_OUTPUT << "\n";
        LOG_VERBOSE_END
        return;
    }

    if ((req->type == MemoryState::REQUEST_READ_REPLY)||(req->type == MemoryState::REQUEST_WRITE_REPLY))
    {
        m_eRequestDone = LF_PROCESSED;

        // pop the initiator if the WR is actually broadcasted, instead of directly sent
#ifndef MEM_CACHE_LEVEL_ONE_SNOOP
        if ((req->type == MemoryState::REQUEST_WRITE_REPLY)&&(req->bbackinv)&&(req->pid == m_nPID))
            pop_initiator_bus(req);
#endif

        //////////////////////////////////////////////////////////////////////////
        // *** the request can be invalidation as well

        LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
            clog << TMP_REQEND_PUTPUT << "\t";
            clog << req->bprocessed << "\t";
            print_request_type(req);
            print_request(req, true);
/*
if ( (req->getlineaddress()>= 0x113200)&&(req->getlineaddress() < 0x113240) ) 
{
    // check main memory data 
    char data[0x40];
    g_pMemoryDataContainer->Fetch(0x113200, 0x40, data);

    for (unsigned int i=0;i<0x40;i++)
    {
        cout << hex << setw(2) << setfill('0') << (unsigned int)(((unsigned char*)data)[i]) << " ";
        clog << hex << setw(2) << setfill('0') << (unsigned int)(((unsigned char*)data)[i]) << " ";
    }
    cout << endl;
    clog << endl;
}
*/
            clog << TMP_REQEND_PUTPUT << "\n";
        LOG_VERBOSE_END


#ifdef MEM_MODULE_STATISTICS
        // statistics
        if (m_pmmStatLatency!=NULL)
        {
            m_pStatCurReplyReq = new ST_request(req);
            m_pStatCurReplyReq->ref = (unsigned long*)req;
        }
#endif

        // return requests to simulator
		if (m_pReqLinkDone != NULL)
        {
			m_lstReqLinkOut.push_back(req);
        }
		else
        {
	        m_pReqLinkDone = req;   // req is on heap
        }


    }
}

#ifdef MEM_MODULE_STATISTICS
////////////////////////////////////////////////////////////////////////
void ProcessorTOK::InitializeStatistics(unsigned int components)
{
    switch(components)
    {
    case STAT_PROC_COMP_LATENCY:
        if (m_pmmStatLatency == NULL)
            m_pmmStatLatency = new map<double, stat_stru_req_latency_t>();
        break;

    case STAT_PROC_COMP_REQ_SENT:
        if (m_pmmStatReqSent == NULL)
            m_pmmStatReqSent = new map<double, stat_stru_req_sent_t>();
        break;

    case STAT_CACHE_COMP_ALL:
        if (m_pmmStatLatency == NULL)
            m_pmmStatLatency = new map<double, stat_stru_req_latency_t>();
        if (m_pmmStatReqSent == NULL)
            m_pmmStatReqSent = new map<double, stat_stru_req_sent_t>();
        break;

    default:
        cout << "warning: specified statistics not matching with the cache" << endl;
        break;
    }
}

void ProcessorTOK::Statistics(STAT_LEVEL lev)
{
    if (m_pmmStatLatency != NULL)
    {
        if (m_pStatCurReplyReq != NULL)
        {
            stat_stru_req_latency_t reqlat;
            reqlat.ptr = m_pStatCurReplyReq->ref;
            reqlat.start = m_pStatCurReplyReq->starttime;
            reqlat.end = sc_time_stamp().to_seconds();
            reqlat.latency = sc_time_stamp().to_seconds() - m_pStatCurReplyReq->starttime;
            reqlat.address = m_pStatCurReplyReq->getreqaddress();
            reqlat.type = m_pStatCurReplyReq->type;
//            memcpy(reqlat.data, m_pStatCurReplyReq->data, 64);
            delete m_pStatCurReplyReq;
            g_fLatency += reqlat.latency;
            m_pStatCurReplyReq = NULL;
            m_pmmStatLatency->insert(pair<double,stat_stru_req_latency_t>(sc_time_stamp().to_seconds(), reqlat));
        }
    }

    if (m_pmmStatReqSent != NULL)
    {
        m_pmmStatReqSent->insert(pair<double, stat_stru_req_sent_t>(sc_time_stamp().to_seconds(), m_tStatReqSent));
    }
}

void ProcessorTOK::DumpStatistics(ofstream& outfile, unsigned int component, unsigned int type)
{
    if (m_pmmStatLatency != NULL)
    {
        map<double, stat_stru_req_latency_t>::iterator iter;

        for (iter=m_pmmStatLatency->begin();iter!=m_pmmStatLatency->end();iter++)
        {
            outfile << setw(10) << (*iter).first << "\t" << name() << "\t" << (*iter).second.start << "\t"<< (*iter).second.end << "\t" << (*iter).second.latency << "\t" << (*iter).second.ptr << "\t" << hex << (*iter).second.address << "\t" << (*iter).second.type << ";";
            for (int i=0;i<64;i++)
            {
//                outfile << " " << setw(2) << setfill('0') << hex << (unsigned int)(unsigned char)(*iter).second.data[i];
            }
            outfile << endl;
        }
    }

    if (m_pmmStatReqSent != NULL)
    {
        map<double, stat_stru_req_sent_t>::iterator iter;

        for (iter=m_pmmStatReqSent->begin();iter!=m_pmmStatReqSent->end();iter++)
        {
            outfile << setw(10) << (*iter).first << "\t" << name() << ".reqs_sent\t" << (*iter).second.read << "\t"<< (*iter).second.write << "\t" << ((*iter).second.read + (*iter).second.write) << endl;
            outfile << m_vecTEST.size() << "\t";
            for (unsigned int i=0;i<m_vecTEST.size();i++)
                outfile << hex << m_vecTEST[i] << "  ";
            outfile << endl;
        }
    }
}
#endif

