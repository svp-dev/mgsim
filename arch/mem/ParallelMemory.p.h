// -*- c++ -*-
#ifndef PARALLELMEMORY_H
#define PARALLELMEMORY_H

#include "arch/Memory.h"
#include "arch/VirtualMemory.h"
#include "sim/inspect.h"
#include "sim/buffer.h"

#include <queue>
#include <deque>
#include <set>
#include <map>
#include <vector>

class Config;
class ComponentModelRegistry;

namespace Simulator
{

class ParallelMemory : public IMemory, public VirtualMemory
{
    class Port;
    // {% from "sim/macros.p.h" import gen_struct %}
    // {% call gen_struct() %}
    ((name Request)
     (state
      (bool        write)
      (MemAddr     address)
      (MemData     data)
      (WClientID   wid)
         ))
    // {% endcall %}

    bool AddRequest(IMemoryCallback& callback, const Request& request);

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

    CycleNo GetMemoryDelay(size_t data_size) const;

    Clock&      m_clock;
    std::vector<Port*>        m_ports;

    BufferSize  m_buffersize;      // Size of request queues
    CycleNo	m_baseRequestTime; // Config: This many cycles per request regardless of size
    CycleNo	m_timePerLine;     // Config: With this many additional cycles per line
    size_t	m_lineSize;        // Config: With this many bytes per line

    DefineSampleVariable(uint64_t, nreads);
    DefineSampleVariable(uint64_t, nread_bytes);
    DefineSampleVariable(uint64_t, nwrites);
    DefineSampleVariable(uint64_t, nwrite_bytes);

public:
    ParallelMemory(const std::string& name, Object& parent, Clock& clock);
    ~ParallelMemory();

    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif
