#ifndef IO_DCA_H
#define IO_DCA_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class IODirectCacheAccess : public Object
{
public:

    struct Request 
    {
        IODeviceID client;
        MemAddr    address;
        bool       write;
        MemData    data;
    };

private:

    struct Response
    {
        MemAddr address;
        MemData data;
    };

    Processor&           m_cpu;
    IOBusInterface&      m_busif;
    const MemSize        m_lineSize; 

    Buffer<Request>      m_requests; // from bus
    Buffer<Response>     m_responses; // from memory

    bool                 m_has_outstanding_request;
    IODeviceID           m_outstanding_client;
    MemAddr              m_outstanding_address;
    MemSize              m_outstanding_size;

public:
    IODirectCacheAccess(const std::string& name, Object& parent, Clock& clock, Clock& busclock, Processor& proc, IOBusInterface& busif, Config& config);

    bool QueueRequest(const Request& req);
    
    Process p_MemoryOutgoing;
    Process p_BusOutgoing;

    ArbitratedService<> p_service;

    Result DoMemoryOutgoing();
    Result DoBusOutgoing();

    bool OnMemoryReadCompleted(MemAddr addr, const MemData& data) ;
    bool OnMemoryWriteCompleted(TID tid) { return true; }
    bool OnMemoryInvalidated(MemAddr addr) { return true; }
    bool OnMemorySnooped(MemAddr addr, const MemData& data) { return true; }

};

#endif
