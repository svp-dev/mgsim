#include "CMLink.h"
#include <cassert>
#include "Processor.h"

#include <iostream>
#include <iomanip>
#include <cstring>

#include "coma/memorys/predef.h"

using namespace Simulator;
using namespace std;
using namespace MemSim;

namespace MemSim
{
    uint64_t g_uAccessDelayS = 0;
    uint64_t g_uAccessDelayL = 0;
    unsigned int g_uAccessL = 0;
    unsigned int g_uAccessS = 0;
    unsigned int g_uConflictS = 0;
    unsigned int g_uConflictL = 0;
    uint64_t g_uConflictDelayS = 0;
    uint64_t g_uConflictDelayL = 0;
}

void MemStatMarkConflict(unsigned long* ptr)
{
    if (ptr != NULL)
    {
        ((CMLink::Request*)ptr)->bconflict = true;
    }
}

#define MAX_DATA_SIZE 256       // *** JONY ***

int read_count = 0;
int write_count = 0;
unsigned long bbicount = 0;

bool CMLink::Allocate(MemSize size, int perm, MemAddr& address)
{
    return g_pMemoryDataContainer->Allocate(size, perm, address);
}

void CMLink::RegisterClient(PSize pid, IMemoryCallback& callback, const Process* processes[])
{
    assert(m_clients[pid] == NULL);
    m_clients[pid] = &callback;
}

void CMLink::UnregisterClient(PSize pid)
{
    assert(m_clients[pid] != NULL);
    m_clients[pid] = NULL;
}

bool CMLink::Read(PSize pid, MemAddr address, MemSize size)
{
    assert(m_clients[pid] != NULL);
    IMemoryCallback& callback = *m_clients[pid];
    
    // just to make the memory module active
    Request requestact;
    requestact.callback  = &callback;
    requestact.address   = address;
    requestact.data.size = size;
    requestact.write     = false;
    // FIXME: the following 3 were initialized, is this correct?
    requestact.done = false;
    requestact.starttime = GetCycleNo();
    requestact.bconflict = false;

    m_requests.Push(requestact);

    assert (m_pProcessor != NULL);
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    COMMIT
    {
        Request *prequest = new Request();
        prequest->callback  = &callback;
        prequest->address   = address;
        //prequest->data.data = new char[ (size_t)size ];
        prequest->data.size = size;
        prequest->done      = 0;
        prequest->write     = false;
        prequest->starttime = GetCycleNo();
        prequest->bconflict = false;

        m_nTotalReq++;
        g_uAccessL++;

        // save call back and check 
        if (m_pimcallback == NULL)
            m_pimcallback = &callback;
        else assert (m_pimcallback == &callback);

        // push the request into the link
        m_linkmgs->PutRequest(address, prequest->write, size, NULL, (unsigned long*)prequest);
        read_count ++;
    }
    return true;
}

bool CMLink::Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid)
{
    assert(m_clients[pid] != NULL);
    IMemoryCallback& callback = *m_clients[pid];
    
    // just to make the memory module active
    Request requestact;
    requestact.callback  = &callback;
    requestact.address   = address;
    requestact.data.size = size;
    requestact.tid       = tid;
    requestact.write     = true;
    // FIXME: the following 3 were not initialized! is this correct?
    requestact.done = false;
    requestact.starttime = GetCycleNo();
    requestact.bconflict = false;

    m_requests.Push(requestact);

    assert(m_pProcessor != NULL);

    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    COMMIT
    { 
        Request *prequest = new Request();
        prequest->callback  = &callback;
        prequest->address   = address;
        //prequest->data.data = new char[ (size_t)size ];
        prequest->data.size = size;
        prequest->tid       = tid;
        prequest->done      = 0;
        prequest->write     = true;
        prequest->starttime = GetCycleNo();
        prequest->bconflict = false;
        memcpy(prequest->data.data, data, (size_t)size);

        // save call back and check 
        if (m_pimcallback == NULL)
            m_pimcallback = &callback;
        else assert (m_pimcallback == &callback);

        // put request in link *** JONY ***
        //m_setrequests.insert(prequest); 
        m_nTotalReq++;
        g_uAccessS++;
        m_linkmgs->PutRequest(address, prequest->write, size, (void*)data, (unsigned long*)prequest);
        write_count ++;
    }
    return true;
}

