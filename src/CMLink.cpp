#include "CMLink.h"
#include <cassert>
#include "Processor.h"
#include "memstrace.h"

#include <iostream>
#include <iomanip>
#include <cstring>
using namespace Simulator;
using namespace std;
using namespace MemSim;

MemoryDataContainer *    CMLink::s_pMemoryDataContainer = NULL;

namespace MemSim{
    uint64_t g_uAccessDelayS = 0;
    uint64_t g_uAccessDelayL = 0;
    unsigned int g_uAccessL = 0;
    unsigned int g_uAccessS = 0;

//
//    unsigned int g_uRetryS = 0;
//    unsigned int g_uRetryL = 0;
//
//    uint64_t g_uRetryDelayS = 0;
//    uint64_t g_uRetryDelayL = 0;
//
//
//    unsigned int g_uRingorderS = 0;
//    unsigned int g_uRingorderL = 0;
//
//    uint64_t g_uRingorderDelayS = 0;
//    uint64_t g_uRingorderDelayL = 0;
//
//
//    unsigned int g_uCompeteS = 0;
//    unsigned int g_uCompeteL = 0;
//
//    uint64_t g_uCompeteDelayS = 0;
//    uint64_t g_uCompeteDelayL = 0;
//

    unsigned int g_uConflictS = 0;
    unsigned int g_uConflictL = 0;

    uint64_t g_uConflictDelayS = 0;
    uint64_t g_uConflictDelayL = 0;
}

void MemStatMarkConflict(unsigned long* ptr)
{
    if (ptr == NULL)
        return;

    Simulator::SimpleMemory::Request * req = (Simulator::SimpleMemory::Request*)ptr;

    req->bconflict = true;
}


#define MAX_DATA_SIZE 256		// *** JONY ***

int read_count = 0;
int write_count = 0;
unsigned long bbicount = 0;

#ifdef MEM_REQUEST_PROGRESS
unsigned int nreplies=0;
#endif


std::vector<CMLink*> *CMLink::s_pLinks = NULL;

bool CMLink::Allocate(MemSize size, int perm, MemAddr& address)
{
  assert (m_pProcessor != NULL);

//    assert ((address >> 16) != 0x6de7);
    if (s_pMemoryDataContainer == NULL)
        throw runtime_error("Allocate without memory container");

    s_pMemoryDataContainer->UpdateAllocate(address, size, perm);
#ifdef MEM_DATA_VERIFY
//    s_pMemoryDataContainer->UpdateAllocate()(address, size, (char*)data);
#endif

    return true;
//     return VirtualMemory::Allocate(size, perm, address);
}

bool CMLink::Read(IMemoryCallback& callback, MemAddr address, MemSize size, MemTag tag)
{
    // just to make the memory module active
    Request requestact;
    requestact.callback  = &callback;
    requestact.address   = address;
    requestact.data.size = size;
    requestact.data.tag  = tag;
    requestact.write     = false;
    // FIXME: the following 3 were initialized, is this correct?
    requestact.done = false;
    requestact.starttime = GetKernel()->GetCycleNo();
    requestact.bconflict = false;

    m_requests.Push(requestact);

    assert (m_pProcessor != NULL);
//   assert ((address >> 16) != 0x6de7);
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    //if (address + size > m_size)
    //{
    //    throw InvalidArgumentException("Reading outside of memory");
    //}

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP 
    char *peerdata = (char*)malloc(size);

    vector<LinkMGS*> &vecLinkPeers = m_linkmgs->m_vecLinkPeers;

    for (unsigned int i=0;i<vecLinkPeers.size();i++)
    {
        LinkMGS* peer = vecLinkPeers[i];
        if (peer == m_linkmgs)
            continue;

        // try to read from peer
        bool dcache = false;

        assert(tag.cid != INVALID_CID);
    	if (tag.data)
            dcache = true;

        CMLink* peercm = (CMLink*)peer->GetCMLinkPTR();
        if (peercm->OnMemorySnoopRead(address, (void*)peerdata, size, dcache))
        {
            COMMIT
            {
                #ifdef MEM_DEBUG_TRACE
                uint64_t cycleno = GetKernel()->GetCycleNo();
                LPID pid = m_pProcessor->GetPID();
                TID tid = 0xffff;
                traceproc(cycleno, pid, tid, false, address, size, peerdata);
                #endif

            cout << i << " ----------------------------- addr " << hex << address << " " << m_pProcessor->GetPID() << "\t" ;
            for (unsigned int k=0;k<size;k++)
                cout << hex << setw(2) << setfill('0') << (unsigned int)(((unsigned char*)peerdata)[k]) << " ";
            cout << endl;

                Request request;
                request.callback  = &callback;
                request.address   = address;
                //request.data.data = new char[ (size_t)size ];
                memcpy(request.data.data, peerdata, size);
                request.data.size = size;
                request.data.tag  = tag;
                request.done      = 0;
                request.write     = false;
                request.starttime = GetKernel()->GetCycleNo();
                request.bconflict = false;

                m_snoopreads.push(request);
            }

            free(peerdata); 
            cout << "ERROR !!!!" << endl;
            return true;
        }
    }

    free(peerdata);
#endif

//    if (m_config.bufferSize == INFINITE || m_setrequests.size() < m_config.bufferSize)
    {
        COMMIT
        {
            Request *prequest = new Request();
            prequest->callback  = &callback;
            prequest->address   = address;
            //prequest->data.data = new char[ (size_t)size ];
            prequest->data.size = size;
            prequest->data.tag  = tag;
            prequest->done      = 0;
            prequest->write     = false;
            prequest->starttime = GetKernel()->GetCycleNo();
            prequest->bconflict = false;
            //m_setrequests.insert(prequest);
            m_nTotalReq++;
            g_uAccessL++;

#ifndef MEM_CACHE_LEVEL_ONE_SNOOP
            // save call back and check 
            if (m_pimcallback == NULL)
                m_pimcallback = &callback;
            else assert (m_pimcallback == &callback);
#endif

            // push the request into the link
            m_linkmgs->PutRequest(address, prequest->write, size, NULL, (unsigned long*)prequest);
            //m_linkmgs->PutRequest(address, prequest->write, size, data, (unsigned long*)prequest);
            read_count ++;
        }
//        cout << "READ:: address - " << hex << address << " " << true << endl;
        return true;
    }
//    cout << "READ:: address - " << hex << address << " " << false << endl;
    return false;

}

/*
Result CMLink::Read(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag)
{
    assert(m_pProcessor != NULL);
//    assert ((address >> 16) != 0x6de7);
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    //if (address + size > m_size)
    //{
    //    throw InvalidArgumentException("Reading outside of memory");
    //}

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP 
    char *peerdata = (char*)malloc(size);

    vector<LinkMGS*> &vecLinkPeers = m_linkmgs->m_vecLinkPeers;

    for (unsigned int i=0;i<vecLinkPeers.size();i++)
    {
        LinkMGS* peer = vecLinkPeers[i];
        if (peer == m_linkmgs)
            continue;

        // try to read from peer
        bool dcache = false;

        assert(tag.cid != INVALID_CID);
    	if (tag.data)
            dcache = true;

        CMLink* peercm = (CMLink*)peer->GetCMLinkPTR();
        if (peercm->OnMemorySnoopRead(address, (void*)peerdata, size, dcache))
        {
            COMMIT
            {
                #ifdef MEM_DEBUG_TRACE
                uint64_t cycleno = GetKernel()->GetCycleNo();
                LPID pid = m_pProcessor->GetPID();
                TID tid = 0xffff;
                traceproc(cycleno, pid, tid, false, address, size, peerdata);
                #endif

            cout << i << " ----------------------------- addr " << hex << address << " " << m_pProcessor->GetPID() << "\t" ;
            for (unsigned int k=0;k<size;k++)
                cout << hex << setw(2) << setfill('0') << (unsigned int)(((unsigned char*)peerdata)[k]) << " ";
            cout << endl;

                Request request;
                request.callback  = &callback;
                request.address   = address;
                request.data.data = new char[ (size_t)size ];
                memcpy(request.data.data, peerdata, size);
                request.data.size = size;
                request.data.tag  = tag;
                request.done      = 0;
                request.write     = false;
                request.starttime = GetKernel()->GetCycleNo();
                request.bconflict = false;

                m_snoopreads.push(request);
            }

            free(peerdata); 
            return DELAYED;
        }
    }

    free(peerdata);
#endif

//    if (m_config.bufferSize == INFINITE || m_setrequests.size() < m_config.bufferSize)
    {
        COMMIT{
            Request *prequest = new Request();
            prequest->callback  = &callback;
            prequest->address   = address;
            prequest->data.data = new char[ (size_t)size ];
            prequest->data.size = size;
            prequest->data.tag  = tag;
            prequest->done      = 0;
            prequest->write     = false;
            prequest->starttime = GetKernel()->GetCycleNo();
            prequest->bconflict = false;
            //m_setrequests.insert(prequest);
            m_nTotalReq++;
            g_uAccessL++;

#ifndef MEM_CACHE_LEVEL_ONE_SNOOP
            // save call back and check 
            if (m_pimcallback == NULL)
                m_pimcallback = &callback;
            else assert (m_pimcallback == &callback);
#endif

            // push the request into the link
            m_linkmgs->PutRequest(address, prequest->write, size, data, (unsigned long*)prequest);
            read_count ++;
        }

        return DELAYED;
    }
    return FAILED;
}
*/

