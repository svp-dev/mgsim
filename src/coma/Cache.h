#ifndef COMA_CACHE_H
#define COMA_CACHE_H

#include "Node.h"
#include "../ports.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class COMA::Cache : public COMA::Node
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
        bool         forward;   ///< Loading: forward reply when received
        bool         dirty;     ///< Dirty: line has been written to
        unsigned int updating;  ///< Number of REQUEST_UPDATEs pending on this line
        signed   int tokens;    ///< Number of tokens held here
        unsigned int hops;      ///< Loading, forward: Number of caches between us and the next that wants it
        bool         valid[MAX_MEMORY_OPERATION_SIZE]; ///< Validity bitmask
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
    size_t                        m_numCaches;
    std::vector<IMemoryCallback*> m_clients;
    ArbitratedService             p_lines;
    std::vector<Line>             m_lines;
    std::vector<char>             m_data;

    // Processes
    Process p_Requests;
    Process p_OutNext;
    Process p_OutPrev;
    Process p_InPrev;
    Process p_InNext;

    // Incoming requests from the processors
    // First arbitrate, then buffer (models a bus)
    ArbitratedService p_bus;
    Buffer<Request>   m_requests;
    Buffer<MemData>   m_responses;
    
    Line* FindLine(MemAddr address);
    Line* AllocateLine(MemAddr address, bool empty_only);
    bool  EvictLine(Line* line, const Request& req);

    // Processes
    Result DoRequests();
    Result DoForwardNext();
    Result DoForwardPrev();
    Result DoReceiveNext();
    Result DoReceivePrev();

    Result OnReadRequest(const Request& req);
    Result OnWriteRequest(const Request& req);
    bool OnRequestReceived(Message* msg);
    bool OnResponseReceived(Message* msg);
    bool OnReadCompleted(MemAddr addr, const MemData& data);
public:
    Cache(const std::string& name, COMA& parent, size_t numCaches, const Config& config);
    
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
