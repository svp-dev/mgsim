#ifndef BANKEDMEMORY_H
#define BANKEDMEMORY_H

#include <arch/BankSelector.h>
#include <arch/Memory.h>
#include <arch/VirtualMemory.h>
#include <sim/inspect.h>

#include <queue>
#include <set>

class Config;
class ComponentModelRegistry;

namespace Simulator
{

class ArbitratedWriteFunction;

class BankedMemory : public Object, public IMemory, public VirtualMemory
{
    struct ClientInfo;
    struct Request;
    class Bank;

    std::pair<CycleNo, CycleNo> GetMessageDelay(size_t body_size) const;
    CycleNo                     GetMemoryDelay (size_t data_size) const;

    // IMemory
    MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool /*ignored*/);
    void UnregisterClient(MCID id);
    bool Read (MCID id, MemAddr address);
    bool Write(MCID id, MemAddr address, const MemData& data, WClientID wid);
    using VirtualMemory::Read;
    using VirtualMemory::Write;

    void GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, 
                             uint64_t& nread_bytes, uint64_t& nwrite_bytes,
                             uint64_t& nreads_ext, uint64_t& nwrites_ext) const
    {
        nreads = m_nreads;
        nwrites = m_nwrites;
        nread_bytes = m_nread_bytes;
        nwrite_bytes = m_nwrite_bytes;
        nreads_ext = m_nreads;
        nwrites_ext = m_nwrites;
    }	

protected:
    ComponentModelRegistry& m_registry;
    Clock&                  m_clock;
    std::vector<ClientInfo> m_clients;
    StorageTraceSet         m_storages;
    std::vector<Bank*>      m_banks;
    CycleNo                 m_baseRequestTime;
    CycleNo                 m_timePerLine;
    size_t                  m_lineSize;
    BufferSize              m_bufferSize;
    IBankSelector*          m_selector;

    uint64_t m_nreads;
    uint64_t m_nread_bytes;
    uint64_t m_nwrites;
    uint64_t m_nwrite_bytes;

public:
    BankedMemory(const std::string& name, Object& parent, Clock& clock, Config& config, const std::string& defaultBankSelectorType);
    ~BankedMemory();
    
    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif

