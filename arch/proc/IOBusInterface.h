#ifndef IOBUSINTERFACE_H
#define IOBUSINTERFACE_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class IOResponseMultiplexer;
class IONotificationMultiplexer;
class IODirectCacheAccess;
class IOInterface;

class IOBusInterface : public IIOBusClient, public Object
{
public:
    enum IORequestType
    {
        REQ_READ,
        REQ_WRITE,
        REQ_READRESPONSE
    };

    struct IORequest
    {
        IODeviceID    device;
        IORequestType type;
        MemAddr       address;    // for all types
        IOData        data;       // for writes & read responses
    };

private:
    IOResponseMultiplexer&     m_rrmux;
    IONotificationMultiplexer& m_nmux;
    IODirectCacheAccess&       m_dca;
    IIOBus&                    m_iobus;
    IODeviceID                 m_hostid;

    Buffer<IORequest>          m_outgoing_reqs;

public:
    Processor& GetProcessor() const { return dynamic_cast<Processor&>(*GetParent()->GetParent()); }

    IOBusInterface(const std::string& name, IOInterface& parent, Clock& clock, IOResponseMultiplexer& rrmux, IONotificationMultiplexer& nmux, IODirectCacheAccess& dca, IIOBus& iobus, IODeviceID devid, Config& config);

    bool SendRequest(const IORequest& request);
    
    Process p_OutgoingRequests;

    Result DoOutgoingRequests();


    /* From IIOBusClientCallBack */
    bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
    bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
    bool OnReadResponseReceived(IODeviceID from, MemAddr address, const IOData& data);
    bool OnInterruptRequestReceived(IOInterruptID which);
    bool OnNotificationReceived(IOInterruptID which, Integer tag);

    void GetDeviceIdentity(IODeviceIdentification& id) const;
    
    IODeviceID GetHostID() const { return m_hostid; }

    /* for debugging */
    std::string GetIODeviceName() const { return GetFQN(); };

};


#endif
