#include "Cache.h"
#include "RootDirectory.h"
#include <sim/config.h>
#include <sim/sampling.h>
#include <sim/log2.h>

#include <cassert>
#include <cstring>
using namespace std;

namespace Simulator
{

MCID OneLevelCOMA::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, bool grouped)
{
    MCID id = m_clientMap.size();
    m_clientMap.resize(id + 1);

    size_t abstract_id;
    if (grouped)
        abstract_id = m_numClients - 1;
    else
        abstract_id = m_numClients++;

    size_t cache_id = abstract_id / m_numClientsPerCache;

    if (cache_id == m_caches.size())
    {
        // Add a cache
        stringstream name;
        name << "cache" << m_caches.size();
        Cache* cache = new Cache(name.str(), *this, GetClock(), m_caches.size(), m_config);
        m_caches.push_back(cache);
    }

    // Forward the registration to the cache associated with the processor

    Cache *cache = m_caches[cache_id];

    MCID id_in_cache = cache->RegisterClient(callback, process, traces, storages);

    m_clientMap[id] = make_pair(cache, id_in_cache);

    if (!grouped)
        m_registry.registerBidiRelation(callback.GetMemoryPeer(), *cache, "mem");

    return id;
}

MCID TwoLevelCOMA::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, bool grouped)
{
    MCID id = m_clientMap.size();
    m_clientMap.resize(id + 1);

    size_t abstract_id;
    if (grouped)
    {
        abstract_id = m_numClients - 1;
    }
    else
    {
        abstract_id = m_numClients++;
    }

    size_t cache_id = abstract_id / m_numClientsPerCache;

    if (cache_id == m_caches.size())
    {
        // Add a cache
        if (m_caches.size() % m_numCachesPerLowRing == 0)
        {
            // First cache in a ring; add a directory
            NodeID firstCache = m_caches.size();

            stringstream name;
            name << "dir" << m_directories.size();
            Directory* dir = new Directory(name.str(), *this, GetClock(), firstCache, m_config);
            m_directories.push_back(dir);
        }

        stringstream name;
        name << "cache" << m_caches.size();
        Cache* cache = new Cache(name.str(), *this, GetClock(), m_caches.size(), m_config);
        m_caches.push_back(cache);
    }

    // Forward the registration to the cache associated with the processor

    Cache *cache = m_caches[cache_id];

    MCID id_in_cache = cache->RegisterClient(callback, process, traces, storages);

    m_clientMap[id] = make_pair(cache, id_in_cache);

    if (!grouped)
        m_registry.registerBidiRelation(callback.GetMemoryPeer(), *cache, "mem");

    return id;
}

void COMA::UnregisterClient(MCID id)
{
    // Forward the unregistration to the cache associated with the processor
    assert(id < m_clientMap.size());
    m_clientMap[id].first->UnregisterClient(m_clientMap[id].second);
}

bool COMA::Read(MCID id, MemAddr address)
{
    COMMIT
    {
        m_nreads++;
        m_nread_bytes += m_lineSize;
    }
    // Forward the read to the cache associated with the callback
    return m_clientMap[id].first->Read(m_clientMap[id].second, address);
}

bool COMA::Write(MCID id, MemAddr address, const MemData& data, WClientID wid)
{
    COMMIT
    {
        m_nwrites++;
        m_nwrite_bytes += m_lineSize;
    }
    // Forward the write to the cache associated with the callback
    return m_clientMap[id].first->Write(m_clientMap[id].second, address, data, wid);
}

COMA::COMA(const std::string& name, Simulator::Object& parent, Clock& clock, Config& config) :
    // Note that the COMA class is just a container for caches and directories.
    // It has no processes of its own.
    Simulator::Object(name, parent, clock),
    m_registry(config),
    m_numClientsPerCache(config.getValue<size_t>(*this, "NumClientsPerL2Cache")),
    m_numCachesPerLowRing(config.getValue<size_t>(*this, "NumL2CachesPerRing")),
    m_numClients(0),
    m_lineSize(config.getValue<size_t>("CacheLineSize")),
    m_config(config),
    m_selector(IBankSelector::makeSelector(*this,
                                           config.getValueOrDefault<string>(*this, "BankSelector", "XORFOLD"),
                                           config.getValue<size_t>(*this, "L2CacheNumSets"))),
    m_caches(),
    m_directories(),
    m_roots(config.getValue<size_t>(*this, "NumRootDirectories"), 0),
    m_traces(),
    m_ddr("ddr", *this, config, config.getValue<size_t>(*this, "NumRootDirectories")),
    m_clientMap(),
    m_nreads(0), m_nwrites(0), m_nread_bytes(0), m_nwrite_bytes(0)
{

    RegisterSampleVariableInObject(m_nreads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nread_bytes, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrites, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrite_bytes, SVC_CUMULATIVE);

    // Create the root directories
    if (!IsPowerOfTwo(m_roots.size()))
    {
        throw InvalidArgumentException(*this, "NumRootDirectories is not a power of two");
    }

    for (size_t i = 0; i < m_roots.size(); ++i)
    {
        stringstream rname;
        rname << "rootdir" << i;
        m_roots[i] = new RootDirectory(rname.str(), *this, clock, i, m_roots.size(), m_ddr, config);
    }

}

OneLevelCOMA::OneLevelCOMA(const std::string& name, Simulator::Object& parent, Clock& clock, Config& config) :
    COMA(name, parent, clock, config)
{ };

TwoLevelCOMA::TwoLevelCOMA(const std::string& name, Simulator::Object& parent, Clock& clock, Config& config) :
    COMA(name, parent, clock, config)
{ };


void OneLevelCOMA::Initialize()
{
    m_config.registerObject(*this, "coma");
    m_config.registerProperty(*this, "selector", m_selector->GetName());

    //
    // Figure out the layout of the top-level ring
    //
    std::vector<Node*> nodes(m_roots.size() + m_caches.size(), NULL);

    // First place root directories in their place in the ring.
    for (size_t i = 0; i < m_roots.size(); ++i)
    {
        // Tell the root directory how many rings there are
        m_roots[i]->SetNumRings(1);

        // Do an even (as possible) distribution
        size_t pos = i * m_caches.size() / m_roots.size() + i;

        // In case we map to an already used spot (for uneven distributions), find the next free spot
        while (nodes[pos] != NULL) pos = (pos + 1) % nodes.size();
        nodes[pos] = m_roots[i];
    }

    // Then fill up the gaps with the caches
    for (size_t p = 0, i = 0; i < m_caches.size(); ++i, ++p)
    {
        while (nodes[p] != NULL) ++p;
        nodes[p] = m_caches[i];
    }

    // Now connect everything on the top-level ring
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        Node *next = nodes[(i == 0 ? nodes.size() : i) - 1];
        Node *prev = nodes[(i + 1) % nodes.size()];
        nodes[i]->Connect(next, prev);

        m_config.registerRelation(*nodes[i], *next, "topring", true);
    }
}

void TwoLevelCOMA::Initialize()
{
    m_config.registerObject(*this, "coma");
    m_config.registerProperty(*this, "selector", m_selector->GetName());

    // Initialize the caches
    for (size_t i = 0; i < m_caches.size(); ++i)
    {
        DirectoryBottom* dir = &m_directories[i / m_numCachesPerLowRing]->m_bottom;
        const bool first = (i % m_numCachesPerLowRing == 0);
        const bool last  = (i % m_numCachesPerLowRing == m_numCachesPerLowRing - 1) || (i == m_caches.size() - 1);

        Node *next = first ? dir : static_cast<Node*>(m_caches[i-1]);
        Node *prev = last  ? dir : static_cast<Node*>(m_caches[i+1]);
        m_caches[i]->Connect(next, prev);

        m_config.registerRelation(*m_caches[i], *next, "l2ring", true);
    }

    // Connect the directories to the cache rings
    for (size_t i = 0; i < m_directories.size(); ++i)
    {
        Node *next = m_caches[std::min(i * m_numCachesPerLowRing + m_numCachesPerLowRing, m_caches.size()) - 1];
        Node *prev = m_caches[i * m_numCachesPerLowRing];
        m_directories[i]->m_bottom.Connect(next, prev);

        m_config.registerRelation(m_directories[i]->m_bottom, *next, "l2ring", true);
    }

    //
    // Figure out the layout of the top-level ring
    //
    std::vector<Node*> nodes(m_roots.size() + m_directories.size(), NULL);

    // First place root directories in their place in the ring.
    for (size_t i = 0; i < m_roots.size(); ++i)
    {
        // Tell the root directory how many rings there are
        m_roots[i]->SetNumRings(m_directories.size());

        // Do an even (as possible) distribution
        size_t pos = i * m_directories.size() / m_roots.size() + i;

        // In case we map to an already used spot (for uneven distributions), find the next free spot
        while (nodes[pos] != NULL) pos = (pos + 1) % nodes.size();
        nodes[pos] = m_roots[i];
    }

    // Then fill up the gaps with the directories
    for (size_t p = 0, i = 0; i < m_directories.size(); ++i, ++p)
    {
        while (nodes[p] != NULL) ++p;
        nodes[p] = &m_directories[i]->m_top;
    }

    // Now connect everything on the top-level ring
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        Node *next = nodes[(i == 0 ? nodes.size() : i) - 1];
        Node *prev = nodes[(i + 1) % nodes.size()];
        nodes[i]->Connect(next, prev);

        m_config.registerRelation(*nodes[i], *next, "topring", true);
    }
}

