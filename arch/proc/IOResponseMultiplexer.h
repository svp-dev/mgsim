#ifndef IORESPONSEMUX_H
#define IORESPONSEMUX_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class IOResponseMultiplexer : public Object
{
private:
    RegisterFile&                m_regFile;

    struct IOResponse
    {
        IODeviceID  device;
        IOData      data;
    };
    Buffer<IOResponse>            m_incoming;

    typedef Buffer<RegAddr>       WriteBackQueue;
    std::vector<WriteBackQueue*>  m_wb_buffers;

public:
    IOResponseMultiplexer(const std::string& name, Object& parent, Clock& clock, RegisterFile& rf, const Config& config);

    // sent by device select upon an I/O read from the processor
    bool QueueWriteBackAddress(IODeviceID dev, const RegAddr& addr);

    // triggered by the IOBusInterface
    bool OnReadResponseReceived(IODeviceID from, const IOData& data);

    Process p_IncomingReadResponses;
    
    // upon data available on m_incoming
    Result DoReceivedReadResponses();
};



#endif
