#include "IOBus.h"

#include <sim/except.h>

namespace Simulator
{
    IIOBusClient::~IIOBusClient()
    { }

    IIOBus::~IIOBus()
    { }

    void IIOBusClient::Initialize()
    { }

    bool IIOBusClient::OnReadRequestReceived(IODeviceID from, MemAddr /*address*/, MemSize /*size*/)
    {
        throw exceptf<>("Unsupported read request received from device %u", (unsigned)from);
    }

    bool IIOBusClient::OnWriteRequestReceived(IODeviceID from, MemAddr /*address*/, const IOData& /*data*/)
    {
        throw exceptf<>("Unsupported write request received from device %u", (unsigned)from);
    }

    bool IIOBusClient::OnActiveMessageReceived(IODeviceID from, MemAddr /*address*/, Integer /*arg*/)
    {
        throw exceptf<>("Unsupported active message received from device %u", (unsigned)from);
    }

    bool IIOBusClient::OnReadResponseReceived(IODeviceID from, MemAddr /*address*/, const IOData& /*data*/)
    {
        throw exceptf<>("Unsupported read response received from device %u", (unsigned)from);
    }

    bool IIOBusClient::OnInterruptRequestReceived(IONotificationChannelID /*which*/)
    {
        return true;
    }
    bool IIOBusClient::OnNotificationReceived(IONotificationChannelID /*which*/, Integer /*tag*/)
    {
        return true;
    }


}
