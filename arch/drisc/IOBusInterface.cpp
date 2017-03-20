#include "IOInterface.h"
#include "DRISC.h"
#include <sim/config.h>
#include <sstream>
#include <cstring>

namespace Simulator
{
namespace drisc
{

    IOBusInterface::IOBusInterface(const std::string& name, IOInterface& parent,
                                   IOMessageInterface& ioif, IODeviceID devid)
        : Object(name, parent),
          m_rrmux(parent.GetReadResponseMultiplexer()),
          m_nmux(parent.GetNotificationMultiplexer()),
          m_dca(parent.GetDirectCacheAccess()),
          m_ioif(ioif),
          m_hostid(devid),
          InitBuffer(m_outgoing_reqs, ioif.RegisterClient(devid, *this, true), "OutgoingRequestQueueSize"),
          InitProcess(p_OutgoingRequests, DoOutgoingRequests)
    {
        if (m_outgoing_reqs.GetMaxSize() < 3)
        {
            throw InvalidArgumentException(*this, "OutgoingRequestQueueSize must be at least 3 to accomodate pipeline hazards");
        }
        m_outgoing_reqs.Sensitive(p_OutgoingRequests);
        ioif.GetIC().ConnectSender(ioif.GetSenderKey(devid), p_OutgoingRequests);
    }

    void IOBusInterface::Initialize()
    {
        p_OutgoingRequests.SetStorageTraces(m_ioif.GetRequestTraces(m_hostid));
    }

    bool IOBusInterface::SendRequest(IORequest&& request)
    {
        return m_outgoing_reqs.Push(std::move(request));
    }

    Result IOBusInterface::DoOutgoingRequests()
    {
        assert(!m_outgoing_reqs.Empty());

        const IORequest& req = m_outgoing_reqs.Front();
        if (!m_ioif.SendMessage(m_hostid, req.device, req.msg))
        {
            DeadlockWrite("Unable to send I/O request to %u",
                          (unsigned)req.device);
                return FAILED;
        }

        m_outgoing_reqs.Pop();
        return SUCCESS;
    }

    bool IOBusInterface::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        IODirectCacheAccess::Request req;
        req.client = from;
        req.address = address;
        req.type = (address == 0 && size == 0) ? IODirectCacheAccess::FLUSH : IODirectCacheAccess::READ;
        req.size = size;
        return m_dca.QueueRequest(std::move(req));
    }

    StorageTraceSet IOBusInterface::GetReadRequestTraces() const
    {
        return m_dca.m_requests;
    }

    bool IOBusInterface::OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data)
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
        COMMIT{ memcpy(req.data, data.data, data.size); }
        req.size = data.size;
        return m_dca.QueueRequest(std::move(req));
    }

    StorageTraceSet IOBusInterface::GetWriteRequestTraces() const
    {
        return m_dca.m_requests;
    }

    bool IOBusInterface::OnReadResponseReceived(IODeviceID from, MemAddr address, const IOData& data)
    {
        return m_rrmux.OnReadResponseReceived(from, address, data);
    }

    StorageTraceSet IOBusInterface::GetReadResponseTraces() const
    {
        return m_rrmux.m_incoming;
    }

    bool IOBusInterface::OnInterruptRequestReceived(IONotificationChannelID which)
    {
        return m_nmux.OnInterruptRequestReceived(which);
    }

    StorageTraceSet IOBusInterface::GetInterruptRequestTraces() const
    {
        StorageTraceSet res;
        for (std::vector<Flag      *>::const_iterator p = m_nmux.m_interrupts.begin(); p != m_nmux.m_interrupts.end(); ++p)
        {
            res ^= **p;
        }
        return opt(res);
    }

    bool IOBusInterface::OnNotificationReceived(IONotificationChannelID which, Integer tag)
    {
        return m_nmux.OnNotificationReceived(which, tag);
    }

    StorageTraceSet IOBusInterface::GetNotificationTraces() const
    {
        StorageTraceSet res;
        for (std::vector<Buffer<Integer>*>::const_iterator p = m_nmux.m_notifications.begin(); p != m_nmux.m_notifications.end(); ++p)
        {
            res ^= opt(*(*p));
        }
        return res;
    }

    bool IOBusInterface::OnActiveMessageReceived(IODeviceID /*from*/, MemAddr addr, Integer arg)
    {
        return GetDRISC().Boot(addr, !!arg);
    }

    StorageTraceSet IOBusInterface::GetActiveMessageTraces() const
    {
        StorageTraceSet res;
        res = GetDRISC().GetAllocator().m_allocRequestsNoSuspend;
        return res;
    }

    void IOBusInterface::GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "CPU", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }
    }

    const std::string& IOBusInterface::GetIODeviceName() const
    {
        return GetName();
    }

}
}
