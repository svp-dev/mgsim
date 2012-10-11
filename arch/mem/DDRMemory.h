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

class DDRMemory : public Object, public IMemory, public VirtualMemory
{
    struct ClientInfo;
    struct Request;
    class Interface;

    // IMemory
    MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool /*ignored*/) override;
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
    ComponentModelRegistry&  m_registry;
    Clock&                   m_clock;
    std::vector<ClientInfo>  m_clients;
    StorageTraceSet          m_storages;
    std::vector<Interface*>  m_ifs;
    DDRChannelRegistry       m_ddr;
    size_t                   m_lineSize;
    IBankSelector*           m_selector;

    uint64_t m_nreads;
    uint64_t m_nread_bytes;
    uint64_t m_nwrites;
    uint64_t m_nwrite_bytes;

public:
    DDRMemory(const std::string& name, Object& parent, Clock& clock, Config& config, const std::string& defaultInterfaceSelectorType);
    DDRMemory(const DDRMemory&) = delete;
    DDRMemory& operator=(const DDRMemory&) = delete;
    ~DDRMemory();

    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const override;
};

}
#endif

