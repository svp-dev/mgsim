#include "IODirectCacheAccess.h"
#include "DRISC.h"
#include <sim/config.h>
#include <cstring>

namespace Simulator
{
namespace drisc
{
    IODirectCacheAccess::IODirectCacheAccess(const std::string& name, IOInterface& parent, Clock& clock, Config& config)
        : Object(name, parent, clock),
          m_memory(NULL),
          m_mcid(0),
          m_busif(parent.GetIOBusInterface()),
          m_lineSize(config.getValue<MemSize>("CacheLineSize")),
          m_requests("b_requests", *this, clock, config.getValue<BufferSize>(*this, "RequestQueueSize")),
          m_responses("b_responses", *this, clock, config.getValue<BufferSize>(*this, "ResponseQueueSize")),
          m_pending_writes(0),
          m_outstanding_address(0),
          m_outstanding_size(0),
          m_outstanding_client(0),
          m_has_outstanding_request(false),
          m_flushing(false),
          p_MemoryOutgoing(*this, "send-memory-requests", delegate::create<IODirectCacheAccess, &IODirectCacheAccess::DoMemoryOutgoing>(*this)),
          p_BusOutgoing   (*this, "send-bus-responses", delegate::create<IODirectCacheAccess, &IODirectCacheAccess::DoBusOutgoing>(*this)),
          p_service(*this, clock, "p_service")
    {
        p_service.AddProcess(p_BusOutgoing);
        p_service.AddProcess(p_MemoryOutgoing);
        m_responses.Sensitive(p_BusOutgoing);
        m_requests.Sensitive(p_MemoryOutgoing);

        p_BusOutgoing.SetStorageTraces(opt(m_busif.m_outgoing_reqs));
    }

    void IODirectCacheAccess::ConnectMemory(IMemory* memory)
    {
        assert(m_memory == NULL);
        assert(memory != NULL); // can't register two times

        m_memory = memory;
        StorageTraceSet traces;
        m_mcid = m_memory->RegisterClient(*this, p_MemoryOutgoing, traces, m_responses, true);
        p_MemoryOutgoing.SetStorageTraces(opt(traces ^ m_responses));
    }

    IODirectCacheAccess::~IODirectCacheAccess()
    {
        assert(m_memory != NULL);
        m_memory->UnregisterClient(m_mcid);
    }

    bool IODirectCacheAccess::QueueRequest(const Request& req)
    {
        size_t offset = (size_t)(req.address % m_lineSize);
        if (offset + req.size > m_lineSize)
        {
            throw exceptf<InvalidArgumentException>(*this, "DCA request for %#016llx/%u (dev %u, type %d) crosses over cache line boundary",
                                                    (unsigned long long)req.address, (unsigned)req.size, (unsigned)req.client, (int)req.type);
        }

        auto& cpu = GetDRISC();
        if (req.type != FLUSH && !cpu.CheckPermissions(req.address, req.size, (req.type == WRITE) ? IMemory::PERM_DCA_WRITE : IMemory::PERM_DCA_READ))
        {
            throw exceptf<SecurityException>(*this, "Invalid access in DCA request for %#016llx/%u (dev %u, type %d)",
                                             (unsigned long long)req.address, (unsigned)req.size, (unsigned)req.client, (int)req.type);
        }

        if (!m_requests.Push(req))
        {
            DeadlockWrite("Unable to queue DCA request (%#016llx/%u, dev %u, type %d",
                          (unsigned long long)req.address, (unsigned)req.size, (unsigned)req.client, (int)req.type);
            return false;
        }

        DebugIOWrite("Queued DCA request (%#016llx/%u, dev %u, type %d)",
                     (unsigned long long)req.address, (unsigned)req.size, (unsigned)req.client, (int)req.type);

        return true;
    }

    bool IODirectCacheAccess::OnMemoryWriteCompleted(TID tid)
    {
        if (tid == INVALID_TID) // otherwise for D-Cache
        {
            Response res;
            res.address = 0;
            res.size = 0;
            if (!m_responses.Push(res))
            {
                DeadlockWrite("Unable to push memory write response");
                return false;
            }
        }
        return true;
    }

    bool IODirectCacheAccess::OnMemoryReadCompleted(MemAddr addr, const char * data)
    {
        assert(addr % m_lineSize == 0);

        Response res;
        res.address = addr;
        res.size = m_lineSize;
        std::copy(data, data + m_lineSize, res.data);

        if (!m_responses.Push(res))
        {
            DeadlockWrite("Unable to push memory read response (%#016llx)",
                          (unsigned long long)addr);

            return false;
        }
        return true;
    }

    bool IODirectCacheAccess::OnMemorySnooped(MemAddr /*unused*/, const char* /*data*/, const bool* /*mask*/)
    {
        return true;
    }

    bool IODirectCacheAccess::OnMemoryInvalidated(MemAddr /*unused*/)
    {
        return true;
    }

    Object& IODirectCacheAccess::GetMemoryPeer()
    {
        return GetDRISC();
    }

    Result IODirectCacheAccess::DoBusOutgoing()
    {
        const Response& res = m_responses.Front();


        if (!p_service.Invoke())
        {
            DeadlockWrite("Unable to acquire port for DCA read response (%#016llx, %u)",
                          (unsigned long long)res.address, (unsigned)res.size);

            return FAILED;
        }

        if (res.address == 0 && res.size == 0)
        {
            // write response
            assert(m_pending_writes > 0);

            if (m_pending_writes == 1 && m_flushing == true)
            {
                COMMIT {
                    --m_pending_writes;
                    m_flushing = false;
                }
                // the flush response will be sent below.
            }
            else
            {
                COMMIT {
                    --m_pending_writes;
                }
                // one or more outstanding write left, no flush response needed
                m_responses.Pop();
                return SUCCESS;
            }
        }

        if (m_has_outstanding_request
            && res.address <= m_outstanding_address
            && res.address + res.size >= m_outstanding_address + m_outstanding_size)
        {
            IOBusInterface::IORequest req;
            req.device = m_outstanding_client;
            req.type = IOBusInterface::REQ_READRESPONSE;
            req.address = m_outstanding_address;
            req.data.size = m_outstanding_size;

            memcpy(req.data.data, res.data + (m_outstanding_address - res.address), m_outstanding_size);

            if (!m_busif.SendRequest(req))
            {
                DeadlockWrite("Unable to send DCA read response to client %u for %#016llx/%u",
                              (unsigned)req.device, (unsigned long long)req.address, (unsigned)req.data.size);
                return FAILED;
            }

            DebugIOWrite("Sent DCA read response to client %u for %#016llx/%u",
                         (unsigned)req.device, (unsigned long long)req.address, (unsigned)req.data.size);

            COMMIT {
                m_has_outstanding_request = false;
            }
        }

        m_responses.Pop();

        return SUCCESS;
    }

    Result IODirectCacheAccess::DoMemoryOutgoing()
    {
        assert(m_memory != NULL);
        const Request& req = m_requests.Front();

        switch(req.type)
        {
        case FLUSH:
        {
            if (!p_service.Invoke())
            {
                DeadlockWrite("Unable to acquire port for DCA flush");
                return FAILED;
            }

            if (m_has_outstanding_request)
            {
                // some request is already queued, so we just stall
                DeadlockWrite("Will not send additional DCA flush request from client %u, already waiting for request for dev %u",
                              (unsigned)req.client, (unsigned)m_outstanding_client);
                return FAILED;
            }

            if (m_pending_writes == 0)
            {
                // no outstanding write, fake one and then
                // acknowledge immediately

                COMMIT { ++m_pending_writes; }

                Response res;
                res.address = 0;
                res.size = 0;
                if (!m_responses.Push(res))
                {
                    DeadlockWrite("Unable to push DCA flush response");
                    return FAILED;
                }
            }

            COMMIT {
                m_flushing = true;
                m_has_outstanding_request = true;
                m_outstanding_client = req.client;
                m_outstanding_address = 0;
                m_outstanding_size = 0;
            }

            break;
        }
        case READ:
        {
            // this is a read request coming from the bus.

            if (req.size > m_lineSize || req.size > MAX_MEMORY_OPERATION_SIZE)
            {
                throw InvalidArgumentException("Read size is too big");
            }

            if (req.address / m_lineSize != (req.address + req.size - 1) / m_lineSize)
            {
                throw InvalidArgumentException("Read request straddles cache-line boundary");
            }


            if (!p_service.Invoke())
            {
                DeadlockWrite("Unable to acquire port for DCA read (%#016llx, %u)",
                              (unsigned long long)req.address, (unsigned)req.size);

                return FAILED;
            }

            if (m_has_outstanding_request)
            {
                // some request is already queued, so we just stall
                DeadlockWrite("Will not send additional DCA read request from client %u for %#016llx/%u, already waiting for %#016llx/%u, dev %u",
                              (unsigned)req.client, (unsigned long long)req.address, (unsigned)req.size,
                              (unsigned long long)m_outstanding_address, (unsigned)m_outstanding_size, (unsigned)m_outstanding_client);
                return FAILED;
            }

            // send the request to the memory
            MemAddr line_address  = req.address & -m_lineSize;
            if (!m_memory->Read(m_mcid, line_address))
            {
                DeadlockWrite("Unable to send DCA read from %#016llx/%u, dev %u to memory", (unsigned long long)req.address, (unsigned)req.size, (unsigned)req.client);
                return FAILED;
            }

            COMMIT {
                m_has_outstanding_request = true;
                m_outstanding_client = req.client;
                m_outstanding_address = req.address;
                m_outstanding_size = req.size;
            }

            break;
        }
        case WRITE:
        {
            // write operation

            if (req.size > m_lineSize || req.size > MAX_MEMORY_OPERATION_SIZE)
            {
                throw InvalidArgumentException("Write size is too big");
            }

            if (req.address / m_lineSize != (req.address + req.size - 1) / m_lineSize)
            {
                throw InvalidArgumentException("Write request straddles cache-line boundary");
            }

            if (!p_service.Invoke())
            {
                DeadlockWrite("Unable to acquire port for DCA write (%#016llx, %u)",
                              (unsigned long long)req.address, (unsigned)req.size);

                return FAILED;
            }

            MemAddr line_address  = req.address & -m_lineSize;
            size_t offset = req.address - line_address;

            MemData mdata;
            COMMIT{
                std::copy(req.data, req.data + req.size, mdata.data + offset);
                std::fill(mdata.mask, mdata.mask + offset, false);
                std::fill(mdata.mask + offset, mdata.mask + offset + req.size, true);
                std::fill(mdata.mask + offset + req.size, mdata.mask + m_lineSize, false);
            }

            if (!m_memory->Write(m_mcid, line_address, mdata, INVALID_WCLIENTID))
            {
                DeadlockWrite("Unable to send DCA write to %#016llx/%u to memory", (unsigned long long)req.address, (unsigned)req.size);
                return FAILED;
            }

            COMMIT { ++m_pending_writes; }

            break;
        }
        }

        m_requests.Pop();
        return SUCCESS;

    }


}
}
