#ifndef BANKEDMEMORY_H
#define BANKEDMEMORY_H

#include "arch/Memory.h"
#include "arch/VirtualMemory.h"
#include "sim/inspect.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class ArbitratedWriteFunction;

class BankedMemory : public Object, public IMemoryAdmin, public VirtualMemory
{
    struct ClientInfo;
    struct Request;
    class Bank;

    virtual size_t GetBankFromAddress(MemAddr address) const;

    std::pair<CycleNo, CycleNo> GetMessageDelay(size_t body_size) const;
    CycleNo                     GetMemoryDelay (size_t data_size) const;

    // IMemory
    void Reserve(MemAddr address, MemSize size, int perm);
    void Unreserve(MemAddr address);
    void RegisterClient  (PSize pid, IMemoryCallback& callback, const Process* processes[]);
    void UnregisterClient(PSize pid);
    bool Read (PSize pid, MemAddr address, MemSize size);
    bool Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid);
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
    Clock&                  m_clock;
    std::vector<ClientInfo> m_clients;
    std::vector<Bank*>      m_banks;
    CycleNo                 m_baseRequestTime;
    CycleNo                 m_timePerLine;
    size_t                  m_sizeOfLine;
    BufferSize              m_bufferSize;
    size_t                  m_cachelineSize;

    uint64_t m_nreads;
    uint64_t m_nread_bytes;
    uint64_t m_nwrites;
    uint64_t m_nwrite_bytes;

public:
    BankedMemory(const std::string& name, Object& parent, Clock& clock, Config& config);
    ~BankedMemory();
    
    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif

