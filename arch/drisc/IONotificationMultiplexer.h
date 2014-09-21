// -*- c++ -*-
#ifndef IONMUX_H
#define IONMUX_H

#include <sim/kernel.h>
#include <sim/inspect.h>
#include <sim/storage.h>
#include <arch/IOBus.h>
#include "forward.h"

namespace Simulator
{
namespace drisc
{

class IONotificationMultiplexer : public Object, public Inspect::Interface<Inspect::Read>
{
private:
    std::vector<Register<RegAddr>*> m_writebacks;

    StorageTraceSet GetInterruptRequestTraces() const;
    StorageTraceSet GetNotificationTraces() const;

public:
    std::vector<bool>               m_mask;
    std::vector<SingleFlag*>        m_interrupts;
    std::vector<Buffer<Integer>*>   m_notifications;
    std::vector<ArbitratedService<>*> m_services;


private:
    size_t                          m_lastNotified;

    Object& GetDRISCParent() const { return *GetParent()->GetParent(); };

public:
    IONotificationMultiplexer(const std::string& name, IOInterface& parent, Clock& clock, size_t numChannels, Config& config);
    ~IONotificationMultiplexer();

    // sent by device select upon an I/O read from the processor
    bool SetWriteBackAddress(IONotificationChannelID which, const RegAddr& addr);
    bool ConfigureChannel(IONotificationChannelID which, Integer mode);

    // triggered by the IOBusInterface
    bool OnInterruptRequestReceived(IONotificationChannelID which);
    bool OnNotificationReceived(IONotificationChannelID which, Integer tag);

    Process p_IncomingNotifications;

    // upon interrupt received
    Result DoReceivedNotifications();

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

    StorageTraceSet GetWriteBackTraces() const;

};

}
}

#endif
