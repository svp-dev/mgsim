#ifndef IOBUSINTERFACE_H
#define IOBUSINTERFACE_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class IOResponseMultiplexer;
class IONotificationMultiplexer;

class IOBusInterface : public IIOBusClient, public Object
{
public:
    struct IORequest
    {
        IODeviceID  device;
        MemAddr     address;    // for reads&writes
        bool        write;
        IOData      data;       // for writes
    };

private:
    IOResponseMultiplexer&     m_rrmux;
    IONotificationMultiplexer& m_nmux;
    IIOBus&                    m_iobus;
    IODeviceID                 m_hostid;

    Buffer<IORequest>          m_outgoing_reqs;

public:
    IOBusInterface(const std::string& name, Object& parent, Clock& clock, IOResponseMultiplexer& rrmux, IONotificationMultiplexer& nmux, IIOBus& iobus, IODeviceID devid, Config& config);

    bool SendInterruptAck(IODeviceID to);

    bool SendRequest(const IORequest& request);
    
    Process p_OutgoingRequests;

    Result DoOutgoingRequests();


    /* From IIOBusClientCallBack */
    bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
    bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
    bool OnReadResponseReceived(IODeviceID from, const IOData& data);
    bool OnInterruptRequestReceived(IOInterruptID which);
    bool OnNotificationReceived(IOInterruptID which, Integer tag);

    void GetDeviceIdentity(IODeviceIdentification& id) const;
    
    IODeviceID GetHostID() const { return m_hostid; }

    /* for debugging */
    std::string GetIODeviceName() const { return GetFQN(); };

};


#endif
