#ifndef IO_DCA_H
#define IO_DCA_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class IODirectCacheAccess : public Object, public IMemoryCallback
{
public:

    enum RequestType
    {
        READ = 0,
        WRITE = 1,
        FLUSH = 2
    };

    struct Request 
    {
        IODeviceID  client;
        RequestType type;
        MemAddr     address;
        MemSize     size;
        char        data[MAX_MEMORY_OPERATION_SIZE];
    };

private:

    struct Response
    {
        MemAddr     address;
        MemSize     size;
        char        data[MAX_MEMORY_OPERATION_SIZE];
    };

    Processor&           m_cpu;
    IMemory&             m_memory;
    MCID                 m_mcid;
    IOBusInterface&      m_busif;
    const MemSize        m_lineSize; 

public:
    Buffer<Request>      m_requests; // from bus

private:
    Buffer<Response>     m_responses; // from memory

    bool                 m_has_outstanding_request;
    IODeviceID           m_outstanding_client;
    MemAddr              m_outstanding_address;
    MemSize              m_outstanding_size;

    bool                 m_flushing;
    size_t               m_pending_writes;

public:
    IODirectCacheAccess(const std::string& name, Object& parent, Clock& clock, Processor& proc, IMemory& memory, IOBusInterface& busif, Config& config);
    ~IODirectCacheAccess();

    bool QueueRequest(const Request& req);
    
    Process p_MemoryOutgoing;
    Process p_BusOutgoing;

    ArbitratedService<> p_service;

    Result DoMemoryOutgoing();
    Result DoBusOutgoing();

    bool OnMemoryReadCompleted(MemAddr addr, const char* data) ;
    bool OnMemoryWriteCompleted(TID tid);
    bool OnMemorySnooped(MemAddr /*unused*/, const char* /*data*/, const bool* /*mask*/) { return true; }
    bool OnMemoryInvalidated(MemAddr /*unused*/) { return true; }

    Object& GetMemoryPeer() { return m_cpu; }
};

#endif
