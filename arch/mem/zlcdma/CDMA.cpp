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

MCID ZLCDMA::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, bool grouped)
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

        auto l2Assoc = GetConf("L2CacheAssociativity", size_t);
        auto enableInjection = GetConf("EnableCacheInjection", bool);

        if (m_caches.size() % m_numCachesPerDir == 0)
        {
            // First cache in a ring; add a directory
            CacheID firstCache = m_caches.size();
            Directory* dir = new Directory("dir" + std::to_string(m_directories.size()), *this, GetClock(), firstCache, l2Assoc, m_numCachesPerDir);
            m_directories.push_back(dir);
        }

        Cache* cache = new Cache("cache" + std::to_string(m_caches.size()), *this, GetClock(), m_caches.size(), l2Assoc, enableInjection);
        m_caches.push_back(cache);
    }

    // Forward the registration to the cache associated with the processor

    Cache *cache = m_caches[cache_id];

    MCID id_in_cache = cache->RegisterClient(callback, process, traces, storages);

    m_clientMap[id] = make_pair(cache, id_in_cache);

    if (!grouped)
        RegisterModelBidiRelation(callback.GetMemoryPeer(), *cache, "mem");

    return id;
}

void ZLCDMA::UnregisterClient(MCID id)
{
    // Forward the unregistration to the cache associated with the processor
    assert(id < m_clientMap.size());
    m_clientMap[id].first->UnregisterClient(m_clientMap[id].second);
}

bool ZLCDMA::Read(MCID id, MemAddr address)
{
    COMMIT
    {
        m_nreads++;
        m_nread_bytes += m_lineSize;
    }
    // Forward the read to the cache associated with the callback
    return m_clientMap[id].first->Read(m_clientMap[id].second, address);
}

bool ZLCDMA::Write(MCID id, MemAddr address, const MemData& data, WClientID wid)
{
    COMMIT
    {
        m_nwrites++;
        m_nwrite_bytes += m_lineSize;
    }
    // Forward the write to the cache associated with the callback
    return m_clientMap[id].first->Write(m_clientMap[id].second, address, data, wid);
}

// Note that the CDMA class is just a container for caches and directories.
// It has no processes of its own.
ZLCDMA::ZLCDMA(const std::string& name, Simulator::Object& parent, Clock& clock)
  : Simulator::Object(name, parent),
    m_clock(clock),
    m_numClientsPerCache(GetConf("NumClientsPerL2Cache", size_t)),
    m_numCachesPerDir   (GetConf("NumL2CachesPerRing", size_t)),
    m_numClients(0),
    m_lineSize(GetTopConf("CacheLineSize", size_t)),
    m_selector(IBankSelector::makeSelector(*this,
                                           GetConfOpt("BankSelector", string, "XORFOLD"),
                                           GetConf("L2CacheNumSets", size_t))),
    m_caches(),
    m_directories(),
    m_roots(GetConf("NumRootDirectories", size_t), 0),
    m_traces(),
    m_ddr("ddr", *this, GetConf("NumRootDirectories", size_t)),
    m_clientMap(),
    InitSampleVariable(nreads, SVC_CUMULATIVE), InitSampleVariable(nwrites, SVC_CUMULATIVE), InitSampleVariable(nread_bytes, SVC_CUMULATIVE), InitSampleVariable(nwrite_bytes, SVC_CUMULATIVE)
{

    // Create the root directories
    if (!IsPowerOfTwo(m_roots.size()))
    {
        throw InvalidArgumentException(*this, "NumRootDirectories is not a power of two");
    }

    auto l2Assoc = GetConf("L2CacheAssociativity", size_t);
    for (size_t i = 0; i < m_roots.size(); ++i)
    {
        m_roots[i] = new RootDirectory("rootdir" + std::to_string(i), *this, clock, i, m_roots.size(), m_ddr, l2Assoc, m_numCachesPerDir);
    }

}

