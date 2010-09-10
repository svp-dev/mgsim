#ifndef COMA_ZLCOMA_H
#define COMA_ZLCOMA_H

#include "../Memory.h"
#include "../storage.h"
#include "../VirtualMemory.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class ZLCOMA : public Object, public IMemoryAdmin/*, public VirtualMemory*/
{
    class Link;
    
    std::vector<Link*> m_links;
    uint64_t           m_nreads, m_nwrites, m_nread_bytes, m_nwrite_bytes;
    
public:
    ZLCOMA(const std::string& name, Simulator::Object& parent, Clock& clock, const Config& config);
    ~ZLCOMA();

    // IMemory
    void Reserve(MemAddr address, MemSize size, int perm);
    void Unreserve(MemAddr address);
    void RegisterClient  (PSize pid, IMemoryCallback& callback, const Process* processes[]);
    void UnregisterClient(PSize pid);
    bool Read (PSize pid, MemAddr address, MemSize size);
    bool Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid);
    bool CheckPermissions(MemAddr address, MemSize size, int access) const;

    void GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, uint64_t& nread_bytes, uint64_t& nwrite_bytes) const;

    // IMemoryAdmin
    bool Allocate(MemSize size, int perm, MemAddr& address);
    void Read (MemAddr address, void* data, MemSize size);
    void Write(MemAddr address, const void* data, MemSize size);
};

}
#endif
