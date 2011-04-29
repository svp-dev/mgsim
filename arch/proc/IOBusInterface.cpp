#include "Processor.h"
#include "sim/config.h"
#include <sstream>

namespace Simulator
{
    
    Processor::IOBusInterface::IOBusInterface(const std::string& name, Object& parent, Clock& clock, IOResponseMultiplexer& rrmux, IOInterruptMultiplexer& intmux, IIOBus& iobus, IODeviceID devid, Config& config)
        : Object(name, parent, clock),
          m_rrmux(rrmux),
          m_intmux(intmux),
          m_iobus(iobus),
          m_hostid(devid),
          m_outgoing_reqs("b_outgoing_reqs", *this, clock, config.getValue<BufferSize>(*this,"AsyncIORequestQueueSize", 0)),
          p_OutgoingRequests("outgoing-requests", delegate::create<IOBusInterface, &Processor::IOBusInterface::DoOutgoingRequests>(*this))
    {
        if (m_outgoing_reqs.GetMaxSize() < 3)
        {
            throw InvalidArgumentException(*this, "AsyncIORequestQueueSize must be at least 3 to accomodate pipeline hazards");
        }
        iobus.RegisterClient(devid, *this);
        m_outgoing_reqs.Sensitive(p_OutgoingRequests);
    }

    bool Processor::IOBusInterface::SendRequest(const IORequest& request)
    {
        return m_outgoing_reqs.Push(request);
    }

    Result Processor::IOBusInterface::DoOutgoingRequests()
    {
        assert(!m_outgoing_reqs.Empty());

        const IORequest& req = m_outgoing_reqs.Front();

        if (req.write)
        {
            if (!m_iobus.SendWriteRequest(m_hostid, req.device, req.address, req.data))
            {
                DeadlockWrite("Unable to send I/O write request to %u:%016llx (%u)",
                              (unsigned)req.device, (unsigned long long)req.address, (unsigned)req.data.size);
                return FAILED;
            }
        }
        else
        {
            if (!m_iobus.SendReadRequest(m_hostid, req.device, req.address, req.data.size))
            {
                DeadlockWrite("Unable to send I/O read request to %u:%016llx (%u)",
                              (unsigned)req.device, (unsigned long long)req.address, (unsigned)req.data.size);
                return FAILED;
            }
        }
        
        m_outgoing_reqs.Pop();
        return SUCCESS;
    }

    bool Processor::IOBusInterface::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        // FIXME: This should go to direct cache access
        throw exceptf<SimulationException>(*this, "Unsupported I/O read request from device %u: %016llx (%u)", 
                                           (unsigned)from, (unsigned long long)address, (unsigned) size);
    }

    bool Processor::IOBusInterface::OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data)
    {
        // FIXME: This should go to direct cache access
        throw exceptf<SimulationException>(*this, "Unsupported I/O write request from device %u: %016llx (%u)", 
                                           (unsigned)from, (unsigned long long)address, (unsigned)data.size);
    }

    bool Processor::IOBusInterface::OnReadResponseReceived(IODeviceID from, const IOData& data)
    {
        return m_rrmux.OnReadResponseReceived(from, data);
    }

    bool Processor::IOBusInterface::OnInterruptRequestReceived(IOInterruptID which)
    {
        return m_intmux.OnInterruptRequestReceived(which);
    }

    void Processor::IOBusInterface::GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "CPU", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }    
    }

}