Result CMLink::DoRequests()
{
    Result result = DELAYED;

    result = (m_nTotalReq != 0) ? SUCCESS : DELAYED;

    MemAddr address;
    char data[MAX_DATA_SIZE];
    int extcode = 0;
    MemSize sz = 0;
    Request* prequest = (Request*)m_linkmgs->GetReply(address, data, sz, extcode);

    if ((extcode == 1 || extcode == 2) && m_pimcallback != NULL)
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
            datatemp.size = sz;  // 0 represents IB, >0 represent UD
            m_pimcallback->OnMemorySnooped(address, datatemp);
            m_linkmgs->RemoveReply();
        }

        return SUCCESS;
    }

    assert(m_pProcessor != NULL);

    if (prequest != NULL)
    {
        assert(m_nTotalReq);
        
        Request* preq = prequest;
        if (preq->write) {}
        else {
            memcpy(preq->data.data, data, (size_t)preq->data.size);
        }

        // The current request has completed

        if (!preq->write && !preq->callback->OnMemoryReadCompleted(preq->address, preq->data))
        {
            return FAILED;
        }
        else if (preq->write && !preq->callback->OnMemoryWriteCompleted(preq->tid))
        {
            return FAILED;
        }

        m_requests.Pop();
        COMMIT
        {
            if (preq->write)
            {
                write_count --;
                g_uAccessDelayS += (GetCycleNo() - preq->starttime);
                if (preq->bconflict)
                {
                    g_uConflictDelayS += (GetCycleNo() - preq->starttime);
                    g_uConflictS ++;
                }
            }
            else
            {
                read_count --;
                g_uAccessDelayL += (GetCycleNo() - preq->starttime);
                if (preq->bconflict)
                {
                    g_uConflictDelayL += (GetCycleNo() - preq->starttime);
                    g_uConflictL ++;
                }
            }

            // delete the request and data
            m_nTotalReq--;
            delete preq;

            m_linkmgs->RemoveReply();
        }
    }
    return SUCCESS;
}

bool CMLink::CheckPermissions(MemAddr address, MemSize size, int perm) const
{
    return g_pMemoryDataContainer->CheckPermissions(address, size, perm);
}

void CMLink::Read(MemAddr address, void* data, MemSize size)
{
    return g_pMemoryDataContainer->Read(address, data, size);
}

void CMLink::Write(MemAddr address, const void* data, MemSize size)
{
    return g_pMemoryDataContainer->Write(address, data, size);
}

void CMLink::Reserve(MemAddr address, MemSize size, int perm)
{
    return g_pMemoryDataContainer->Reserve(address, size, perm);
}

void CMLink::Unreserve(MemAddr address)
{
    g_pMemoryDataContainer->Unreserve(address);
}

void CMLink::GetMemoryStatistics(uint64_t& nr, uint64_t& nw, uint64_t& nrb, uint64_t& nwb) const
{
    nr = g_uMemoryAccessesL;
    nrb = g_uMemoryAccessesL * g_nCacheLineSize;
    nw = g_uMemoryAccessesS;
    nwb = g_uMemoryAccessesS * g_nCacheLineSize;
};

CMLink::CMLink(const std::string& name, Object& parent, Clock& clock, const Config& config, LinkMGS* linkmgs)
  : Object(name, parent, clock),
    p_Requests("requests", delegate::create<CMLink, &CMLink::DoRequests>(*this)),
    m_requests(clock, config.getInteger<BufferSize>("CMBufferSize", INFINITE), 2),
    m_linkmgs(linkmgs),
    m_pimcallback(NULL)
{
    m_requests.Sensitive(p_Requests);
    
    // Get number of processors
    const vector<PSize> places = config.getIntegerList<PSize>("NumProcessors");
    PSize numProcs = 0;
    for (size_t i = 0; i < places.size(); ++i) {
        numProcs += places[i];
    }
    m_clients.resize(numProcs, NULL);

    m_pProcessor = NULL;
    m_nTotalReq = 0;
}

