#include "ZLCOMA.h"
#include "memorys/processortok.h"
#include "memorys/predef.h"
#include "../sampling.h"
#include "../config.h"
using namespace std;

extern std::vector<MemSim::ProcessorTOK*> g_Links;

namespace Simulator
{

class ZLCOMA::Link : public Object
{
    SingleFlag            m_active1;
    SingleFlag            m_active2;
    IMemoryCallback*      m_callback;
    MemSim::ProcessorTOK& m_link;
    std::set<MemSim::Message*> m_requests;
    Process               p_Requests;
    unsigned int          m_nPID;
    
    Result DoRequests()
    {
        assert(m_callback != NULL);

        MemSim::Message* req = m_link.GetReply();
        if (req == NULL)
        {
            // No replies yet
            return SUCCESS;
        }
        
        // We have a reply, handle it
        switch (req->type)
        {
        case MemSim::Message::READ_REPLY:
        {
            // Read response, return the data
            MemData data;
            data.size = req->size;
            memcpy(data.data, &req->data[req->address % MemSim::g_nCacheLineSize], req->size);
            if (!m_callback->OnMemoryReadCompleted(req->address, data))
            {
                return FAILED;
            }
            break;
        }
                
        case MemSim::Message::WRITE_REPLY:
            // Write response, notify for thread
            if (req->pid != m_nPID) {
                // This WR is not for us, ignore it
                COMMIT{ m_link.RemoveReply(); }
                return SUCCESS;
            }

            if (!m_callback->OnMemoryWriteCompleted(req->tid))
            {
                return FAILED;
            }

        default:
            assert(false);
            break;
        }

        size_t left = m_requests.size();
        if (m_requests.find(req) != m_requests.end())
        {
            COMMIT{ m_requests.erase(req); }
            left--;
        }
            
        if (left == 0)
        {
            // This was the last pending request
            m_active1.Clear();
        }
            
        // We've handled the reply, remove it
        COMMIT{ m_link.RemoveReply(); }
        return SUCCESS;
    }

public:
    void RegisterClient(PSize pid, IMemoryCallback& callback, const Process* processes[])
    {
        assert(m_callback == NULL);
        m_callback = &callback;
    }

    void UnregisterClient(PSize pid)
    {
        assert(m_callback != NULL);
        m_callback = NULL;
    }

    void Read(PSize pid, MemAddr address, MemSize size)
    {
        assert(m_callback != NULL);
        assert(size == MemSim::g_nCacheLineSize);
        assert(address % MemSim::g_nCacheLineSize == 0);

        if (size > SIZE_MAX)
        {
            throw InvalidArgumentException("Size argument too big");
        }

        COMMIT
        {
            MemSim::Message *req = new MemSim::Message();
            req->pid     = m_nPID;
            req->address = address;
            req->size    = size;
            req->type    = MemSim::Message::READ;
            std::fill(req->bitmask, req->bitmask + MemSim::g_nCacheLineSize, true);
            m_link.PutRequest(req);

            m_requests.insert(req);
        }

        m_active1.Set();
    }

    void Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid)
    {
        unsigned int offset = address % MemSim::g_nCacheLineSize;

        assert(m_callback != NULL);
        assert(size <= MemSim::g_nCacheLineSize);
        assert(offset + size <= MemSim::g_nCacheLineSize);

        if (size > SIZE_MAX)
        {
            throw InvalidArgumentException("Size argument too big");
        }

        COMMIT
        {
            MemSim::Message *req = new MemSim::Message();
            req->pid     = m_nPID;
            req->address = address;
            req->size    = size;
            req->tid     = tid;
            req->type    = MemSim::Message::WRITE;
            memcpy(&req->data[offset], data, size);
            std::fill(req->bitmask, req->bitmask + MemSim::g_nCacheLineSize, false);
            std::fill(req->bitmask + offset, req->bitmask + offset + size, true);
            m_link.PutRequest(req);

            m_requests.insert(req);
        }

        m_active1.Set();
    }
    
    Link(Object& parent, unsigned int nPID, MemSim::ProcessorTOK& link)
      : Object("link", parent),
        m_active1(GetClock(), false),
        m_active2(GetClock(), false),
        m_callback(NULL),
        m_link(link),
        p_Requests("requests", delegate::create<Link, &Link::DoRequests>(*this)),
        m_nPID(nPID)
    {
        link.m_active = &m_active2;
        m_active1.Sensitive(p_Requests);
        m_active2.Sensitive(p_Requests);
    }
};

void ZLCOMA::RegisterClient(PSize pid, IMemoryCallback& callback, const Process* processes[])
{
    // Forward the registration to the link associated with the processor
    m_links[pid]->RegisterClient(pid, callback, processes);
}

void ZLCOMA::UnregisterClient(PSize pid)
{
    // Forward the unregistration to the link associated with the processor
    m_links[pid]->UnregisterClient(pid);
}

bool ZLCOMA::Read(PSize pid, MemAddr address, MemSize size)
{
    COMMIT
    {
        m_nreads++;
        m_nread_bytes += size;
    }
    
    // Forward the read to the link associated with the callback
    m_links[pid]->Read(pid, address, size);
    return true;
}

bool ZLCOMA::Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid)
{
    COMMIT
    {
        m_nwrites++;
        m_nwrite_bytes += size;
    }
    
    // Forward the write to the link associated with the callback
    m_links[pid]->Write(pid, address, data, size, tid);
    return true;
}

void ZLCOMA::Reserve(MemAddr address, MemSize size, int perm)
{
    return g_pMemoryDataContainer->Reserve(address, size, perm);
    //return VirtualMemory::Reserve(address, size, perm);
}

void ZLCOMA::Unreserve(MemAddr address)
{
    return g_pMemoryDataContainer->Unreserve(address);
    //return VirtualMemory::Unreserve(address);
}

bool ZLCOMA::Allocate(MemSize size, int perm, MemAddr& address)
{
    return g_pMemoryDataContainer->Allocate(size, perm, address);
    //return VirtualMemory::Allocate(size, perm, address);
}

void ZLCOMA::Read(MemAddr address, void* data, MemSize size)
{
    return g_pMemoryDataContainer->Read(address, data, size);
    //return VirtualMemory::Read(address, data, size);
}

void ZLCOMA::Write(MemAddr address, const void* data, MemSize size)
{
    return g_pMemoryDataContainer->Write(address, data, size);
	//return VirtualMemory::Write(address, data, size);
}

bool ZLCOMA::CheckPermissions(MemAddr address, MemSize size, int access) const
{
    return g_pMemoryDataContainer->CheckPermissions(address, size, access);
	//return VirtualMemory::CheckPermissions(address, size, access);
}

static size_t GetNumProcessors(const Config& config)
{
    const vector<PSize> placeSizes = config.getIntegerList<PSize>("NumProcessors");
    PSize numProcessors = 0;
    for (size_t i = 0; i < placeSizes.size(); ++i) {
        numProcessors += placeSizes[i];
    }
    return numProcessors;
}

ZLCOMA::ZLCOMA(const std::string& name, Simulator::Object& parent, Clock& clock, const Config& config) :
    Object(name, parent, clock),
    m_links(GetNumProcessors(config), NULL),
    m_nreads(0), m_nwrites(0), m_nread_bytes(0), m_nwrite_bytes(0)
{
    RegisterSampleVariableInObject(m_nreads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nread_bytes, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrites, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrite_bytes, SVC_CUMULATIVE);
    
    for (size_t i = 0; i < m_links.size(); ++i)
    {
        m_links[i] = new Link(*this, i, *g_Links[i]);
    }
}

ZLCOMA::~ZLCOMA()
{
}

void ZLCOMA::GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, uint64_t& nread_bytes, uint64_t& nwrite_bytes) const
{
    nreads = m_nreads;
    nwrites = m_nwrites;
    nread_bytes = m_nread_bytes;
    nwrite_bytes = m_nwrite_bytes;
}

}
