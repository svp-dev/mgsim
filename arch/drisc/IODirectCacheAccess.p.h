// -*- c++ -*-
#ifndef IO_DCA_H
#define IO_DCA_H

#include <sim/kernel.h>
#include <sim/buffer.h>
#include <arch/Memory.h>
#include <arch/IOBus.h>
#include <arch/drisc/forward.h>

namespace Simulator
{
namespace drisc
{

class IODirectCacheAccess : public Object, public IMemoryCallback
{
public:

    enum RequestType
    {
        READ = 0,
        WRITE = 1,
        FLUSH = 2
    };


    // {% from "sim/macros.p.h" import gen_struct %}
    // {% call gen_struct() %}
    ((name Request)
     (state
      (IODeviceID  client)
      (RequestType type)
      (MemAddr     address)
      (MemSize     size)
      (array       data char MAX_MEMORY_OPERATION_SIZE)
         ))
    // {% endcall %}

private:

    // {% call gen_struct() %}
    ((name Response)
     (state
      (MemAddr address)
      (MemSize size)
      (array   data char MAX_MEMORY_OPERATION_SIZE)))
    // {% endcall %}

    IMemory*             m_memory;
    MCID                 m_mcid;
    IOBusInterface&      m_busif;
    const MemSize        m_lineSize;

    Object& GetDRISCParent() const { return *GetParent()->GetParent(); }

public:
    Buffer<Request>      m_requests; // from bus

private:
    Buffer<Response>     m_responses; // from memory

    size_t               m_pending_writes;

    MemAddr              m_outstanding_address;
    MemSize              m_outstanding_size;
    IODeviceID           m_outstanding_client;
    bool                 m_has_outstanding_request;
    bool                 m_flushing;

public:
    IODirectCacheAccess(const std::string& name, IOInterface& parent, Clock& clock);
    IODirectCacheAccess(const IODirectCacheAccess&) = delete;
    IODirectCacheAccess& operator=(const IODirectCacheAccess&) = delete;
    ~IODirectCacheAccess();
    void ConnectMemory(IMemory* memory);

    bool QueueRequest(Request&& req);

    Process p_MemoryOutgoing;
    Process p_BusOutgoing;

    ArbitratedService<> p_service;

    Result DoMemoryOutgoing();
    Result DoBusOutgoing();

    bool OnMemoryReadCompleted(MemAddr addr, const char* data) override;
    bool OnMemoryWriteCompleted(TID tid) override;
    bool OnMemorySnooped(MemAddr /*unused*/, const char* /*data*/, const bool* /*mask*/) override;
    bool OnMemoryInvalidated(MemAddr /*unused*/) override;

    Object& GetMemoryPeer() override;
};

}
}

#endif
