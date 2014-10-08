// -*- c++ -*-
#ifndef CDMA_CACHE_H
#define CDMA_CACHE_H

#include "Node.h"
#include <sim/inspect.h>
#include <arch/BankSelector.h>

#include <queue>
#include <set>

class Config;

namespace Simulator
{

class CDMA::Cache : public CDMA::Node, public Inspect::Interface<Inspect::Read>
{
public:
    enum LineState
    {
        LINE_EMPTY,     ///< Empty, can be used.
        LINE_LOADING,   ///< Allocated, read request sent.
        LINE_FULL,      ///< Allocated and data present.
    };

    struct Line
    {
        LineState    state;     ///< State of the line
        MemAddr      tag;       ///< Tag of the line
        char*        data;      ///< Data of the line
        CycleNo      access;    ///< Last access time (for LRU replacement)
        unsigned int tokens;    ///< Number of tokens in this line
        bool         dirty;     ///< Dirty: line has been written to
        unsigned int updating;  ///< Number of REQUEST_UPDATEs pending on this line
        bool         valid[MAX_MEMORY_OPERATION_SIZE]; ///< Validity bitmask
    };

private:
    struct Request
    {
        MemData      mdata;
        bool         write;
        MemAddr      address;
        unsigned int client;
        WClientID    wid;
    };

    size_t                        m_lineSize;
    size_t                        m_assoc;
    size_t                        m_sets;
    IBankSelector*                m_selector;
    std::vector<IMemoryCallback*> m_clients;
    StorageTraceSet               m_storages;
    ArbitratedService<>           p_lines;
    std::vector<Line>             m_lines;
    std::vector<char>             m_data;

    // Statistics

    /* reads */
    DefineSampleVariable(uint64_t, numRAccesses);
    DefineSampleVariable(uint64_t, numHardRConflicts);
    DefineSampleVariable(uint64_t, numStallingREvictions);
    DefineSampleVariable(uint64_t, numREvictions);
    DefineSampleVariable(uint64_t, numStallingRLoads);
    DefineSampleVariable(uint64_t, numRLoads);
    DefineSampleVariable(uint64_t, numRFullHits);
    DefineSampleVariable(uint64_t, numStallingRHits);
    DefineSampleVariable(uint64_t, numLoadingRMisses);

    /* writes */
    DefineSampleVariable(uint64_t, numWAccesses);
    DefineSampleVariable(uint64_t, numHardWConflicts);
    DefineSampleVariable(uint64_t, numStallingWEvictions);
    DefineSampleVariable(uint64_t, numWEvictions);
    DefineSampleVariable(uint64_t, numStallingWLoads);
    DefineSampleVariable(uint64_t, numWLoads);
    DefineSampleVariable(uint64_t, numStallingWHits);
    DefineSampleVariable(uint64_t, numWEHits);
    DefineSampleVariable(uint64_t, numLoadingWUpdates);
    DefineSampleVariable(uint64_t, numSharedWUpdates);
    DefineSampleVariable(uint64_t, numStallingWUpdates);

    /* accesses from network */
    DefineSampleVariable(uint64_t, numReceivedMessages);
    DefineSampleVariable(uint64_t, numIgnoredMessages);
    DefineSampleVariable(uint64_t, numForwardStalls);

    // read and read completions
    DefineSampleVariable(uint64_t, numNetworkRHits);
    DefineSampleVariable(uint64_t, numRCompletions);
    DefineSampleVariable(uint64_t, numStallingRCompletions);
    // evictions
    DefineSampleVariable(uint64_t, numInjectedEvictions);
    DefineSampleVariable(uint64_t, numMergedEvictions);
    // updates
    DefineSampleVariable(uint64_t, numStallingWCompletions);
    DefineSampleVariable(uint64_t, numWCompletions);
    DefineSampleVariable(uint64_t, numNetworkWHits);
    DefineSampleVariable(uint64_t, numStallingWSnoops);

    // Processes
    Process p_Requests;
    Process p_In;

    // Incoming requests from the processors
    // First arbitrate, then buffer (models a bus)
    ArbitratedService<PriorityCyclicArbitratedPort> p_bus;
    Buffer<Request>     m_requests;
    Buffer<MemData>     m_responses;

    Line* FindLine(MemAddr address);
    Line* AllocateLine(MemAddr address, bool empty_only, MemAddr *ptag = NULL);
    bool  EvictLine(Line* line, const Request& req);

    // Processes
    Result DoRequests();
    Result DoReceive();

    Result OnReadRequest(const Request& req);
    Result OnWriteRequest(const Request& req);
    bool OnMessageReceived(Message* msg);
    bool OnReadCompleted(MemAddr addr, const char * data);

    // Administrative
    friend class CDMA;
public:
    Cache(const std::string& name, CDMA& parent, Clock& clock, NodeID id, size_t refAssoc, size_t refNumSets);
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;
    ~Cache();

    size_t GetLineSize() const { return m_lineSize; }
    size_t GetNumSets() const { return m_sets; }
    size_t GetNumLines() const override;

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

    MCID RegisterClient  (IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages);
    void UnregisterClient(MCID id);
    bool Read (MCID id, MemAddr address);
    bool Write(MCID id, MemAddr address, const MemData& data, WClientID wid);
};

}
#endif