bool CMLink::Write(IMemoryCallback& callback, MemAddr address, const void* data, MemSize size, MemTag tag)
{
    // just to make the memory module active
    Request requestact;
    requestact.callback  = &callback;
    requestact.address   = address;
    requestact.data.size = size;
    requestact.data.tag  = tag;
    requestact.write     = true;
    // FIXME: the following 3 were not initialized! is this correct?
    requestact.done = false;
    requestact.starttime = GetKernel()->GetCycleNo();
    requestact.bconflict = false;

    m_requests.Push(requestact);

    assert(m_pProcessor != NULL);

    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    //if (address + size > m_size)
    //{
    //    throw InvalidArgumentException("Writing outside of memory");
    //}

//    if (m_config.bufferSize == INFINITE || m_setrequests.size() < m_config.bufferSize)
    {
        COMMIT
        { 
            Request *prequest = new Request();
            prequest->callback  = &callback;
            prequest->address   = address;
            //prequest->data.data = new char[ (size_t)size ];
            prequest->data.size = size;
            prequest->data.tag  = tag;
            prequest->done      = 0;
            prequest->write     = true;
            prequest->starttime = GetKernel()->GetCycleNo();
            prequest->bconflict = false;
            memcpy(prequest->data.data, data, (size_t)size);


#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
            vector<LinkMGS*> &vecLinkPeers = m_linkmgs->m_vecLinkPeers;
            for (unsigned int i=0;i<vecLinkPeers.size();i++)
            {
                LinkMGS* peer = vecLinkPeers[i];
                if (peer == m_linkmgs)
                    continue;

                CMLink* peercm = (CMLink*)peer->GetCMLinkPTR();
                if (!peercm->OnMemorySnoopWrite(address, (void*)prequest->data.data, size))
                    return false;
            }
#endif


#ifndef MEM_CACHE_LEVEL_ONE_SNOOP
            // save call back and check 
            if (m_pimcallback == NULL)
                m_pimcallback = &callback;
            else assert (m_pimcallback == &callback);
#endif

            // put request in link *** JONY ***
            //m_setrequests.insert(prequest); 
            m_nTotalReq++;
            g_uAccessS++;
            m_linkmgs->PutRequest(address, prequest->write, size, (void*)data, (unsigned long*)prequest);
            write_count ++;
        }

//        cout << "WRITE:: address - " << hex << address << " " << true << endl;
        return true;
    }
//    cout << "WRITE:: address - " << hex << address << " " << false << endl;
    return false;
}

/*
Result CMLink::Write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag)
{
    assert(m_pProcessor != NULL);

    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    //if (address + size > m_size)
    //{
    //    throw InvalidArgumentException("Writing outside of memory");
    //}

//    if (m_config.bufferSize == INFINITE || m_setrequests.size() < m_config.bufferSize)
    {
        COMMIT{ 
            Request *prequest = new Request();
            prequest->callback  = &callback;
            prequest->address   = address;
            prequest->data.data = new char[ (size_t)size ];
            prequest->data.size = size;
            prequest->data.tag  = tag;
            prequest->done      = 0;
            prequest->write     = true;
            prequest->starttime = GetKernel()->GetCycleNo();
            prequest->bconflict = false;
            memcpy(prequest->data.data, data, (size_t)size);


#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
            vector<LinkMGS*> &vecLinkPeers = m_linkmgs->m_vecLinkPeers;
            for (unsigned int i=0;i<vecLinkPeers.size();i++)
            {
                LinkMGS* peer = vecLinkPeers[i];
                if (peer == m_linkmgs)
                    continue;

                CMLink* peercm = (CMLink*)peer->GetCMLinkPTR();
                if (!peercm->OnMemorySnoopWrite(address, (void*)prequest->data.data, size))
                    return FAILED;
            }
#endif


#ifndef MEM_CACHE_LEVEL_ONE_SNOOP
            // save call back and check 
            if (m_pimcallback == NULL)
                m_pimcallback = &callback;
            else assert (m_pimcallback == &callback);
#endif

            // put request in link *** JONY ***
            //m_setrequests.insert(prequest); 
            m_nTotalReq++;
            g_uAccessS++;
            m_linkmgs->PutRequest(address, prequest->write, size, data, (unsigned long*)prequest);
            write_count ++;
        }

        return DELAYED;
    }
    return FAILED;
}
*/


