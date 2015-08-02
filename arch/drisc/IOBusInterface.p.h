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

    void Initialize() override;
    Object& GetDRISCParent() const { return *GetParent()->GetParent(); }

public:
    Buffer<IORequest>          m_outgoing_reqs;

public:
    IIOBus& GetIOBus() const { return m_iobus; }

    IOBusInterface(const std::string& name, IOInterface& parent, Clock& clock, IIOBus& iobus, IODeviceID devid);

    bool SendRequest(IORequest&& msg);

    Process p_OutgoingRequests;

    Result DoOutgoingRequests();

    /* From IIOBusClientCallBack */
    bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size) override;
    bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data) override;
    bool OnReadResponseReceived(IODeviceID from, MemAddr address, const IOData& data) override;
    bool OnInterruptRequestReceived(IONotificationChannelID which) override;
    bool OnNotificationReceived(IONotificationChannelID which, Integer tag) override;
    bool OnActiveMessageReceived(IODeviceID from, MemAddr address, Integer arg) override;

    StorageTraceSet GetReadRequestTraces() const override;
    StorageTraceSet GetWriteRequestTraces() const override;
    StorageTraceSet GetReadResponseTraces() const override;
    StorageTraceSet GetInterruptRequestTraces() const override;
    StorageTraceSet GetNotificationTraces() const override;
    StorageTraceSet GetActiveMessageTraces() const override;

    void GetDeviceIdentity(IODeviceIdentification& id) const override;

    IODeviceID GetHostID() const { return m_hostid; }

    /* for debugging */
    const std::string& GetIODeviceName() const override;

};

}
}

#endif
