#ifndef IONMUX_H
#define IONMUX_H

#ifndef PROCESSOR_H
#error This file should be included in DRISC.h
#endif

class IOBusInterface;

class IONotificationMultiplexer : public Object, public Inspect::Interface<Inspect::Read>
{
private:
    drisc::RegisterFile&            m_regFile;
    Allocator&                      m_allocator;

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

public:
    IONotificationMultiplexer(const std::string& name, Object& parent, Clock& clock, drisc::RegisterFile& rf, Allocator& alloc, size_t numChannels, Config& config);
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


#endif
