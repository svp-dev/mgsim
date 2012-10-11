#ifndef COMA_COMA_H
#define COMA_COMA_H

#include <arch/Memory.h>
#include <arch/VirtualMemory.h>
#include <arch/BankSelector.h>
#include <sim/inspect.h>
#include <arch/mem/DDR.h>

#include <queue>
#include <set>

class Config;
class ComponentModelRegistry;

namespace Simulator
{

class COMA : public Object, public IMemory, public VirtualMemory, public Inspect::Interface<Inspect::Line|Inspect::Trace>
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
        COMA& m_parent;

    public:
        Object(const std::string& name, COMA& parent)
            : Simulator::Object(name, parent), m_parent(parent) {}
        virtual ~Object() {}
    };

protected:
    typedef std::set<MemAddr> TraceMap;
    typedef size_t            CacheID;

    ComponentModelRegistry&     m_registry;
    size_t                      m_numClientsPerCache;
    size_t                      m_numCachesPerLowRing;
    size_t                      m_numClients;
    size_t                      m_lineSize;
    Config&                     m_config;
    IBankSelector*              m_selector;           ///< Mapping of line addresses to set indexes
    std::vector<Cache*>         m_caches;             ///< List of caches
    std::vector<Directory*>     m_directories;        ///< List of directories
    std::vector<RootDirectory*> m_roots;              ///< List of root directories
    TraceMap                    m_traces;             ///< Active traces
    DDRChannelRegistry          m_ddr;                ///< List of DDR channels

    std::vector<std::pair<Cache*,MCID> > m_clientMap; ///< Mapping of MCID to caches

    uint64_t                    m_nreads, m_nwrites, m_nread_bytes, m_nwrite_bytes;

    unsigned int GetTotalTokens() const {
        // One token per cache
        return m_caches.size();
    }

    virtual void Initialize() = 0;

public:
    COMA(const std::string& name, Simulator::Object& parent, Clock& clock, Config& config);
    COMA(const COMA&) = delete;
    COMA& operator=(const COMA&) = delete;
    ~COMA();

    const TraceMap& GetTraces() const { return m_traces; }

    IBankSelector& GetBankSelector() const { return *m_selector; }

    size_t GetLineSize() const { return m_lineSize; }
    size_t GetNumClientsPerCache() const { return m_numClientsPerCache; }
    size_t GetNumCachesPerLowRing() const { return m_numCachesPerLowRing; }
    size_t GetNumCaches() const { return m_caches.size(); }
    size_t GetNumDirectories() const { return m_directories.size(); }
    size_t GetNumRootDirectories() const { return m_roots.size(); }
    size_t GetNumCacheSets() const;
    size_t GetCacheAssociativity() const;
    size_t GetDirectoryAssociativity() const;
    
    // IMemory
    virtual MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool grouped) = 0;
    void UnregisterClient(MCID id);
    bool Read (MCID id, MemAddr address);
    bool Write(MCID id, MemAddr address, const MemData& data, WClientID wid);

    using VirtualMemory::Read;
    using VirtualMemory::Write;
    void GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, 
                             uint64_t& nread_bytes, uint64_t& nwrite_bytes,
                             uint64_t& nreads_ext, uint64_t& nwrites_ext) const;

    void Cmd_Info (std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Line (std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Trace(std::ostream& out, const std::vector<std::string>& arguments);
};

class OneLevelCOMA : public COMA
{
public:
    void Initialize();

    MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool grouped);

    OneLevelCOMA(const std::string& name, Simulator::Object& parent, Clock& clock, Config& config);
};

class TwoLevelCOMA : public COMA
{
public:
    void Initialize();

    MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool grouped);

    TwoLevelCOMA(const std::string& name, Simulator::Object& parent, Clock& clock, Config& config);
};

}
#endif
