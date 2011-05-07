#include "Processor.h"
#include "sim/config.h"
#include <cstring>

namespace Simulator
{
    Processor::IODirectCacheAccess::IODirectCacheAccess(const std::string& name, Object& parent, Clock& clock, Clock& busclock, Processor& proc, IOBusInterface& busif, Config& config)
        : Object(name, parent, clock),
          m_cpu(proc),
          m_busif(busif),
          m_lineSize(config.getValue<MemSize>("CacheLineSize")),
          m_requests("b_requests", *this, clock, config.getValue<BufferSize>(*this, "RequestQueueSize")),
          m_responses("b_responses", *this, busclock, config.getValue<BufferSize>(*this, "ResponseQueueSize")),
          m_has_outstanding_request(false),
          p_MemoryOutgoing("send-memory-requests", delegate::create<IODirectCacheAccess, &Processor::IODirectCacheAccess::DoMemoryOutgoing>(*this)),
          p_BusOutgoing("send-bus-responses", delegate::create<IODirectCacheAccess, &Processor::IODirectCacheAccess::DoBusOutgoing>(*this)),
          p_service(*this, clock, "p_service")
    {
        p_service.AddProcess(p_MemoryOutgoing);
        p_service.AddProcess(p_BusOutgoing);
        m_requests.Sensitive(p_MemoryOutgoing);
        m_responses.Sensitive(p_BusOutgoing);
    }
    
    bool Processor::IODirectCacheAccess::QueueRequest(const Request& req)
    {
        size_t offset = (size_t)(req.address % m_lineSize);
        if (offset + req.data.size > m_lineSize)
        {
            throw exceptf<InvalidArgumentException>(*this, "DCA request for %#016llx/%u (dev %u, write %d) crosses over cache line boundary", 
                                                    (unsigned long long)req.address, (unsigned)req.data.size, (unsigned)req.client, (int)req.write);
        }

        if (!m_cpu.CheckPermissions(req.address, req.data.size, req.write ? IMemory::PERM_DCA_WRITE : IMemory::PERM_DCA_READ))
        {
            throw exceptf<SecurityException>(*this, "Invalid access in DCA request for %#016llx/%u (dev %u, write %d)",
                                             (unsigned long long)req.address, (unsigned)req.data.size, (unsigned)req.client, (int)req.write);
        }

        if (!m_requests.Push(req))
        {
            DeadlockWrite("Unable to queue DCA request (%#016llx/%u, dev %u, write %d",
                          (unsigned long long)req.address, (unsigned)req.data.size, (unsigned)req.client, (int)req.write);
            return false;
        }

        return true;
    }


    bool Processor::IODirectCacheAccess::OnMemoryReadCompleted(MemAddr addr, const MemData& data)
    {
        assert(addr % m_lineSize == 0);
        assert(data.size == m_lineSize);

        Response res;
        res.address = addr;
        res.data = data;
        if (!m_responses.Push(res))
        {
            DeadlockWrite("Unable to push memory read response (%#016llx, %u)",
                          (unsigned long long)addr, (unsigned)data.size);
            
            return false;
        }
        return true;
    }

    Result Processor::IODirectCacheAccess::DoBusOutgoing()
    {
        const Response& res = m_responses.Front();


        if (!p_service.Invoke())
        {
            DeadlockWrite("Unable to acquire port for DCA read response (%#016llx, %u)",
                          (unsigned long long)res.address, (unsigned)res.data.size);
            
            return FAILED;
        }

        if (m_has_outstanding_request 
            && res.address <= m_outstanding_address
            && res.address + res.data.size >= m_outstanding_address + m_outstanding_size)
        {
            IOBusInterface::IORequest req;
            req.device = m_outstanding_client;
            req.type = IOBusInterface::REQ_READRESPONSE;
            req.address = m_outstanding_address;
            req.data.size = m_outstanding_size;
            memcpy(req.data.data, res.data.data + (m_outstanding_address - res.address), m_outstanding_size);
            
            if (!m_busif.SendRequest(req))
            {
                DeadlockWrite("Unable to send DCA read response to client %u for %#016llx/%u",
                              (unsigned)req.device, (unsigned long long)req.address, (unsigned)req.data.size);
                return FAILED;
            }
            
            COMMIT {
                m_has_outstanding_request = false;
            }
        }

        m_responses.Pop();

        return SUCCESS;
    }

    Result Processor::IODirectCacheAccess::DoMemoryOutgoing()
    {
        const Request& req = m_requests.Front();

        if (!req.write)
        {
            // this is a read request coming from the bus.

            if (!p_service.Invoke())
            {
                DeadlockWrite("Unable to acquire port for DCA read (%#016llx, %u)",
                              (unsigned long long)req.address, (unsigned)req.data.size);
                
                return FAILED;
            }

            if (m_has_outstanding_request)
            {
                // some request is already queued, so we just stall
                DeadlockWrite("Will not send additional DCA read request from client %u for %#016llx/%u, already waiting for %#016llx/%u, dev %u",
                              (unsigned)req.client, (unsigned long long)req.address, (unsigned)req.data.size, 
                              (unsigned long long)m_outstanding_address, (unsigned)m_outstanding_size, (unsigned)m_outstanding_client);
                return FAILED;
            }
            
            // send the request to the memory
            MemAddr line_address  = req.address & -m_lineSize;
            if (!m_cpu.ReadMemory(line_address, m_lineSize))
            {
                DeadlockWrite("Unable to send DCA read from %#016llx/%u, dev %u to memory", (unsigned long long)req.address, (unsigned)req.data.size, (unsigned)req.client);
                return FAILED;
            }

            COMMIT {
                m_has_outstanding_request = true;
                m_outstanding_client = req.client;
                m_outstanding_address = req.address;
                m_outstanding_size = req.data.size;
            }
        }
        else
        {
            // write operation: just write through

            if (!m_cpu.WriteMemory(req.address, req.data.data, req.data.size, 0))
            {
                DeadlockWrite("Unable to send DCA write to %#016llx/%u to memory", (unsigned long long)req.address, (unsigned)req.data.size);
                return FAILED;
            }

        }

        m_requests.Pop();
        return SUCCESS;

    }


}