void ZLCDMA::Initialize()
{
    RegisterModelObject(*this, "coma");
    RegisterModelProperty(*this, "selector", m_selector->GetName());

    // Initialize the caches
    for (size_t i = 0; i < m_caches.size(); ++i)
    {
        DirectoryBottom* dir = &m_directories[i / m_numCachesPerDir]->m_bottom;
        const bool first = (i % m_numCachesPerDir == 0);
        const bool last  = (i % m_numCachesPerDir == m_numCachesPerDir - 1) || (i == m_caches.size() - 1);

        Node *next = first ? dir : static_cast<Node*>(m_caches[i-1]);
        Node *prev = last  ? dir : static_cast<Node*>(m_caches[i+1]);
        m_caches[i]->Initialize(next, prev);

        RegisterModelRelation(*m_caches[i], *next, "l2ring");
    }

    // Connect the directories to the cache rings
    for (size_t i = 0; i < m_directories.size(); ++i)
    {
        Node *next = m_caches[std::min(i * m_numCachesPerDir + m_numCachesPerDir, m_caches.size()) - 1];
        Node *prev = m_caches[i * m_numCachesPerDir];
        m_directories[i]->m_bottom.Initialize(next, prev);

        RegisterModelRelation(m_directories[i]->m_bottom, *next, "l2ring");
    }

    //
    // Figure out the layout of the top-level ring
    //
    std::vector<Node*> nodes(m_roots.size() + m_directories.size(), NULL);

    // First place root directories in their place in the ring.
    for (size_t i = 0; i < m_roots.size(); ++i)
    {
        // Tell the root directory how many directories there are
        m_roots[i]->SetNumDirectories(m_directories.size());

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
        nodes[i]->Initialize(next, prev);

        RegisterModelRelation(*nodes[i], *next, "topring");
    }
}

ZLCDMA::~ZLCDMA()
{
    for (auto c : m_caches)
        delete c;

    for (auto d : m_directories)
        delete d;

    for (auto r : m_roots)
        delete r;

    delete m_selector;
}

void ZLCDMA::GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, uint64_t& nread_bytes, uint64_t& nwrite_bytes, uint64_t& nreads_ext, uint64_t& nwrites_ext) const
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

void ZLCDMA::Cmd_Info(ostream& out, const vector<string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "ranges")
    {
        return VirtualMemory::Cmd_Info(out, arguments);
    }
    out <<
    "The CDMA Memory represents a hierarchical ring-based network of caches where each\n"
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

void ZLCDMA::Cmd_Line(ostream& out, const vector<string>& arguments) const
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
    for (std::vector<RootDirectory*>::const_iterator p = m_roots.begin(); p != m_roots.end(); ++p)
    {
        const RootDirectory::Line* line = static_cast<const RootDirectory*>(*p)->FindLine(address);
        if (line != NULL)
        {
            const char* state = "present";
            out << (*p)->GetName() << ": " << state << endl;
            printed = true;
        }
    }
    if (printed) out << endl;

    // Check the directories
    printed = false;
    for (std::vector<Directory*>::const_iterator p = m_directories.begin(); p != m_directories.end(); ++p)
    {
        const Directory::Line* line = static_cast<const Directory*>(*p)->FindLine(address);
        if (line != NULL)
        {
            out << (*p)->GetName() << ": present" << endl;
            printed = true;
        }
    }
    if (printed) out << endl;

    // Check the caches
    printed = false;
    for (std::vector<Cache*>::const_iterator p = m_caches.begin(); p != m_caches.end(); ++p)
    {
        const Cache::Line* line = static_cast<const Cache*>(*p)->FindLine(address);
        if (line != NULL)
        {
            const char* state = "present";
            out << (*p)->GetName() << ": " << state << ", " << line->tokens << " tokens" << endl;
            printed = true;
        }
    }
    if (printed) out << endl;
}

void ZLCDMA::Cmd_Trace(ostream& out, const vector<string>& arguments)
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
