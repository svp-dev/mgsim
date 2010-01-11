#ifndef PARALLELMEMORY_H
#define PARALLELMEMORY_H

#include "Memory.h"
#include "kernel.h"
#include "Processor.h"
#include "VirtualMemory.h"
#include <queue>
#include <deque>
#include <set>
#include <map>
#include <vector>

namespace Simulator
{

class ParallelMemory : public Object, public IMemoryAdmin, public VirtualMemory
{
    struct Request;
	struct ClientInfo;
	class Port;

    bool AddRequest(IMemoryCallback& callback, const Request& request);
    
    // IMemory
    void Reserve(MemAddr address, MemSize size, int perm);
    void Unreserve(MemAddr address);
    void RegisterClient(PSize pid, IMemoryCallback& callback, const Process* processes[]);
    void UnregisterClient(PSize pid);
    bool Read (PSize pid, MemAddr address, MemSize size, MemTag tag);
    bool Write(PSize pid, MemAddr address, const void* data, MemSize size, MemTag tag);
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
    
    CycleNo GetMemoryDelay(size_t data_size) const;
    
    std::vector<ClientInfo> m_clients;
    
    CycleNo	m_baseRequestTime; // Config: This many cycles per request regardless of size
    CycleNo	m_timePerLine;     // Config: With this many additional cycles per line
    size_t	m_sizeOfLine;      // Config: With this many bytes per line
    
    uint64_t m_nreads;
    uint64_t m_nread_bytes;
    uint64_t m_nwrites;
    uint64_t m_nwrite_bytes;

public:
    ParallelMemory(const std::string& name, Object& parent, const Config& config);
    ~ParallelMemory();

    // Debugging
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif

