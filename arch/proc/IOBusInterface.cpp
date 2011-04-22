#include "Processor.h"
#include "sim/config.h"
#include <sstream>

namespace Simulator
{

    Processor::IOBusInterface::IOBusInterface(const std::string& name, Object& parent, Clock& clock, IOResponseMultiplexer& rrmux, IOInterruptMultiplexer& intmux, IIOBus& iobus, const Config& config)
        : Object(name, parent, clock),
          m_rrmux(rrmux),
          m_intmux(intmux),
          m_iobus(iobus),
          m_outgoing_reqs("b_outgoing_reqs", *this, clock, config.getValue<BufferSize>("AsyncIORequestQueueSize", 1)),
          m_outgoing_acks("b_outgoing_acks", *this, clock, config.getValue<BufferSize>("AsyncIOInterruptAckQueueSize", 1)),
          p_OutgoingRequests("outgoing-requests", delegate::create<IOBusInterface, &Processor::IOBusInterface::DoOutgoingRequests>(*this)),
          p_OutgoingInterruptAcks("outgoing-acks", delegate::create<IOBusInterface, &Processor::IOBusInterface::DoOutgoingInterruptAcks>(*this))
    {
        m_outgoing_reqs.Sensitive(p_OutgoingRequests);
        m_outgoing_acks.Sensitive(p_OutgoingInterruptAcks);
    }

    bool Processor::IOBusInterface::SendInterruptAck(IODeviceID to)
    {
        return m_outgoing_acks.Push(to);
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
            if (!m_iobus.SendWriteRequest(0, req.device, req.address, req.data))
            {
                DeadlockWrite("Unable to send I/O write request to %u:%016llx (%u)",
                              (unsigned)req.device, (unsigned long long)req.address, (unsigned)req.data.size);
                return FAILED;
            }
        }
        else
        {
            if (!m_iobus.SendReadRequest(0, req.device, req.address, req.data.size))
            {
                DeadlockWrite("Unable to send I/O read request to %u:%016llx (%u)",
                              (unsigned)req.device, (unsigned long long)req.address, (unsigned)req.data.size);
                return FAILED;
            }
        }
        
        m_outgoing_reqs.Pop();
        return SUCCESS;
    }

    Result Processor::IOBusInterface::DoOutgoingInterruptAcks()
    {
        assert(!m_outgoing_acks.Empty());

        IODeviceID dev = m_outgoing_acks.Front();

        if (!m_iobus.SendInterruptAck(0, dev))
        {
            DeadlockWrite("Unable to send interrupt acknowledgement to device %u", (unsigned)dev);
            return FAILED;
        }

        m_outgoing_acks.Pop();
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

    bool Processor::IOBusInterface::OnInterruptRequestReceived(IODeviceID from)
    {
        return m_intmux.OnInterruptRequestReceived(from);
    }

    bool Processor::IOBusInterface::OnInterruptAckReceived(IODeviceID from)
    {
        throw exceptf<SimulationException>(*this, "Unexpected interrupt acknowledgement from device %u", (unsigned)from);
    }

}