Result CMLink::OnCycle(unsigned int)
{
//    cout << "OnCycle :: " << " ++++++++++++++++++++++" << endl;
    Result result = DELAYED;
#ifdef MEM_CACHE_LEVEL_ONE_SNOOP

    CycleNo now = GetKernel()->GetCycleNo();
    if (!m_snoopreads.empty())
    {   
        const Request& request = m_snoopreads.front();
        if (request.done > 0 && now >=request.done)
        {
            assert(request.write == false);

            if (!request.callback->OnMemoryReadCompleted(request.data))
            {
                cout << "OnCycle:: address - " << hex << request.address << " " << "failed" << endl;
                return FAILED;
            }
    
            COMMIT{
                 m_snoopreads.pop(); 
            }
            cout << "OnCycle:: address - " << hex << request.address << " " << "success" << endl;
            return SUCCESS;
        }
    }
    else
        result = DELAYED;


    if (!m_snoopreads.empty())
    {
        Request& request = m_snoopreads.front();
        if (request.done == 0)
        {
            COMMIT
            {
                CycleNo requestTime = 1;    // preset snoop time, or 0
                request.done = now + requestTime;
            }  
        }

        result = SUCCESS;
    }   
#endif

	//result = ((result == SUCCESS)||(!m_setrequests.empty())) ? SUCCESS : DELAYED;
	result = ((result == SUCCESS)||(m_nTotalReq != 0)) ? SUCCESS : DELAYED;

    MemAddr address;
    char data[MAX_DATA_SIZE];
    int extcode = 0;
    MemSize sz = 0;
    Request* prequest = (Request*)m_linkmgs->GetReply(address, data, sz, extcode);

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
    if ((extcode == 1)||(extcode == 2))
#else
    if (((extcode == 1)||(extcode == 2))&&(m_pimcallback!=NULL))
#endif
    {
        if (m_pProcessor == NULL)
        {
            return DELAYED;
        }

        COMMIT
        {
            bbicount++;

            MemData datatemp;
            memcpy(datatemp.data, data, MAX_MEMORY_OPERATION_SIZE);
//            datatemp.data = data;
            datatemp.size = sz;  // 0 represents IB, >0 represent UD
#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
            vector<LinkMGS*> &vecLinkPeers = m_linkmgs->m_vecLinkPeers;
            for (unsigned int i=0;i<vecLinkPeers.size();i++)
            {
                LinkMGS* peer = vecLinkPeers[i];
                // if (peer == m_linkmgs)
                //     continue;

                CMLink* peercm = (CMLink*)peer->GetCMLinkPTR();
                if (!peercm->OnMemorySnoopIB(address, data, sz))
                    return FAILED;
            }
#else
            m_pimcallback->OnMemorySnooped(address, datatemp);
#endif


            m_linkmgs->RemoveReply();
        }

//        cout << "OnCycle:: address - " << hex << address << " " << "success" << endl;
        return SUCCESS;
    }

    assert(m_pProcessor != NULL);


    //if (prequest != NULL)    	assert(!m_setrequests.empty());
    if (prequest != NULL)    	assert(m_nTotalReq);


	//if (!m_setrequests.empty())
	{
		if (prequest != NULL)
		{
//			std::set<Request*>::iterator iter = m_setrequests.find(prequest);
//			if (iter == m_setrequests.end())
//			{
//				std::cout << "error: cannot find returned request" << endl;
//				exit(2);
//				return FAILED;
//			}
//
//			Request* preq = *iter;
			Request* preq = prequest;
			if (preq->write) {}
			else {
				memcpy(preq->data.data, data, (size_t)preq->data.size);
			}

			// The current request has completed

			if (!preq->write && !preq->callback->OnMemoryReadCompleted(preq->data))
			{
//                cerr << "fail in read" << endl;
//                abort();
				return FAILED;
			}
			else if (preq->write && !preq->callback->OnMemoryWriteCompleted(preq->data.tag))
			{
//				cout << "fail in write" << endl;
				return FAILED;
			}

            m_requests.Pop();
			COMMIT
			{
                if (preq->write)
                {
                    write_count --;
                    g_uAccessDelayS += (GetKernel()->GetCycleNo() - preq->starttime);
                    if (preq->bconflict)
                    {
                        g_uConflictDelayS += (GetKernel()->GetCycleNo() - preq->starttime);
                        g_uConflictS ++;
                    }

                    ////DebugSimWrite(" reply read/write count : %d / %d\n", read_count, write_count);
//                    if ((read_count & 0x1f)==0)
//                        clog << dec << "reply -- read/write count : " << read_count << " / " << write_count << " | " << bbicount << endl;

                    #ifdef MEM_STORE_SEQUENCE_DEBUG
                    uint64_t cycleno = GetKernel()->GetCycleNo();
                    LPID pid = ((Processor*)prequest->callback)->GetPID();
                    uint64_t addr = preq->address;
                    debugSSRproc(preq->starttime, cycleno, pid, addr, preq->data.size, (char*)preq->data.data);
                    #endif

                }
                else
                {
                    #ifdef MEM_DEBUG_TRACE
                    uint64_t cycleno = ((Processor*)prequest->callback)->GetKernel().GetCycleNo();
                    LPID pid = ((Processor*)prequest->callback)->GetPID();
                    TID tid = 0xffff;
                    uint64_t addr = prequest->address;
                    traceproc(cycleno, pid, tid, false, addr, prequest->data.size, (char*)prequest->data.data);
                    #endif

                    read_count --;
                    g_uAccessDelayL += (GetKernel()->GetCycleNo() - preq->starttime);
                    if (preq->bconflict)
                    {
                        g_uConflictDelayL += (GetKernel()->GetCycleNo() - preq->starttime);
                        g_uConflictL ++;
                    }

//                    if ((read_count & 0x1f)==0)
//                        clog << dec << "reply -- read/write count : " << read_count << " / " << write_count << " | " << bbicount << endl;


                }
                #ifdef MEM_REQUEST_PROGRESS
                if (nreplies == 0xff)
                {
                    nreplies = 0;
                    clog << "." ;
                }
                else
                    nreplies++;

                if ((read_count == 0)&&(write_count == 0))
                    clog << endl;

                #endif

 
                // delete the request and data
//                Request *pdelreq = *iter;
//				m_setrequests.erase(iter);
//                delete pdelreq;
                m_nTotalReq--;
                delete preq;

				m_linkmgs->RemoveReply();

//                //////////////////////////////////////////////////////////////////////////
//                // print some stuff for debug
//                if ((read_count == 5) && (write_count == 0xd2c))                {
//                    std::vector<CMLink*>::iterator itervec;
//                    for (unsigned int j=0;j<s_pLinks->size();j++)
//                    {
//                        CMLink* plin = (*s_pLinks)[j];
//
//                        std::set<Request*>::iterator iter;
//                        for (iter=plin->m_setrequests.begin();iter!=plin->m_setrequests.end();iter++)
//                        {
//                            Request* reqxxx = *iter;
//                            if (reqxxx->write)
//                                continue;
//                        }
//                    }
//
//                }
//                //////////////////////////////////////////////////////////////////////////


			}
		}


	}


//    cout << "OnCycle:: address - " << hex << address << " " << result << endl;
	return SUCCESS;
	//return result;
}

