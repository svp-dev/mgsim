#ifndef IORESPONSEMUX_H
#define IORESPONSEMUX_H

#ifndef PROCESSOR_H
#error This file should be included in DRISC.h
#endif

class IOResponseMultiplexer : public Object
{
private:
    drisc::RegisterFile&         m_regFile;
    Allocator&                   m_allocator;

    struct IOResponse
    {
        IOData      data;
        IODeviceID  device;
    };

public:
    Buffer<IOResponse>            m_incoming;

private:
    typedef Buffer<RegAddr>       WriteBackQueue;
    std::vector<WriteBackQueue*>  m_wb_buffers;

    Process p_dummy;
    Result DoNothing() { COMMIT{ p_dummy.Deactivate(); }; return SUCCESS; }

public:
    IOResponseMultiplexer(const std::string& name, Object& parent, Clock& clock, drisc::RegisterFile& rf, Allocator& alloc, size_t numDevices, Config& config);
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



#endif
