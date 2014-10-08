// -*- c++ -*-
#ifndef CDMA_CDMA_H
#define CDMA_CDMA_H

#include <arch/Memory.h>
#include <arch/VirtualMemory.h>
#include <sim/inspect.h>
#include <arch/mem/DDR.h>

#include <queue>
#include <set>

class Config;
class ComponentModelRegistry;

namespace Simulator
{

class CDMA : public Object, public IMemory, public VirtualMemory, public Inspect::Interface<Inspect::Line|Inspect::Trace>
{
public:
    class Node;
    class DirectoryTop;
    class DirectoryBottom;
    class Directory;
    class RootDirectory;
    class Cache;

    // A simple base class for all CDMA objects. It keeps track of what
    // CDMA memory it's in.
    class Object : public virtual Simulator::Object
    {
    protected:
        CDMA& m_parent;

    public:
        Object(const std::string& name, CDMA& parent)
            : Simulator::Object(name, parent), m_parent(parent) {}
        virtual ~Object() {}
    };

protected:
    typedef std::set<MemAddr> TraceMap;
    typedef size_t            NodeID;

    Clock&                      m_clock;
    size_t                      m_numClientsPerCache;
    size_t                      m_numCachesPerLowRing;
    size_t                      m_numClients;
    size_t                      m_lineSize;
    std::vector<Cache*>         m_caches;             ///< List of caches
    std::vector<Directory*>     m_directories;        ///< List of directories
    std::vector<RootDirectory*> m_roots;              ///< List of root directories
    TraceMap                    m_traces;             ///< Active traces
    DDRChannelRegistry          m_ddr;                ///< List of DDR channels

    std::vector<std::pair<Cache*,MCID> > m_clientMap; ///< Mapping of MCID to caches

    DefineSampleVariable(uint64_t, nreads);
    DefineSampleVariable(uint64_t, nwrites);
    DefineSampleVariable(uint64_t, nread_bytes);
    DefineSampleVariable(uint64_t, nwrite_bytes);

    unsigned int GetTotalTokens() const {
        // One token per cache
        return m_caches.size();
    }

    virtual void Initialize() = 0;

public:
    CDMA(const std::string& name, Simulator::Object& parent, Clock& clock);
    CDMA(const CDMA&) = delete;
    CDMA& operator=(const CDMA&) = delete;
    ~CDMA();

    const TraceMap& GetTraces() const { return m_traces; }

    size_t GetLineSize() const { return m_lineSize; }
    size_t GetNumCaches() const { return m_caches.size(); }
    size_t GetNumDirectories() const { return m_directories.size(); }
    size_t GetNumRootDirectories() const { return m_roots.size(); }

    Cache& GetCache(size_t i) const { return *m_caches[i]; }
    Directory& GetDirectory(size_t i) const { return *m_directories[i]; }
    RootDirectory& GetRootDirectory(size_t i) const { return *m_roots[i]; }

    // IMemory
    virtual MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, bool grouped) override = 0;
    void UnregisterClient(MCID id) override;
    using VirtualMemory::Read;
    using VirtualMemory::Write;
    bool Read (MCID id, MemAddr address) override;
    bool Write(MCID id, MemAddr address, const MemData& data, WClientID wid) override;

    void GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites,
                             uint64_t& nread_bytes, uint64_t& nwrite_bytes,
                             uint64_t& nreads_ext, uint64_t& nwrites_ext) const override;

    void Cmd_Info (std::ostream& out, const std::vector<std::string>& arguments) const override;
    void Cmd_Line (std::ostream& out, const std::vector<std::string>& arguments) const override;
    void Cmd_Trace(std::ostream& out, const std::vector<std::string>& arguments) override;
};

class OneLevelCDMA : public CDMA
{
public:
    void Initialize();

    MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, bool grouped) override;

    OneLevelCDMA(const std::string& name, Simulator::Object& parent, Clock& clock);
};

class TwoLevelCDMA : public CDMA
{
public:
    void Initialize();

    MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, bool grouped) override;

    TwoLevelCDMA(const std::string& name, Simulator::Object& parent, Clock& clock);
};

}
#endif