/*
Result CMLink::OnCycleWritePhase(unsigned int stateIndex)
{
    Result result = DELAYED;
#ifdef MEM_CACHE_LEVEL_ONE_SNOOP

    CycleNo now = GetKernel()->GetCycleNo();
    if (!m_snoopreads.empty())
    {   
        const Request& request = m_snoopreads.front();
        if (request.done > 0 && now >=request.done)
        {
            assert(request.write == false);

            if (!request.callback->OnMemoryReadCompleted(request.data))
            {
                return FAILED;
            }
    
            COMMIT{
                 m_snoopreads.pop(); 
            }
            return SUCCESS;
        }
    }
    else
        result = DELAYED;


    if (!m_snoopreads.empty())
    {
        Request& request = m_snoopreads.front();
        if (request.done == 0)
        {
            COMMIT
            {
                CycleNo requestTime = 1;    // preset snoop time, or 0
                request.done = now + requestTime;
            }  
        }

        result = SUCCESS;
    }   
#endif

	//result = ((result == SUCCESS)||(!m_setrequests.empty())) ? SUCCESS : DELAYED;
	result = ((result == SUCCESS)||(m_nTotalReq != 0)) ? SUCCESS : DELAYED;

    MemAddr address;
    char data[MAX_DATA_SIZE];
    int extcode = 0;
    MemSize sz = 0;
    Request* prequest = (Request*)m_linkmgs->GetReply(address, data, sz, extcode);

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
    if ((extcode == 1)||(extcode == 2))
#else
    if (((extcode == 1)||(extcode == 2))&&(m_pimcallback!=NULL))
#endif
    {
    if (m_pProcessor == NULL)   return DELAYED;

        COMMIT
        {
            bbicount++;

            MemData datatemp;
            datatemp.data = data;
            datatemp.size = sz;  // 0 represents IB, >0 represent UD
#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
            vector<LinkMGS*> &vecLinkPeers = m_linkmgs->m_vecLinkPeers;
            for (unsigned int i=0;i<vecLinkPeers.size();i++)
            {
                LinkMGS* peer = vecLinkPeers[i];
                // if (peer == m_linkmgs)
                //     continue;

                CMLink* peercm = (CMLink*)peer->GetCMLinkPTR();
                if (!peercm->OnMemorySnoopIB(address, data, sz))
                    return FAILED;
            }
#else
            m_pimcallback->OnMemorySnooped(address, datatemp);
#endif


            m_linkmgs->RemoveReply();
        }

        return SUCCESS;
    }

    assert(m_pProcessor != NULL);


    //if (prequest != NULL)    	assert(!m_setrequests.empty());
    if (prequest != NULL)    	assert(m_nTotalReq);


	//if (!m_setrequests.empty())
	{
		if (prequest != NULL)
		{
//			std::set<Request*>::iterator iter = m_setrequests.find(prequest);
//			if (iter == m_setrequests.end())
//			{
//				std::cout << "error: cannot find returned request" << endl;
//				exit(2);
//				return FAILED;
//			}
//
//			Request* preq = *iter;
			Request* preq = prequest;
			if (preq->write) {}
			else {
				memcpy(preq->data.data, data, (size_t)preq->data.size);
			}

			// The current request has completed

			if (!preq->write && !preq->callback->OnMemoryReadCompleted(preq->data))
			{
				return FAILED;
			}
			else if (preq->write && !preq->callback->OnMemoryWriteCompleted(preq->data.tag))
			{
				cout << "fail in write" << endl;
				return FAILED;
			}

			COMMIT
			{
                if (preq->write)
                {
                    write_count --;
                    g_uAccessDelayS += (GetKernel()->GetCycleNo() - preq->starttime);
                    if (preq->bconflict)
                    {
                        g_uConflictDelayS += (GetKernel()->GetCycleNo() - preq->starttime);
                        g_uConflictS ++;
                    }

                    ////DebugSimWrite(" reply read/write count : %d / %d\n", read_count, write_count);
//                    if ((read_count & 0x1f)==0)
//                        clog << dec << "reply -- read/write count : " << read_count << " / " << write_count << " | " << bbicount << endl;

                    uint64_t cycleno = GetKernel()->GetCycleNo();
                    LPID pid = ((Processor*)prequest->callback)->GetPID();
                    uint64_t addr = preq->address;
                    #ifdef MEM_STORE_SEQUENCE_DEBUG
                    debugSSRproc(preq->starttime, cycleno, pid, addr, preq->data.size, (char*)preq->data.data);
                    #endif

                }
                else
                {
                    #ifdef MEM_DEBUG_TRACE
                    uint64_t cycleno = ((Processor*)prequest->callback)->GetKernel().GetCycleNo();
                    LPID pid = ((Processor*)prequest->callback)->GetPID();
                    TID tid = 0xffff;
                    uint64_t addr = prequest->address;
                    traceproc(cycleno, pid, tid, false, addr, prequest->data.size, (char*)prequest->data.data);
                    #endif

                    read_count --;
                    g_uAccessDelayL += (GetKernel()->GetCycleNo() - preq->starttime);
                    if (preq->bconflict)
                    {
                        g_uConflictDelayL += (GetKernel()->GetCycleNo() - preq->starttime);
                        g_uConflictL ++;
                    }

//                    if ((read_count & 0x1f)==0)
//                        clog << dec << "reply -- read/write count : " << read_count << " / " << write_count << " | " << bbicount << endl;


                }
                #ifdef MEM_REQUEST_PROGRESS
                if (nreplies == 0xff)
                {
                    nreplies = 0;
                    clog << "." ;
                }
                else
                    nreplies++;

                if ((read_count == 0)&&(write_count == 0))
                    clog << endl;

                #endif

 
                // delete the request and data
//                Request *pdelreq = *iter;
//				m_setrequests.erase(iter);
//                delete pdelreq;
                m_nTotalReq--;
                delete preq;

				m_linkmgs->RemoveReply();

//                //////////////////////////////////////////////////////////////////////////
//                // print some stuff for debug
//                if ((read_count == 5) && (write_count == 0xd2c))                {
//                    std::vector<CMLink*>::iterator itervec;
//                    for (unsigned int j=0;j<s_pLinks->size();j++)
//                    {
//                        CMLink* plin = (*s_pLinks)[j];
//
//                        std::set<Request*>::iterator iter;
//                        for (iter=plin->m_setrequests.begin();iter!=plin->m_setrequests.end();iter++)
//                        {
//                            Request* reqxxx = *iter;
//                            if (reqxxx->write)
//                                continue;
//                        }
//                    }
//
//                }
//                //////////////////////////////////////////////////////////////////////////


			}
		}


	}


	return result;
}
*/

