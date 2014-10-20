// -*- c++ -*-
#ifndef BANKEDMEMORY_H
#define BANKEDMEMORY_H

#include <arch/BankSelector.h>
#include <arch/Memory.h>
#include <arch/VirtualMemory.h>
#include <sim/inspect.h>
#include <sim/sampling.h>

#include <queue>
#include <set>

class Config;
class ComponentModelRegistry;

namespace Simulator
{

class ArbitratedWriteFunction;

class BankedMemory : public IMemory, public VirtualMemory
{
    struct ClientInfo;
    struct Request;
    class Bank;

    std::pair<CycleNo, CycleNo> GetMessageDelay(size_t body_size) const;
    CycleNo                     GetMemoryDelay (size_t data_size) const;

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
    Clock&                  m_clock;
    std::vector<ClientInfo> m_clients;
    StorageTraceSet         m_storages;
    std::vector<Bank*>      m_banks;
    CycleNo                 m_baseRequestTime;
    CycleNo                 m_timePerLine;
    size_t                  m_lineSize;
    IBankSelector*          m_selector;

    DefineSampleVariable(uint64_t, nreads);
    DefineSampleVariable(uint64_t, nread_bytes);
    DefineSampleVariable(uint64_t, nwrites);
    DefineSampleVariable(uint64_t, nwrite_bytes);

public:
    BankedMemory(const std::string& name, Object& parent, Clock& clock, const std::string& defaultBankSelectorType);
    BankedMemory(const BankedMemory&) = delete;
    BankedMemory& operator=(const BankedMemory&) = delete;
    ~BankedMemory();

    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const override;
};

}
#endif
