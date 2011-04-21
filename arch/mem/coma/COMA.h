#ifndef COMA_COMA_H
#define COMA_COMA_H

#include "arch/Memory.h"
#include "arch/VirtualMemory.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class COMA : public Object, public IMemoryAdmin, public VirtualMemory
{
    class DDRChannel;
public:
    class Node;
    class DirectoryTop;
    class DirectoryBottom;
    class Directory;
    class RootDirectory;
    class Cache;
    
    // A simple base class for all COMA objects. It keeps track of what
    // COMA memory it's in.
    class Object : public virtual Simulator::Object
    {
    protected:
        COMA& m_parent;
        
    public:
        Object(const std::string& name, COMA& parent)
            : Simulator::Object(name, parent), m_parent(parent) {}
        virtual ~Object() {}
    };

private:    
    typedef std::set<MemAddr> TraceMap;
    typedef size_t            CacheID;
    
    size_t                      m_numProcsPerCache;
    size_t                      m_numCachesPerDir;
    std::vector<Cache*>         m_caches;             ///< List of caches
    std::vector<Directory*>     m_directories;        ///< List of directories
    std::vector<RootDirectory*> m_roots;              ///< List of root directories
    TraceMap                    m_traces;             ///< Active traces
    
    uint64_t                    m_nreads, m_nwrites, m_nread_bytes, m_nwrite_bytes;
    
public:
    COMA(const std::string& name, Simulator::Object& parent, Clock& clock, const Config& config);
    ~COMA();

    const TraceMap& GetTraces() const { return m_traces; }
    
    // IMemory
    void Reserve(MemAddr address, MemSize size, int perm);
    void Unreserve(MemAddr address);
    void RegisterClient  (PSize pid, IMemoryCallback& callback, const Process* processes[]);
    void UnregisterClient(PSize pid);
    bool Read (PSize pid, MemAddr address, MemSize size);
    bool Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid);
    bool CheckPermissions(MemAddr address, MemSize size, int access) const;

    void GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, 
                             uint64_t& nread_bytes, uint64_t& nwrite_bytes,
                             uint64_t& nreads_ext, uint64_t& nwrites_ext) const;

    void Cmd_Help (std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Line (std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Trace(std::ostream& out, const std::vector<std::string>& arguments);

    // IMemoryAdmin
    bool Allocate(MemSize size, int perm, MemAddr& address);
    void Read (MemAddr address, void* data, MemSize size);
    void Write(MemAddr address, const void* data, MemSize size);
};

}
#endif
