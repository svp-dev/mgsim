#include "IOBus.h"
#include "except.h"

namespace Simulator
{
    IIOBusClient::~IIOBusClient()
    { }

    IIOBus::~IIOBus()
    { }

    bool IIOBusClient::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        throw exceptf<SimulationException>("Unsupported read request received from device %u", (unsigned)from);
    }

    bool IIOBusClient::OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data)
    {
        throw exceptf<SimulationException>("Unsupported write request received from device %u", (unsigned)from);
    }

    bool IIOBusClient::OnReadResponseReceived(IODeviceID from, MemAddr address, const IOData& data)
    {
        throw exceptf<SimulationException>("Unsupported read response received from device %u", (unsigned)from);
    }

    bool IIOBusClient::OnInterruptRequestReceived(IOInterruptID which)
    {
        return true;
    }
    bool IIOBusClient::OnNotificationReceived(IOInterruptID which, Integer tag)
    {
        return true;
    }


}
