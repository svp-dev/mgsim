// -*- c++ -*-
#ifndef IOBUSINTERFACE_H
#define IOBUSINTERFACE_H

#include <sim/kernel.h>
#include <sim/storage.h>
#include <arch/IOMessageInterface.h>
#include <arch/drisc/forward.h>

namespace Simulator
{
namespace drisc
{

class IOBusInterface : public IIOMessageClient, public Object
{
public:

    // {% from "sim/macros.p.h" import gen_struct %}
    // {% call gen_struct() %}
    ((name IORequest)
     (state
      (IODeviceID    device)
      (IOMessage*    msg)       // for writes & read responses
         ))
    // {% endcall %}

private:
    IOResponseMultiplexer&     m_rrmux;
    IONotificationMultiplexer& m_nmux;
    IODirectCacheAccess&       m_dca;
    IOMessageInterface&        m_ioif;
    IODeviceID                 m_hostid;

    void Initialize() override;
    Object& GetDRISCParent() const { return *GetParent()->GetParent(); }

public:
    Buffer<IORequest>         m_outgoing_reqs;

public:
    IOMessageInterface& GetIF() const { return m_ioif; }

    IOBusInterface(const std::string& name, IOInterface& parent,
                   IOMessageInterface& ioif, IODeviceID devid);

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
