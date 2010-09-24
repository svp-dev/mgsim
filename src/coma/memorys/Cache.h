#ifndef ZLCOMA_CACHE_H
#define ZLCOMA_CACHE_H

#include "Node.h"
#include "../../ports.h"
#include <queue>
#include <set>

namespace Simulator
{

class ZLCOMA::Cache : public ZLCOMA::Node
{
public:
    struct Line
    {
        bool    valid;
        MemAddr tag;
        CycleNo time;

        char data   [MAX_MEMORY_OPERATION_SIZE];
        bool bitmask[MAX_MEMORY_OPERATION_SIZE]; // bit mask is defined to hold written data before reply comes back
                                                 // it does not always represent the validness of word segment within the line
                                                // for WP states, bitmask only indicates the newly written data.

        unsigned int tokencount;

        bool dirty;         // Whether the cache-line contains dirty data?

        // invalidated is focusing on state
        bool invalidated;   // whether the line is already invalidated
                            // invalidated-line's token will not count.
                            // or say they are counted as zero to remote request.
                            // but to local request, they are still available,
                            // which can easily represent the validness of data
                            // without consulting the local bit-mask.
                            // thus, to remote request,
                            // invalidated lines will not change their token number.

        bool priority;      // priority flag represent priority token in the system,
                            // which in this system will facilitate the invalidation
                            // similar to micro06-ring paper
    
        bool breserved;     // reserved for special purpose.
                            // for instance the owner-ev to R will transfer the R to W state
                            // however it shouldn't be carried out directly, but with reserved flag
                            // RESERVED

        // tlock is focusing on token
        // when tlock == true, invalidated must be true
        bool tlock;         // token lock represent that all the tokens in the line are locked,
                            // which can only be unlocked by priority token
    
        bool pending;       // pending request, either read or write pending

        unsigned int gettokenglobalvisible() const
        {
            return (invalidated || tlock) ? 0 : tokencount;
        }

        // check whether the line state signify the line is complete
        bool IsLineAtCompleteState() const
        {
            assert(valid);
            if (!dirty && pending)
            {
                return !contains(bitmask, bitmask + MAX_MEMORY_OPERATION_SIZE, false);
            }
            return (!dirty || tokencount != 0);
        }
    };

private:    
    struct Request : public MemData
    {
        bool         write;
        MemAddr      address;
        unsigned int client;
        TID          tid;
    };
    
    class MergeStoreBuffer;

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

    // Merge Store Buffer Implementation
    // 0. pending line can lock the line itself itself by further access (any further accesses or specific access -- decided by llock TBD)
    // 1. read on write pending lines (the corresponding merged line should not be locked) with tokens, even locked tokens, can be performed on the line directly and return.
    // 2. read on read pending line (the corresponding merged line should not be locked )with tokens, even ghost tokens, can be perfomed on the line directly and return.
    // 3. write on write pending lines with priority token can be performed directly according to the following rules
    //    a. it can be performed on the merged store line but no return, if the AT request was from a different family.
    //    b. if the AT request was from the same family, the request can be performed directly on the line and return immediately.  [N/A yet]
    //    *. if no knowledge about the family, then the request should be performed on the MSB without return.
    // 4. write on read pending line with priority token can be performed on the line directly and no immediate return,
    //    but the merged AT/AD needs to be send out and also change the state to writepending immediately 
    //    (additional line state needs to remember the outgoing AT request) [N/A yet]
    // 5. write on writepending lines without priority tokens may write to merge store buffer if availablei and no immediate return.
    //    a. write to unlocked merged lines can proceed directly with no immediate return.
    //    b. write to locked merged lines will be blocked (or proceeded according to previous request initiators on the merge buffer TBD)
    // 6. read on the writepending lines without priority tokens are more delicate, since read always needs to read the whole line
    //    a. if a merged buffer is not locked, it can proceeed, otherwise it will be blocked to suspend in the normal queue
    //    b. read might need to combine both line and merged store buffer, as long as the data are available, it can read and return;
    //       merged store buffer has the priority, if both line and merged buffer has the data, line data is ignored
    //    c. when a read fails, the buffer slot/line will be set as locked (or a bitmapping for the read initiator is updated to improve the performance TBD) 
    //       and preventing others (illegal ones) from access the data
    // 7. write on read pending line without priority token will have to write to merge buffer (or maybe just suspend, since it might waste too much MSB TBD) [N/A yet]
    //    a. write can proceed with no immediate reply, 
    //    b. update on the merged request is of course automatic
    // 8. whenever the writepending line gets the priority token, the line will directly merge with the corresponding merge store buffer line
    //    the merged request for the merge store line will be eliminated, 
    //    and all the following write requests suspended on the merged line will be directly returend without processing.
    //    this can happen also after the AD/AT request returns
    // 9. whenever read pending line get priority token, (T cannot possibly get priority token, only R can), merge write buffer will be directly merged with the line 
    //    and change the state to write pending state, merged request has to be sent out to invalidate others.    [N/A yet]
    //
    // *  any request suspended on the merge store line will be queued and a merged request will gather all the data in those request
    // *  any unavailablility of the merge store buffer will cause the incoming request to suspend on normal queues
    // *  merge buffer slot/line are locked or not is decided by the llock variable
    // *  all request without immediate reply to the processor, will have to be queued in the merged line queue. 
    //    the merged line queue will be served before any other requests suspended on the line or even in the pipeline
    // *  change in handling AD/RS/SR return on write pending state

    // Merge Store buffer module provide the merge capability on stores on the pending lines
    MergeStoreBuffer* m_msbModule;

    bool OnAcquireTokenRem(Message* msg);
    bool OnAcquireTokenRet(Message* msg);
    bool OnAcquireTokenDataRem(Message* msg);
    bool OnAcquireTokenDataRet(Message* msg);
    bool OnDisseminateTokenData(Message* msg);
    bool OnPostAcquirePriorityToken(Line* line, MemAddr address);

    Line* FindLine(MemAddr address);
    Line* GetEmptyLine(MemAddr address);
    Line* GetReplacementLine(MemAddr address);
    bool  EvictLine(Line* line);

    // Processes
    Result DoRequests();
    Result DoReceive();

    Result OnReadRequest(const Request& req);
    Result OnWriteRequest(const Request& req);
    bool OnMessageReceived(Message* msg);
    bool OnReadCompleted(MemAddr addr, const MemData& data);

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
