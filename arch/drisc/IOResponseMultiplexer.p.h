// -*- c++ -*-
#ifndef IORESPONSEMUX_H
#define IORESPONSEMUX_H

#include <sim/kernel.h>
#include <sim/buffer.h>
#include <arch/IOBus.h>
#include <arch/drisc/forward.h>

namespace Simulator
{
namespace drisc
{

class IOResponseMultiplexer : public Object
{
private:
    // {% from "sim/macros.p.h" import gen_struct %}
    // {% call gen_struct() %}
    ((name IOResponse)
     (state
      (IOData     data)
      (IODeviceID device)
         ))
    // {% endcall %}

public:
    Buffer<IOResponse>            m_incoming;

private:
    typedef Buffer<RegAddr>       WriteBackQueue;
    std::vector<WriteBackQueue*>  m_wb_buffers;

    Process p_dummy;
    Result DoNothing();

    Object& GetDRISCParent() const { return *GetParent()->GetParent(); };
public:
    IOResponseMultiplexer(const std::string& name, IOInterface& parent, Clock& clock, size_t numDevices);
    ~IOResponseMultiplexer();

    // sent by device select upon an I/O read from the processor
    bool QueueWriteBackAddress(IODeviceID dev, const RegAddr& addr);

    // triggered by the IOBusInterface
    bool OnReadResponseReceived(IODeviceID from, MemAddr address, const IOData& data);

    Process p_IncomingReadResponses;

    // upon data available on m_incoming
    Result DoReceivedReadResponses();

    StorageTraceSet GetWriteBackTraces() const;
};

}
}


#endif
