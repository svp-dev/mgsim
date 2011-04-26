#ifndef ZLCOMA_CACHE_H
#define ZLCOMA_CACHE_H

#include "Node.h"
#include "sim/inspect.h"
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
    };

private:    
    struct Request : public MemData
    {
        bool         write;
        MemAddr      address;
        unsigned int client;
        TID          tid;
    };
    
    size_t                        m_lineSize;
    size_t                        m_assoc;
    size_t                        m_sets;
    size_t                        m_numTokens;
    bool                          m_inject;    
    CacheID                       m_id;
    std::vector<IMemoryCallback*> m_clients;
    ArbitratedService<>           p_lines;
    std::vector<Line>             m_lines;
    std::vector<char>             m_data;

    // Statistics
    uint64_t                      m_numHits;
    uint64_t                      m_numMisses;

    // Processes
    Process p_Requests;
    Process p_In;

    // Incoming requests from the processors
    // First arbitrate, then buffer (models a bus)
    ArbitratedService<> p_bus;
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
    Line* GetEmptyLine(MemAddr address);
    Line* GetReplacementLine(MemAddr address);

    bool  ClearLine(Line* line);
    bool  EvictLine(Line* line, const Request& req);
    bool  AcknowledgeQueuedWrites(Line* line);
    bool  OnReadCompleted(MemAddr addr, const MemData& data);

    // Processes
    Result DoRequests();
    Result DoReceive();

public:
    Cache(const std::string& name, ZLCOMA& parent, Clock& clock, CacheID id, size_t numCaches, const Config& config);

    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
    const Line* FindLine(MemAddr address) const;

    void RegisterClient  (PSize pid, IMemoryCallback& callback, const Process* processes[]);
    void UnregisterClient(PSize pid);
    bool Read (PSize pid, MemAddr address, MemSize size);
    bool Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid);
};

}

#endif
