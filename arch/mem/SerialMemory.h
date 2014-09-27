// -*- c++ -*-
#ifndef SERIALMEMORY_H
#define SERIALMEMORY_H

#include <arch/Memory.h>
#include <arch/VirtualMemory.h>
#include <sim/inspect.h>

#include <deque>
#include <set>

class Config;
class ComponentModelRegistry;

namespace Simulator
{

class SerialMemory : public Object, public IMemory, public VirtualMemory
{
    struct Request
    {
        MemAddr          address;
        MemData          data;
        IMemoryCallback* callback;
        WClientID        wid;
        bool             write;
    };

    // IMemory
    MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, bool /*ignored*/) override;
    void UnregisterClient(MCID id) override;
    using VirtualMemory::Read;
    using VirtualMemory::Write;
    bool Read (MCID id, MemAddr address) override;
    bool Write(MCID id, MemAddr address, const MemData& data, WClientID wid) override;

    void GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites,
                             uint64_t& nread_bytes, uint64_t& nwrite_bytes,
                             uint64_t& nreads_ext, uint64_t& nwrites_ext) const override
    {
        nreads = m_nreads;
        nwrites = m_nwrites;
        nread_bytes = m_nread_bytes;
        nwrite_bytes = m_nwrite_bytes;
        nreads_ext = m_nreads;
        nwrites_ext = m_nwrites;
    }

    std::vector<IMemoryCallback*> m_clients;
    Buffer<Request>               m_requests;
    ArbitratedService<CyclicArbitratedPort> p_requests;
    CycleNo                       m_baseRequestTime;
    CycleNo                       m_timePerLine;
    CycleNo                       m_lineSize;
    CycleNo                       m_nextdone;
    StorageTraceSet               m_storages;

    uint64_t m_nreads;
    uint64_t m_nread_bytes;
    uint64_t m_nwrites;
    uint64_t m_nwrite_bytes;

    // Processes
    Process p_Requests;

    Result DoRequests();

public:
    SerialMemory(const std::string& name, Object& parent, Clock& clock);

    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif

