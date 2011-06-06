#ifndef ZLCOMA_COMA_H
#define ZLCOMA_COMA_H

#include "arch/Memory.h"
#include "arch/VirtualMemory.h"
#include "mem/DDR.h"
#include <queue>
#include <set>

class Config;
class ComponentModelRegistry;

namespace Simulator
{

class ZLCOMA : public Object, public IMemoryAdmin, public VirtualMemory, public Inspect::Interface<Inspect::Line|Inspect::Trace>
{
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
        ZLCOMA& m_parent;
        
    public:
        Object(const std::string& name, ZLCOMA& parent)
            : Simulator::Object(name, parent), m_parent(parent) {}
        virtual ~Object() {}
    };

private:    
    typedef std::set<MemAddr> TraceMap;
    typedef size_t            CacheID;
    typedef std::pair<size_t, TID> WriteAck;
    
    // Helper function for checking if a value is contained in a range
    template<class InputIterator, class EqualityComparable>
    static inline bool contains(InputIterator first, InputIterator last, const EqualityComparable& value)
    {
        return std::find(first, last, value) != last;
    }

    
    ComponentModelRegistry&     m_registry;
    size_t                      m_numClientsPerCache;
    size_t                      m_numCachesPerDir;
    size_t                      m_numClients;
    Config&                     m_config;
    std::vector<Cache*>         m_caches;             ///< List of caches
    std::vector<Directory*>     m_directories;        ///< List of directories
    std::vector<RootDirectory*> m_roots;              ///< List of root directories
    TraceMap                    m_traces;             ///< Active traces
    DDRChannelRegistry          m_ddr;                ///< List of DDR channels
    
    uint64_t                    m_nreads, m_nwrites, m_nread_bytes, m_nwrite_bytes;
    
    void ConfigureTopRing();

    unsigned int GetTotalTokens() const {
        // One token per cache
        return m_caches.size();
    }
    
    void Initialize();

public:
    ZLCOMA(const std::string& name, Simulator::Object& parent, Clock& clock, Config& config);
    ~ZLCOMA();

    const TraceMap& GetTraces() const { return m_traces; }
    
    // IMemory
    void Reserve(MemAddr address, MemSize size, int perm);
    void Unreserve(MemAddr address);
    MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage);
    void UnregisterClient(MCID id);
    bool Read (MCID id, MemAddr address, MemSize size);
    bool Write(MCID id, MemAddr address, const void* data, MemSize size, TID tid);
    bool CheckPermissions(MemAddr address, MemSize size, int access) const;

    void GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, 
                             uint64_t& nread_bytes, uint64_t& nwrite_bytes,
                             uint64_t& nreads_ext, uint64_t& nwrites_ext) const;

    void Cmd_Info (std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Line (std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Trace(std::ostream& out, const std::vector<std::string>& arguments);

    // IMemoryAdmin
    bool Allocate(MemSize size, int perm, MemAddr& address);
    void Read (MemAddr address, void* data, MemSize size);
    void Write(MemAddr address, const void* data, MemSize size);
};

}
#endif
