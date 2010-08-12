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
    SingleFlag            m_active;
    IMemoryCallback*      m_callback;
    unsigned int          m_nRequests;
    MemSim::ProcessorTOK& m_link;
    Process               p_Requests;
    unsigned int          m_nPID;
    
    Result DoRequests()
    {
        assert(m_callback != NULL);

        MemSim::ST_request* req = m_link.GetReply();
        if (req == NULL)
        {
            // No replies yet
            return SUCCESS;
        }
        
        // We have a reply, handle it
        switch (req->type)
        {
        case MemSim::MemoryState::REQUEST_READ_REPLY:
        {
            // Read response, return the data
            MemData data;
            data.size = req->nsize;
            memcpy(data.data, &req->data[req->offset], req->nsize);
            if (!m_callback->OnMemoryReadCompleted(req->getlineaddress(), data))
            {
                return FAILED;
            }
            break;
        }
                
        case MemSim::MemoryState::REQUEST_WRITE_REPLY:
            // Write response, notify for thread
            if (req->pid != m_nPID) {
                // This WR is not for us, ignore it
                COMMIT{ m_link.RemoveReply(); }
                return SUCCESS;
            }

            if (req->bbackinv) {
                // Pop the initiator if the WR is actually broadcasted, instead of directly sent
                COMMIT{ pop_initiator(req); }
            }

            if (!m_callback->OnMemoryWriteCompleted(req->tid))
            {
                return FAILED;
            }

        case MemSim::MemoryState::REQUEST_INVALIDATE_BR:
            // Invalidation
            if (!m_callback->OnMemoryInvalidated(req->getlineaddress()))
            {
                return FAILED;
            }
            break;

        default:
            assert(false);
            break;
        }

        if (req->type != MemSim::MemoryState::REQUEST_INVALIDATE_BR)
        {
            // Decrement the request counter
            assert(m_nRequests > 0);
            if (m_nRequests == 1)
            {
                // This was the last pending request
               m_active.Clear();
            }
            COMMIT{ m_nRequests--; }
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

        if (size > SIZE_MAX)
        {
            throw InvalidArgumentException("Size argument too big");
        }

        COMMIT
        {
            MemSim::ST_request *req = new MemSim::ST_request();
            req->pid        = m_nPID;
            req->addresspre = address / MemSim::g_nCacheLineSize;
            req->offset     = address % MemSim::g_nCacheLineSize;
            req->nsize      = size;
            req->type       = MemSim::MemoryState::REQUEST_READ;
            m_link.PutRequest(req);

            m_nRequests++;
        }

        m_active.Set();
    }

    void Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid)
    {
        assert(m_callback != NULL);

        if (size > SIZE_MAX)
        {
            throw InvalidArgumentException("Size argument too big");
        }

        COMMIT
        {
            MemSim::ST_request *req = new MemSim::ST_request();
            req->pid        = m_nPID;
            req->addresspre = address / MemSim::g_nCacheLineSize;
            req->offset     = address % MemSim::g_nCacheLineSize;
            req->nsize      = size;
            req->tid        = tid;
            req->type       = MemSim::MemoryState::REQUEST_WRITE;
            memcpy(&req->data[req->offset], data, size);
            m_link.PutRequest(req);

            m_nRequests++;
        }

        m_active.Set();
    }
    
    Link(Object& parent, unsigned int nPID, MemSim::ProcessorTOK& link)
      : Object("link", parent),
        m_active(*parent.GetKernel(), false),
        m_callback(NULL),
        m_nRequests(0),
        m_link(link),
        p_Requests("requests", delegate::create<Link, &Link::DoRequests>(*this)),
        m_nPID(nPID)
    {
        m_active.Sensitive(p_Requests);
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
    return VirtualMemory::Reserve(address, size, perm);
}

void ZLCOMA::Unreserve(MemAddr address)
{
    return VirtualMemory::Unreserve(address);
}

bool ZLCOMA::Allocate(MemSize size, int perm, MemAddr& address)
{
    return VirtualMemory::Allocate(size, perm, address);
}

void ZLCOMA::Read(MemAddr address, void* data, MemSize size)
{
    return VirtualMemory::Read(address, data, size);
}

void ZLCOMA::Write(MemAddr address, const void* data, MemSize size)
{
	return VirtualMemory::Write(address, data, size);
}

bool ZLCOMA::CheckPermissions(MemAddr address, MemSize size, int access) const
{
	return VirtualMemory::CheckPermissions(address, size, access);
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

ZLCOMA::ZLCOMA(const std::string& name, Simulator::Object& parent, const Config& config) :
    Object(name, parent),
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
    
    g_pMemoryDataContainer = this;
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
