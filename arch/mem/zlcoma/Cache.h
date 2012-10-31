#ifndef ZLCOMA_CACHE_H
#define ZLCOMA_CACHE_H

#include "Node.h"
#include <sim/inspect.h>
#include <arch/BankSelector.h>

#include <queue>
#include <set>

namespace Simulator
{

class ZLCOMA::Cache : public ZLCOMA::Node, public Inspect::Interface<Inspect::Read>
{
public:
    struct Line
    {
        bool    valid;  // Line in use?
        MemAddr tag;    // Tag of address for this line
        CycleNo time;   // Last access time for LRU eviction

        // The data stored in this line
        char data[MAX_MEMORY_OPERATION_SIZE];

        // The bitmask indicates the valid sections of the line,
        // when writes are stored by replies come back with the
        // whole cache line.
        bool bitmask[MAX_MEMORY_OPERATION_SIZE];

        // Tokens held by this line
        unsigned int tokens;

        // Does this line have the priority token?
        bool priority;

        // Does this line have a pending read and/or write?
        bool pending_read;
        bool pending_write;

        // Whether we have (or want, when pending) all the tokens
        bool dirty;

        // Whether the line's tokens are transient.
        bool transient;

        // Temporary hack for storing write-acknowledgements
        std::vector<WriteAck> ack_queue;

        Line()
        : valid(false), tag(0), time(0), tokens(0), priority(false),
            pending_read(false), pending_write(false), dirty(false),
            transient(false), ack_queue()
        {}
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

    IBankSelector&                m_selector;
    size_t                        m_lineSize;
    size_t                        m_assoc;
    size_t                        m_sets;
    bool                          m_inject;
    CacheID                       m_id;
    std::vector<IMemoryCallback*> m_clients;
    StorageTraceSet               m_storages;
    ArbitratedService<>           p_lines;
    std::vector<Line>             m_lines;

    // Statistics
    uint64_t                      m_numHits;
    uint64_t                      m_numMisses;
    uint64_t                      m_numConflicts;
    uint64_t                      m_numResolved;

    // Processes
    Process p_Requests;
    Process p_In;

    // Incoming requests from the processors
    // First arbitrate, then buffer (models a bus)
    ArbitratedService<PriorityCyclicArbitratedPort> p_bus;
    Buffer<Request>     m_requests;
    Buffer<MemData>     m_responses;

    Result OnAcquireTokensRem(Message* msg);
    Result OnAcquireTokensRet(Message* msg);
    Result OnReadRem(Message* msg);
    Result OnReadRet(Message* msg);
    Result OnEviction(Message* msg);

    Result OnReadRequest(const Request& req);
    Result OnWriteRequest(const Request& req);
    Result OnMessageReceived(Message* msg);

    Line* FindLine(MemAddr address);
    Line* GetEmptyLine(MemAddr address, MemAddr& tag);
    Line* GetReplacementLine(MemAddr address, MemAddr& tag);

    bool  ClearLine(Line* line);
    bool  EvictLine(Line* line, const Request& req);
    bool  AcknowledgeQueuedWrites(Line* line);
    bool  OnReadCompleted(MemAddr addr, const char * data);

    // Processes
    Result DoRequests();
    Result DoReceive();

public:
    Cache(const std::string& name, ZLCOMA& parent, Clock& clock, CacheID id, Config& config);

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
    const Line* FindLine(MemAddr address) const;

    MCID RegisterClient  (IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage);
    void UnregisterClient(MCID id);
    bool Read (MCID id, MemAddr address);
    bool Write(MCID id, MemAddr address, const MemData& data, WClientID wid);
};

}

#endif