COMA::~COMA()
{
    for (size_t i = 0; i < m_caches.size(); ++i)
    {
        delete m_caches[i];
    }

    for (size_t i = 0; i < m_directories.size(); ++i)
    {
        delete m_directories[i];
    }

    for (size_t i = 0; i < m_roots.size(); ++i)
    {
        delete m_roots[i];
    }

    delete m_selector;
}

size_t COMA::GetNumCacheSets() const
{
    if (m_caches.empty())
        return 0;
    return m_caches[0]->GetNumSets();
}

size_t COMA::GetCacheAssociativity() const
{
    if (m_caches.empty())
        return 0;
    return m_caches[0]->GetAssociativity();
}

size_t COMA::GetDirectoryAssociativity() const
{
    if (m_directories.empty())
        return 0;
    return m_directories[0]->m_assoc;
}

void COMA::GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, uint64_t& nread_bytes, uint64_t& nwrite_bytes, uint64_t& nreads_ext, uint64_t& nwrites_ext) const
{
    nreads = m_nreads;
    nwrites = m_nwrites;
    nread_bytes = m_nread_bytes;
    nwrite_bytes = m_nwrite_bytes;

    uint64_t nre = 0, nwe = 0;
    for (size_t i = 0; i < m_roots.size(); ++i)
    {
        m_roots[i]->GetMemoryStatistics(nre, nwe);
        nreads_ext += nre;
        nwrites_ext += nwe;
    }
}

void COMA::Cmd_Info(ostream& out, const vector<string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "ranges")
    {
        return VirtualMemory::Cmd_Info(out, arguments);
    }
    out <<
    "The COMA Memory represents a hierarchical ring-based network of caches where each\n"
    "cache services several processors. Rings of caches are connected via directories\n"
    "to higher-level rings. One or more root directories at the top provide access to\n"
    "to off-chip storage.\n"
    "\n"
    "This memory uses the following mapping of lines to cache sets(banks): " << m_selector->GetName() <<
    "\n\n"
    "Supported operations:\n"
    "- info <component> ranges\n"
    "  Displays the currently reserved and allocated memory ranges\n\n"
    "- inspect <component> <start> <size>\n"
    "  Reads the specified number of bytes of raw data from memory from the\n"
    "  specified address\n\n"
    "- line <component> <address>\n"
    "  Finds the specified line in the system and prints its distributed state\n"
    "- trace <component> <address> [clear]\n"
    "  Sets or clears tracing for the specified address\n";
}

void COMA::Cmd_Line(ostream& out, const vector<string>& arguments) const
{
    // Parse argument
    MemAddr address = 0;
    char* endptr = NULL;
    if (arguments.size() == 1)
    {
        address = (MemAddr)strtoull( arguments[0].c_str(), &endptr, 0 );
    }

    if (arguments.size() != 1 || *endptr != '\0')
    {
        out << "Usage: line <mem> <address>" << endl;
        return;
    }

    // Check the root directories
    bool printed = false;
    for (auto p = m_roots.begin(); p != m_roots.end(); ++p)
    {
        auto line = (*p)->FindLine(address, true);
        if (line != NULL)
        {
            const char* state = (line->state == RootDirectory::LINE_LOADING) ? "loading" : "loaded";
            out << (*p)->GetFQN() << ": " << state << endl;
            printed = true;
        }
    }
    if (printed) out << endl;

    // Check the directories
    printed = false;
    for (auto p = m_directories.begin(); p != m_directories.end(); ++p)
    {
        auto line = (*p)->FindLine(address);
        if (line != NULL)
        {
            out << (*p)->GetFQN() << ": present" << endl;
            printed = true;
        }
    }
    if (printed) out << endl;

    // Check the caches
    printed = false;
    for (auto p = m_caches.begin(); p != m_caches.end(); ++p)
    {
        auto line = (*p)->FindLine(address);
        if (line != NULL)
        {
            const char* state = (line->state == Cache::LINE_LOADING) ? "loading" : "loaded";
            out << (*p)->GetFQN() << ": " << state << ", " << line->tokens << " tokens" << endl;
            printed = true;
        }
    }
    if (printed) out << endl;
}

void COMA::Cmd_Trace(ostream& out, const vector<string>& arguments)
{
    // Parse argument
    MemAddr address;
    char* endptr = NULL;
    if (!arguments.empty())
    {
        address = (MemAddr)strtoull( arguments[0].c_str(), &endptr, 0 );
    }

    if (arguments.empty() || *endptr != '\0')
    {
        out << "Usage: trace <mem> <address> [clear]" << endl;
        return;
    }

    if (arguments.size() > 1 && arguments[1] == "clear")
    {
        m_traces.erase(address);
        out << "Disabled tracing of address 0x" << hex << address << endl;
    }
    else
    {
        m_traces.insert(address);
        out << "Enabled tracing of address 0x" << hex << address << endl;
    }
}

}
