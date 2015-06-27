// -*- c++ -*-
#ifndef DDRMEMORY_H
#define DDRMEMORY_H

#include "DDR.h"

#include <arch/BankSelector.h>
#include <arch/Memory.h>
#include <arch/VirtualMemory.h>
#include <sim/inspect.h>

#include <queue>
#include <set>

class Config;

namespace Simulator
{

class DDRMemory : public IMemory, public VirtualMemory
{
    struct ClientInfo;

    // {% from "sim/macros.p.h" import gen_struct %}
    // {% call gen_struct() %}
    ((name Request)
     (state
        (MCID        client)
        (bool        write)
        (MemAddr     address)
        (MemData     data)
        (WClientID   wid)
         ))
    // {% endcall %}

    class Interface;

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

protected:
    Clock&                   m_clock;
    std::vector<ClientInfo>  m_clients;
    StorageTraceSet          m_storages;
    std::vector<Interface*>  m_ifs;
    DDRChannelRegistry       m_ddr;
    size_t                   m_lineSize;
    IBankSelector*           m_selector;

    DefineSampleVariable(uint64_t, nreads);
    DefineSampleVariable(uint64_t, nread_bytes);
    DefineSampleVariable(uint64_t, nwrites);
    DefineSampleVariable(uint64_t, nwrite_bytes);

public:
    DDRMemory(const std::string& name, Object& parent, Clock& clock, const std::string& defaultInterfaceSelectorType);
    DDRMemory(const DDRMemory&) = delete;
    DDRMemory& operator=(const DDRMemory&) = delete;
    ~DDRMemory();

    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const override;
};

}
#endif
