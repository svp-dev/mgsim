#ifndef ZLCOMA_COMA_H
#define ZLCOMA_COMA_H

#include <arch/Memory.h>
#include <arch/VirtualMemory.h>
#include <arch/BankSelector.h>
#include <arch/mem/DDR.h>

#include <queue>
#include <set>

class Config;
class ComponentModelRegistry;

namespace Simulator
{

class ZLCOMA : public Object, public IMemory, public VirtualMemory, public Inspect::Interface<Inspect::Line|Inspect::Trace>
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
    typedef std::pair<size_t, WClientID> WriteAck;

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

    void ConfigureTopRing();

    unsigned int GetTotalTokens() const {
        // One token per cache
        return m_caches.size();
    }

    void Initialize();

public:
    ZLCOMA(const std::string& name, Simulator::Object& parent, Clock& clock, Config& config);
    ZLCOMA(const ZLCOMA&) = delete;
    ZLCOMA& operator=(const ZLCOMA&) = delete;
    ~ZLCOMA();

    const TraceMap& GetTraces() const { return m_traces; }

    IBankSelector& GetBankSelector() const { return *m_selector; }

    // IMemory
    MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, bool grouped) override;
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

}
#endif
