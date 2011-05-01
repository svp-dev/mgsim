#ifndef IONMUX_H
#define IONMUX_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class IOBusInterface;

class IONotificationMultiplexer : public Object
{
private:
    RegisterFile&                   m_regFile;

    std::vector<Register<RegAddr>*> m_writebacks;
    std::vector<SingleFlag*>        m_interrupts;
    std::vector<Buffer<Integer>*>   m_notifications;

    size_t                          m_lastNotified;

public:
    IONotificationMultiplexer(const std::string& name, Object& parent, Clock& clock, RegisterFile& rf, size_t numChannels, Config& config);
    ~IONotificationMultiplexer();

    // sent by device select upon an I/O read from the processor
    bool SetWriteBackAddress(IOInterruptID which, const RegAddr& addr);

    // triggered by the IOBusInterface
    bool OnInterruptRequestReceived(IOInterruptID which);
    bool OnNotificationReceived(IOInterruptID which, Integer tag);

    Process p_IncomingNotifications;
    
    // upon interrupt received
    Result DoReceivedNotifications();
};


#endif
