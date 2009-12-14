#ifndef BANKEDMEMORY_H
#define BANKEDMEMORY_H

#include "Memory.h"
#include "storage.h"
#include "VirtualMemory.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class ArbitratedWriteFunction;

class BankedMemory : public IComponent, public IMemoryAdmin, public VirtualMemory
{
    struct Request;
    struct Bank;

    virtual size_t GetBankFromAddress(MemAddr address) const;
    bool AddRequest(Buffer<Request>& queue, Request& request, bool data);

    // Component
    Result OnCycle(unsigned int stateIndex);

    // IMemory
    void Reserve(MemAddr address, MemSize size, int perm);
    void Unreserve(MemAddr address);
    void RegisterListener  (PSize pid, IMemoryCallback& callback, const ArbitrationSource* sources);
    void UnregisterListener(PSize pid, IMemoryCallback& callback);
    bool Read (IMemoryCallback& callback, MemAddr address, MemSize size, MemTag tag);
    bool Write(IMemoryCallback& callback, MemAddr address, const void* data, MemSize size, MemTag tag);
	bool CheckPermissions(MemAddr address, MemSize size, int access) const;

    // IMemoryAdmin
    bool Allocate(MemSize size, int perm, MemAddr& address);
    void Read (MemAddr address, void* data, MemSize size);
    void Write(MemAddr address, const void* data, MemSize size);

    void GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, 
                             uint64_t& nread_bytes, uint64_t& nwrite_bytes) const
    {
        nreads = m_nreads;
        nread_bytes = m_nread_bytes;
        nwrites = m_nwrites;
        nwrite_bytes = m_nwrite_bytes;
    }	

    // Debugging
    static void PrintRequest(std::ostream& out, char prefix, const Request& request);


protected:
    // We need arbitration per client because only one bank can write
    // to a client at a time.
    typedef std::map<IMemoryCallback*, ArbitratedService*> ClientMap;
    
    ClientMap          m_clients;
    std::vector<Bank*> m_banks;
    CycleNo            m_baseRequestTime;
    CycleNo            m_timePerLine;
    size_t             m_sizeOfLine;
    BufferSize         m_bufferSize;
    size_t             m_cachelineSize;

        uint64_t m_nreads;
        uint64_t m_nread_bytes;
        uint64_t m_nwrites;
        uint64_t m_nwrite_bytes;

public:
    BankedMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config);
    ~BankedMemory();
    
    // Debugging
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif

