// -*- c++ -*-
#ifndef IOBUSINTERFACE_H
#define IOBUSINTERFACE_H

#include <sim/kernel.h>
#include <sim/storage.h>
#include <arch/IOBus.h>
#include <arch/drisc/forward.h>

namespace Simulator
{
namespace drisc
{

class IOBusInterface : public IIOBusClient, public Object
{
public:
    enum IORequestType
    {
        REQ_READ,
        REQ_WRITE,
        REQ_READRESPONSE
    };

    // {% from "sim/macros.p.h" import gen_struct %}
    // {% call gen_struct() %}
    ((name IORequest)
     (state
      (IODeviceID    device)
      (IORequestType type)
      (MemAddr       address)    // for all types
      (IOData        data)       // for writes & read responses
         ))
    // {% endcall %}

private:
    IOResponseMultiplexer&     m_rrmux;
    IONotificationMultiplexer& m_nmux;
    IODirectCacheAccess&       m_dca;
    IIOBus&                    m_iobus;
    IODeviceID                 m_hostid;

    void Initialize();
    Object& GetDRISCParent() const { return *GetParent()->GetParent(); }

public:
    Buffer<IORequest>          m_outgoing_reqs;

public:
    IIOBus& GetIOBus() const { return m_iobus; }

    IOBusInterface(const std::string& name, IOInterface& parent, Clock& clock, IIOBus& iobus, IODeviceID devid);

    bool SendRequest(const IORequest& request);

    Process p_OutgoingRequests;

    Result DoOutgoingRequests();


    /* From IIOBusClientCallBack */
    bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
    bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
    bool OnReadResponseReceived(IODeviceID from, MemAddr address, const IOData& data);
    bool OnInterruptRequestReceived(IONotificationChannelID which);
    bool OnNotificationReceived(IONotificationChannelID which, Integer tag);
    bool OnActiveMessageReceived(IODeviceID from, MemAddr address, Integer arg);

    StorageTraceSet GetReadRequestTraces() const;
    StorageTraceSet GetWriteRequestTraces() const;
    StorageTraceSet GetReadResponseTraces() const;
    StorageTraceSet GetInterruptRequestTraces() const;
    StorageTraceSet GetNotificationTraces() const;
    StorageTraceSet GetActiveMessageTraces() const;

    void GetDeviceIdentity(IODeviceIdentification& id) const;

    IODeviceID GetHostID() const { return m_hostid; }

    /* for debugging */
    const std::string& GetIODeviceName() const;

};

}
}

#endif