bool CMLink::CheckPermissions(MemAddr /*address*/, MemSize , int ) const
{
  assert(m_pProcessor != NULL);
//  assert((address >> 16) != 0x6de7);
    return true;
}

void CMLink::Read(MemAddr address, void* data, MemSize size)
{
//    assert((address >> 16) != 0x6de7);
    if (s_pMemoryDataContainer == NULL)
        throw runtime_error("Read without memory container");

    s_pMemoryDataContainer->Fetch(address, size, (char*)data);
    //return VirtualMemory::read(address, data, size);
}

void CMLink::Write(MemAddr address, const void* data, MemSize size)
{
//    assert((address >> 16) != 0x6de7);
    if (s_pMemoryDataContainer == NULL)
        throw runtime_error("Write without memory container");
   
    s_pMemoryDataContainer->Update(address, size, (char*)data);
#ifdef MEM_DATA_VERIFY
    s_pMemoryDataContainer->VerifyUpdate(address, size, (char*)data);
#endif
//    return VirtualMemory::Write(address, data, size);
}

void CMLink::Reserve(MemAddr address, MemSize size, int perm)
{
//    assert((address >> 16) != 0x6de7);
    if (s_pMemoryDataContainer == NULL)
        throw runtime_error("Allocate without memory container");

    s_pMemoryDataContainer->UpdateReserve(address, size, perm);
#ifdef MEM_DATA_VERIFY
//    s_pMemoryDataContainer->UpdateAllocate()(address, size, (char*)data);
#endif

//    return VirtualMemory::Reserve(address, size, perm);
}

void CMLink::Unreserve(MemAddr address)
{
    if (s_pMemoryDataContainer == NULL)
        throw runtime_error("Allocate without memory container");

    s_pMemoryDataContainer->UpdateUnreserve(address);
//#ifdef MEM_DATA_VERIFY
////    s_pMemoryDataContainer->UpdateAllocate()(address, size, (char*)data);
//#endif
//
//    return VirtualMemory::Unreserve(address);

}



#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
bool CMLink::OnMemorySnoopRead(unsigned __int64 address, void *data, unsigned int size, bool dcache)
{
    return m_pProcessor->OnMemorySnoopedRead(address, data, size, dcache);
}

bool CMLink::OnMemorySnoopWrite(unsigned __int64 address, void *data, unsigned int size)
{
    return m_pProcessor->OnMemorySnoopedWrite(address, data, size);
}

bool CMLink::OnMemorySnoopIB(unsigned __int64 address, void* data, unsigned int size)
{
    return m_pProcessor->OnMemorySnoopedIB(address, data, size);
}

#endif

