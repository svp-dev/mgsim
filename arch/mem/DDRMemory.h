#ifndef DDRMEMORY_H
#define DDRMEMORY_H

#include "BankSelector.h"
#include "DDR.h"
#include "arch/Memory.h"
#include "arch/VirtualMemory.h"
#include "sim/inspect.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class DDRMemory : public Object, public IMemoryAdmin, public VirtualMemory
{
    struct ClientInfo;
    struct Request;
    class Interface;

    // IMemory
    void Reserve(MemAddr address, MemSize size, int perm);
    void Unreserve(MemAddr address);
    MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool /*ignored*/);
    void UnregisterClient(MCID id);
    bool Read (MCID id, MemAddr address, MemSize size);
    bool Write(MCID id, MemAddr address, const void* data, MemSize size, TID tid);
	bool CheckPermissions(MemAddr address, MemSize size, int access) const;

    // IMemoryAdmin
    bool Allocate(MemSize size, int perm, MemAddr& address);
    void Read (MemAddr address, void* data, MemSize size);
    void Write(MemAddr address, const void* data, MemSize size);

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
    ~DDRMemory();
    
    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif

