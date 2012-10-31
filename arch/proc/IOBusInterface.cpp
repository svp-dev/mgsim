#include "Processor.h"
#include <sim/config.h>
#include <sstream>

namespace Simulator
{

    Processor::IOBusInterface::IOBusInterface(const std::string& name, IOInterface& parent, Clock& clock, IOResponseMultiplexer& rrmux, IONotificationMultiplexer& nmux, IODirectCacheAccess& dca, IIOBus& iobus, IODeviceID devid, Config& config)
        : Object(name, parent, clock),
          m_rrmux(rrmux),
          m_nmux(nmux),
          m_dca(dca),
          m_iobus(iobus),
          m_hostid(devid),
          m_outgoing_reqs("b_outgoing_reqs", *this, clock, config.getValue<BufferSize>(*this, "OutgoingRequestQueueSize")),
          p_OutgoingRequests(*this, "outgoing-requests", delegate::create<IOBusInterface, &Processor::IOBusInterface::DoOutgoingRequests>(*this))
    {
        if (m_outgoing_reqs.GetMaxSize() < 3)
        {
            throw InvalidArgumentException(*this, "OutgoingRequestQueueSize must be at least 3 to accomodate pipeline hazards");
        }
        iobus.RegisterClient(devid, *this);
        m_outgoing_reqs.Sensitive(p_OutgoingRequests);
    }

    void Processor::IOBusInterface::Initialize()
    {
        p_OutgoingRequests.SetStorageTraces(
            m_iobus.GetWriteRequestTraces() ^
            m_iobus.GetReadRequestTraces(m_hostid) ^
            m_iobus.GetReadResponseTraces() );
    }

    bool Processor::IOBusInterface::SendRequest(const IORequest& request)
    {
        return m_outgoing_reqs.Push(request);
    }

    Result Processor::IOBusInterface::DoOutgoingRequests()
    {
        assert(!m_outgoing_reqs.Empty());

        const IORequest& req = m_outgoing_reqs.Front();

        switch(req.type)
        {
        case REQ_WRITE:
            if (!m_iobus.SendWriteRequest(m_hostid, req.device, req.address, req.data))
            {
                DeadlockWrite("Unable to send I/O write request to %u:%016llx (%u)",
                              (unsigned)req.device, (unsigned long long)req.address, (unsigned)req.data.size);
                return FAILED;
            }
            break;
        case REQ_READ:
            if (!m_iobus.SendReadRequest(m_hostid, req.device, req.address, req.data.size))
            {
                DeadlockWrite("Unable to send I/O read request to %u:%016llx (%u)",
                              (unsigned)req.device, (unsigned long long)req.address, (unsigned)req.data.size);
                return FAILED;
            }
            break;
        case REQ_READRESPONSE:
            if (!m_iobus.SendReadResponse(m_hostid, req.device, req.address, req.data))
            {
                DeadlockWrite("Unable to send DCA read response to %u:%016llx (%u)",
                              (unsigned)req.device, (unsigned long long)req.address, (unsigned)req.data.size);
                return FAILED;
            }
            break;
        };

        m_outgoing_reqs.Pop();
        return SUCCESS;
    }

    bool Processor::IOBusInterface::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        IODirectCacheAccess::Request req;
        req.client = from;
        req.address = address;
        req.type = (address == 0 && size == 0) ? IODirectCacheAccess::FLUSH : IODirectCacheAccess::READ;
        req.size = size;
        return m_dca.QueueRequest(req);
    }

    StorageTraceSet Processor::IOBusInterface::GetReadRequestTraces() const
    {
        return m_dca.m_requests;
    }

    bool Processor::IOBusInterface::OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data)
    {
        if (data.size > MAX_MEMORY_OPERATION_SIZE)
        {
            throw exceptf<InvalidArgumentException>(*this, "Write request for %#016llx/%u from client %u is too large",
                                                    (unsigned long long)address, (unsigned)data.size, (unsigned)from);
        }

        IODirectCacheAccess::Request req;
        req.client = from;
        req.address = address;
        req.type = IODirectCacheAccess::WRITE;
        memcpy(req.data, data.data, data.size);
        req.size = data.size;
        return m_dca.QueueRequest(req);
    }

    StorageTraceSet Processor::IOBusInterface::GetWriteRequestTraces() const
    {
        return m_dca.m_requests;
    }

    bool Processor::IOBusInterface::OnReadResponseReceived(IODeviceID from, MemAddr address, const IOData& data)
    {
        return m_rrmux.OnReadResponseReceived(from, address, data);
    }

    StorageTraceSet Processor::IOBusInterface::GetReadResponseTraces() const
    {
        return m_rrmux.m_incoming;
    }

    bool Processor::IOBusInterface::OnInterruptRequestReceived(IONotificationChannelID which)
    {
        return m_nmux.OnInterruptRequestReceived(which);
    }

    StorageTraceSet Processor::IOBusInterface::GetInterruptRequestTraces() const
    {
        StorageTraceSet res;
        for (std::vector<SingleFlag*>::const_iterator p = m_nmux.m_interrupts.begin(); p != m_nmux.m_interrupts.end(); ++p)
        {
            res ^= opt(*(*p));
        }
        return res;
    }

    bool Processor::IOBusInterface::OnNotificationReceived(IONotificationChannelID which, Integer tag)
    {
        return m_nmux.OnNotificationReceived(which, tag);
    }

    StorageTraceSet Processor::IOBusInterface::GetNotificationTraces() const
    {
        StorageTraceSet res;
        for (std::vector<Buffer<Integer>*>::const_iterator p = m_nmux.m_notifications.begin(); p != m_nmux.m_notifications.end(); ++p)
        {
            res ^= opt(*(*p));
        }
        return res;
    }

    void Processor::IOBusInterface::GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "CPU", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }
    }

}
