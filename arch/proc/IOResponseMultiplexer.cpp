#include "Processor.h"
#include "config.h"
#include <sstream>

namespace Simulator
{

Processor::IOResponseMultiplexer::IOResponseMultiplexer(const std::string& name, Object& parent, Clock& clock, RegisterFile& rf, size_t numDevices, Config& config)
    : Object(name, parent, clock),
      m_regFile(rf),
      m_incoming("b_incoming", *this, clock, config.getValue<BufferSize>("AsyncIOReadResponseQueueSize", 1)),
      p_dummy("dummy-process", delegate::create<IOResponseMultiplexer, &Processor::IOResponseMultiplexer::DoNothing>(*this)),
      p_IncomingReadResponses("completed-reads", delegate::create<IOResponseMultiplexer, &Processor::IOResponseMultiplexer::DoReceivedReadResponses>(*this))
{
    m_incoming.Sensitive(p_IncomingReadResponses);

    BufferSize wbqsize = config.getValue<BufferSize>("AsyncIOWritebackQueueSize", 3); 
    if (wbqsize < 3)
    {
        throw InvalidArgumentException(*this, "AsyncIOWritebackQueueSize must be at least 3 to accomodate pipeline hazards");
    }

    m_wb_buffers.resize(numDevices, 0);
    for (size_t i = 0; i < numDevices; ++i)
    {
        std::stringstream ss;
        ss << "b_writeback" << i;
        m_wb_buffers[i] = new WriteBackQueue(ss.str(), *this, clock, wbqsize);
        m_wb_buffers[i]->Sensitive(p_dummy);
    }

}
      
bool Processor::IOResponseMultiplexer::QueueWriteBackAddress(IODeviceID dev, const RegAddr& addr)
{
    assert(dev < m_wb_buffers.size());

    return m_wb_buffers[dev]->Push(addr);
}

bool Processor::IOResponseMultiplexer::OnReadResponseReceived(IODeviceID from, const IOData& data)
{
    assert(from < m_wb_buffers.size());

    IOResponse response = { from, data };

    return m_incoming.Push(response);
}

Result Processor::IOResponseMultiplexer::DoReceivedReadResponses()
{
    assert(!m_incoming.Empty());

    const IOResponse& response = m_incoming.Front();

    WriteBackQueue& wbq = *m_wb_buffers[response.device];

    // If a read response was received, there must be at least one
    // pending writeback address.
    if (wbq.Empty())
    {
        throw exceptf<SimulationException>(*this, "Unexpected read response from device %u", (unsigned)response.device);
        
    }

    const RegAddr& addr = wbq.Front();

    // Try to write to register

    if (!m_regFile.p_asyncW.Write(addr))
    {
        DeadlockWrite("Unable to acquire port to write back %s", addr.str().c_str());
        return FAILED;
    }

    RegValue regvalue;
    if (!m_regFile.ReadRegister(addr, regvalue))
    {
        DeadlockWrite("Unable to read register %s", addr.str().c_str());
        return FAILED;
    }
    
    if (regvalue.m_state == RST_FULL)
    {
        // Rare case: the request info is still in the pipeline, stall!
        DeadlockWrite("Register %s is not yet written for read completion", addr.str().c_str());
        return FAILED;
    }

    if (regvalue.m_state != RST_PENDING && regvalue.m_state != RST_WAITING)
    {
        // We're too fast, wait!
        DeadlockWrite("I/O read completed before register %s was cleared", addr.str().c_str());
        return FAILED;
    }
    
    // Now write
    Integer value = UnserializeRegister(addr.type, response.data.data, response.data.size);
    regvalue.m_state = RST_FULL;

    switch (addr.type) {
        case RT_INTEGER: regvalue.m_integer       = value; break;
        case RT_FLOAT:   regvalue.m_float.integer = value; break;
    }

    if (!m_regFile.WriteRegister(addr, regvalue, false))
    {
        DeadlockWrite("Unable to write register %s", addr.str().c_str());
        return FAILED;
    }
    
    DebugIOWrite("Completed I/O read: %#016llx -> %s",
                  (unsigned long long)value, addr.str().c_str());

    wbq.Pop();
    m_incoming.Pop();

    return SUCCESS;
}
    
}
