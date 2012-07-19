#include "Cache.h"
#include "sim/config.h"
#include "sim/sampling.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <iomanip>
using namespace std;

namespace Simulator
{

// When we insert a message into the ring, we want at least one slots
// available in the buffer to avoid deadlocking the ring network. This
// is not necessary for forwarding messages.
static const size_t MINSPACE_INSERTION = 2;
static const size_t MINSPACE_FORWARD   = 1;

MCID COMA::Cache::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage)
{
    MCID index = m_clients.size();
    m_clients.resize(index + 1);

    m_clients[index] = &callback;
    
    p_bus.AddCyclicProcess(process);
    traces = m_requests;
    
    m_storages *= opt(storage);
    p_Requests.SetStorageTraces(m_storages ^ GetOutgoingTrace());
    p_In.SetStorageTraces(opt(m_storages ^ GetOutgoingTrace()));

    return index;
}

void COMA::Cache::UnregisterClient(MCID id)
{
    assert(m_clients[id] != NULL);
    m_clients[id] = NULL;
}

// Called from the processor on a memory read (typically a whole cache-line)
// Just queues the request.
bool COMA::Cache::Read(MCID id, MemAddr address)
{
    assert(address % m_lineSize == 0);
    
    // We need to arbitrate between the different processes on the cache,
    // and then between the different clients. There are 2 arbitrators for this.
    if (!p_bus.Invoke())
    {
        // Arbitration failed
        DeadlockWrite("Unable to acquire bus for read");
        return false;
    }
    
    Request req;
    req.address = address;
    req.write   = false;
    
    // Client should have been registered
    assert(m_clients[id] != NULL);

    if (!m_requests.Push(req))
    {
        // Buffer was full
        DeadlockWrite("Unable to push read request into buffer");
        return false;
    }
    
    return true;
}

// Called from the processor on a memory write (can be any size with write-through/around)
// Just queues the request.
bool COMA::Cache::Write(MCID id, MemAddr address, const MemData& data, WClientID wid)
{
    assert(address % m_lineSize == 0);

    // We need to arbitrate between the different processes on the cache,
    // and then between the different clients. There are 2 arbitrators for this.
    if (!p_bus.Invoke())
    {
        // Arbitration failed
        DeadlockWrite("Unable to acquire bus for write");
        return false;
    }
    
    Request req;
    req.address = address;
    req.write   = true;
    req.client  = id;
    req.wid     = wid;
    COMMIT{
    std::copy(data.data, data.data + m_lineSize, req.data);
    std::copy(data.mask, data.mask + m_lineSize, req.mask);
    }

    // Client should have been registered
    assert(m_clients[req.client] != NULL);
    
    if (!m_requests.Push(req))
    {
        // Buffer was full
        DeadlockWrite("Unable to push write request into buffer");
        return false;
    }
    
    // Snoop the write back to the other clients
    for (size_t i = 0; i < m_clients.size(); ++i)
    {
        IMemoryCallback* client = m_clients[i];
        if (client != NULL && i != req.client)
        {
            if (!client->OnMemorySnooped(req.address, req.data, req.mask))
            {
                DeadlockWrite("Unable to snoop data to cache clients");
                return false;
            }
        }
    }
    
    return true;
}

// Attempts to find a line for the specified address.
COMA::Cache::Line* COMA::Cache::FindLine(MemAddr address)
{
    MemAddr tag;
    size_t  setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line& line = m_lines[set + i];
        if (line.state != LINE_EMPTY && line.tag == tag)
        {
            // The wanted line was in the cache
            return &line;
        }
    }
    return NULL;
}

// Attempts to find a line for the specified address.
const COMA::Cache::Line* COMA::Cache::FindLine(MemAddr address) const
{
    MemAddr tag;
    size_t  setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        const Line& line = m_lines[set + i];
        if (line.state != LINE_EMPTY && line.tag == tag)
        {
            // The wanted line was in the cache
            return &line;
        }
    }
    return NULL;
}

// Attempts to allocate a line for the specified address.
// If empty_only is true, only empty lines will be considered.
COMA::Cache::Line* COMA::Cache::AllocateLine(MemAddr address, bool empty_only, MemAddr* ptag)
{
    MemAddr tag;
    size_t  setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

    // Find the line
    Line* empty   = NULL;
    Line* replace = NULL;
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line& line = m_lines[set + i];
        if (line.state == LINE_EMPTY)
        {
            // Empty, unused line, remember this one
            DeadlockWrite("New line, tag %#016llx: allocating empty line %zu from set %zu", (unsigned long long)tag, i, setindex);
            empty = &line;
        }
        else if (!empty_only)
        {
            // We're also considering non-empty lines; use LRU
            assert(line.tag != tag);
            DeadlockWrite("New line, tag %#016llx: considering busy line %zu from set %zu, tag %#016llx, state %u, updating %u, access %llu",
                          (unsigned long long)tag,
                          i, setindex,
                          (unsigned long long)line.tag, (unsigned)line.state, (unsigned)line.updating, (unsigned long long)line.access);
            if (line.state != LINE_LOADING && line.updating == 0 && (replace == NULL || line.access < replace->access))
            {
                // The line is available to be replaced and has a lower LRU rating,
                // remember it for replacing
                replace = &line;
            }
        }
    }
    
    // The line could not be found, allocate the empty line or replace an existing line
    if (ptag) *ptag = tag;
    return (empty != NULL) ? empty : replace;
}

bool COMA::Cache::EvictLine(Line* line, const Request& req)
{
    // We never evict loading or updating lines
    assert(line->state != LINE_LOADING);
    assert(line->updating == 0);

    size_t setindex = (line - &m_lines[0]) / m_assoc;
    MemAddr address = m_selector.Unmap(line->tag, setindex) * m_lineSize;
    
    TraceWrite(address, "Evicting with %u tokens due to miss for address %#016llx", line->tokens, (unsigned long long)req.address);
    
    Message* msg = NULL;
    COMMIT
    {
        msg = new Message;
        msg->type      = Message::EVICTION;
        msg->address   = address;
        msg->ignore    = false;
        msg->sender    = m_id;
        msg->tokens    = line->tokens;
        msg->dirty     = line->dirty;
        std::copy(line->data, line->data + m_lineSize, msg->data.data);
    }
    
    if (!SendMessage(msg, MINSPACE_INSERTION))
    {
        DeadlockWrite("Unable to buffer eviction request for next node");
        return false;
    }
    
    // Send line invalidation to caches
    if (!p_bus.Invoke())
    {
        DeadlockWrite("Unable to acquire the bus for sending invalidation");
        return false;
    }

    for (std::vector<IMemoryCallback*>::const_iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (*p != NULL && !(*p)->OnMemoryInvalidated(address))
        {
            DeadlockWrite("Unable to send invalidation to clients");
            return false;
        }
    }
    
    COMMIT{ line->state = LINE_EMPTY; }
    return true;
}

// Called when a message has been received from the previous node in the chain
bool COMA::Cache::OnMessageReceived(Message* msg)
{
    assert(msg != NULL);

    // We need to grab p_lines because it also arbitrates access to the
    // outgoing ring buffer.    
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to acquire lines");
        return false;
    }
    
    if (msg->ignore || (msg->type == Message::REQUEST_DATA_TOKEN && msg->sender != m_id))
    {
        // This is either
        // * a message that should ignored, or
        // * a read response that has not reached its origin yet
        // Just forward it
        if (!SendMessage(msg, MINSPACE_FORWARD))
        {
            DeadlockWrite("Unable to buffer forwarded request for next node");
            ++m_numForwardStalls;
            return false;
        }
        COMMIT { ++m_numIgnoredMessages; }
        return true;
    }
    
    Line* line = FindLine(msg->address);   
    switch (msg->type)
    {
    case Message::REQUEST:
    case Message::REQUEST_DATA:
        // Some cache had a read miss. See if we have the line.        

        if (line != NULL && line->state == LINE_FULL)
        {
            // We have a copy of the line
            if (line->tokens > 1)
            {
                // We can give the request data and tokens
                TraceWrite(msg->address, "Received Read Request; Attaching data and tokens");
                        
                COMMIT
                {
                    msg->type   = Message::REQUEST_DATA_TOKEN;
                    msg->tokens = 1;
                    msg->dirty  = line->dirty;
                    std::copy(line->data, line->data + m_lineSize, msg->data.data);

                    line->tokens -= msg->tokens;
                                
                    // Also update last access time.
                    line->access = GetKernel()->GetCycleNo();
                }
            }
            else if (msg->type == Message::REQUEST)
            {
                // We can only give the request data, not tokens
                TraceWrite(msg->address, "Received Read Request; Attaching data");

                COMMIT
                {
                    msg->type  = Message::REQUEST_DATA;
                    msg->dirty = line->dirty;
                    std::copy(line->data, line->data + m_lineSize, msg->data.data);
                }
            }

            // Statistics
            COMMIT{ ++m_numNetworkRHits; }
        }
        else
            COMMIT{ ++m_numIgnoredMessages; }

        // Forward the message.
        if (!SendMessage(msg, MINSPACE_FORWARD))
        {
            DeadlockWrite("Unable to buffer request for next node");
            ++m_numForwardStalls;
            return false;
        }
        break;

    case Message::REQUEST_DATA_TOKEN:
    {
        // We received a line for a previous read miss on this cache
        assert(line != NULL);
        assert(line->state == LINE_LOADING);
        assert(msg->tokens > 0);
        assert(line->tokens == 0);

        TraceWrite(msg->address, "Received Read Response with %u tokens", msg->tokens);
        
        COMMIT
        {
            // Some byte may have been overwritten by processor.
            // Update the message. This will ensure the response
            // gets the latest value, and other processors too
            // (which is fine, according to non-determinism).
            line::blit(msg->data.data, line->data, line->valid, m_lineSize);

            // Store the data, masked by the already-valid bitmask
            line::blitnot(line->data, msg->data.data, line->valid, m_lineSize);
            line::setifnot(line->valid, true, line->valid, m_lineSize);

            line->state  = LINE_FULL;
            line->tokens = msg->tokens;
            line->dirty  = msg->dirty || line->dirty;
        }
        
        /*
         Put the data on the bus for the processors.
         Merge with pending writes first so we don't accidentally give some
         other processor on the bus old data after its write.
         This is kind of a hack; it's feasibility in hardware in a single cycle
         is questionable.
        */
        char data[m_lineSize];

        COMMIT
        {
            std::copy(msg->data.data, msg->data.data + m_lineSize, data);

            for (Buffer<Request>::const_iterator p = m_requests.begin(); p != m_requests.end(); ++p)
            {
                if (p->write && p->address == msg->address)
                {
                    // This is a write to the same line, merge it
                    line::blit(data, p->data, p->mask, m_lineSize);
                }
            }
        }

        if (!OnReadCompleted(msg->address, data))
        {
            DeadlockWrite("Unable to notify clients of read completion");
            ++m_numStallingRCompletions;
            return false;
        }

        // Statistics
        COMMIT{ ++m_numRCompletions; }
        
        COMMIT{ delete msg; }
        break;
    }
    
    case Message::EVICTION:
        if (line != NULL)
        {
            if (line->state == LINE_FULL)
            {
                // We have the line, merge it.
                TraceWrite(msg->address, "Merging Evict Request with %u tokens into line with %u tokens", msg->tokens, line->tokens);
            
                // Just add the tokens count to the line.
                assert(msg->tokens > 0);
                COMMIT
                {
                    line->tokens += msg->tokens;
                
                    // Combine the dirty flags
                    line->dirty = line->dirty || msg->dirty;
                
                    delete msg;

                    // Statistics
                    ++m_numMergedEvictions;
                }
                break;
            }
        }
        // We don't have the line, see if we have an empty spot
        else
        {
            MemAddr tag;
            if ((line = AllocateLine(msg->address, true, &tag)) != NULL)
            {
                // Yes, place the line there
                TraceWrite(msg->address, "Storing Evict Request for line %#016llx locally with %u tokens", (unsigned long long)msg->address, msg->tokens);
                
                COMMIT
                {
                    line->state    = LINE_FULL;
                    line->tag      = tag;
                    line->tokens   = msg->tokens;
                    line->dirty    = msg->dirty;
                    line->updating = 0;
                    line->access   = GetKernel()->GetCycleNo();
                    std::fill(line->valid, line->valid + m_lineSize, true);
                    std::copy(msg->data.data, msg->data.data + m_lineSize, line->data);

                    delete msg; 

                    // Statistics
                    ++m_numInjectedEvictions;
                }
                break;
            }
        }

        // Just forward it
        if (!SendMessage(msg, MINSPACE_FORWARD))
        {
            DeadlockWrite("Unable to buffer forwarded eviction request for next node");
            ++m_numForwardStalls;
            return false;
        }

        COMMIT{ ++m_numIgnoredMessages; }
        break;

    case Message::UPDATE:
        if (msg->sender == m_id)
        {
            // The update has come full circle.
            // Notify the sender of write consistency.
            assert(line != NULL);
            assert(line->updating > 0);
            
            if (!m_clients[msg->client]->OnMemoryWriteCompleted(msg->wid))
            {
                ++m_numStallingWCompletions;
                return false;
            }

            COMMIT
            {
                line->updating--;
                delete msg;

                // Statistics
                ++m_numWCompletions;
            }
        }
        else
        {
            // Update the line, if we have it, and forward the message
            if (line != NULL)
            {
                COMMIT
                {
                    line::blit(line->data, msg->data.data, msg->data.mask, m_lineSize);
                    line::setif(line->valid, true, msg->data.mask, m_lineSize);

                    // Statistics
                    ++m_numNetworkWHits;
                }
            
                // Send the write as a snoop to the processors
                for (size_t i = 0; i < m_clients.size(); ++i)
                {
                    if (m_clients[i] != NULL)
                    {
                        if (!m_clients[i]->OnMemorySnooped(msg->address, msg->data.data, msg->data.mask))
                        {
                            DeadlockWrite("Unable to snoop update to cache clients");
                            ++m_numStallingWSnoops;
                            return false;
                        }
                    }
                }
            }
            else
                COMMIT{ ++m_numIgnoredMessages; }
            
            if (!SendMessage(msg, MINSPACE_FORWARD))
            {
                DeadlockWrite("Unable to buffer forwarded update request for next node");
                ++m_numForwardStalls;
                return false;
            }
        }
        break;
        
    default:
        assert(false);
        break;    
    }
    return true;
}
    
bool COMA::Cache::OnReadCompleted(MemAddr addr, const char * data)
{
    // Send the completion on the bus
    if (!p_bus.Invoke())
    {
        DeadlockWrite("Unable to acquire the bus for sending read completion");
        return false;
    }
    
    for (std::vector<IMemoryCallback*>::const_iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (*p != NULL && !(*p)->OnMemoryReadCompleted(addr, data))
        {
            DeadlockWrite("Unable to send read completion to clients");
            return false;
        }
    }
    
    return true;
}

// Handles a write request from below
// FAILED  - stall
// DELAYED - repeat next cycle
// SUCCESS - advance
Result COMA::Cache::OnWriteRequest(const Request& req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Lines busy, cannot process bus write request");
        return FAILED;
    }
    
    MemAddr tag;
    
    Line* line = FindLine(req.address);
    if (line == NULL)
    {
        // Write miss; write-allocate
        line = AllocateLine(req.address, false, &tag);
        if (line == NULL)
        {
            ++m_numHardWConflicts;
            DeadlockWrite("Unable to allocate line for bus write request");
            return FAILED;
        }

        if (line->state != LINE_EMPTY)
        {
            // We're overwriting another line, evict the old line
            TraceWrite(req.address, "Processing Bus Write Request: Miss; Evicting line with tag %#016llx",
                       (unsigned long long)line->tag);

            if (!EvictLine(line, req))
            {
                ++m_numStallingWEvictions;
                DeadlockWrite("Unable to evict line for bus write request");
                return FAILED;
            }

            COMMIT { ++m_numWEvictions; }
            return DELAYED;
        }

        TraceWrite(req.address, "Processing Bus Write Request: Miss; Sending Read Request");
        
        // Reset the line
        COMMIT
        {
            line->state    = LINE_LOADING;
            line->tag      = tag;
            line->tokens   = 0;
            line->dirty    = false;
            line->updating = 0;
            std::fill(line->valid, line->valid + m_lineSize, false);
        }
        
        // Send a request out for the cache-line
        Message* msg = NULL;
        COMMIT
        {
            msg = new Message;
            msg->type      = Message::REQUEST;
            msg->address   = req.address;
            msg->ignore    = false;
            msg->tokens    = 0;
            msg->sender    = m_id;
            
        }
            
        if (!SendMessage(msg, MINSPACE_INSERTION))
        {
            ++m_numStallingWLoads;
            DeadlockWrite("Unable to buffer read request for next node");
            return FAILED;
        }

        // Statistics
        COMMIT { ++m_numWLoads; }
        
        // Now try against next cycle
        return DELAYED;
    }

    // Write hit
    // Although we may hit a loading line
    if (line->state == LINE_FULL && line->tokens == m_parent.GetTotalTokens())
    {
        // We have all tokens, notify the sender client immediately
        TraceWrite(req.address, "Processing Bus Write Request: Exclusive Hit");
        
        if (!m_clients[req.client]->OnMemoryWriteCompleted(req.wid))
        {
            ++m_numStallingWHits;
            DeadlockWrite("Unable to process bus write completion for client %u", (unsigned)req.client);
            return FAILED;
        }

        // Statistics
        COMMIT{ ++m_numWEHits; }
    }
    else
    {
        // There are other copies out there, send out an update message
        TraceWrite(req.address, "Processing Bus Write Request: Shared Hit; Sending Update");
        
        Message* msg = NULL;
        COMMIT
        {
            msg = new Message;
            msg->address   = req.address;
            msg->type      = Message::UPDATE;
            msg->sender    = m_id;
            msg->ignore    = false;
            msg->client    = req.client;
            msg->wid       = req.wid;
            std::copy(req.data, req.data + m_lineSize, msg->data.data);
            std::copy(req.mask, req.mask + m_lineSize, msg->data.mask);

            // Lock the line to prevent eviction
            line->updating++;
        }
            
        if (!SendMessage(msg, MINSPACE_INSERTION))
        {
            ++m_numStallingWUpdates;
            DeadlockWrite("Unable to buffer update request for next node");
            return FAILED;
        }

        // Statistics
        COMMIT {
            if(line->state == LINE_LOADING)
                ++m_numLoadingWUpdates;
            else
                ++m_numSharedWUpdates;
        }
    }
    
    // Either way, at this point we have a line, so we
    // write the data into it.
    COMMIT
    {
        line::blit(line->data, req.data, req.mask, m_lineSize);
        line::setif(line->valid, true, req.mask, m_lineSize);
        
        // The line is now dirty
        line->dirty = true;
        
        // Also update last access time.
        line->access = GetKernel()->GetCycleNo();                    
    }
    return SUCCESS;
}

// Handles a read request from below
// FAILED  - stall
// DELAYED - repeat next cycle
// SUCCESS - advance
Result COMA::Cache::OnReadRequest(const Request& req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Lines busy, cannot process bus read request");
        return FAILED;
    }

    Line* line = FindLine(req.address);
    MemAddr tag;

    if (line == NULL)
    {
        // Read miss, allocate a line
        line = AllocateLine(req.address, false, &tag);
        if (line == NULL)
        {
            ++m_numHardRConflicts;
            DeadlockWrite("Unable to allocate line for bus read request");
            return FAILED;
        }

        if (line->state != LINE_EMPTY)
        {
            // We're overwriting another line, evict the old line
            TraceWrite(req.address, "Processing Bus Read Request: Miss; Evicting line with tag %#016llx",
                       (unsigned long long)line->tag);
            
            if (!EvictLine(line, req))
            {
                ++m_numStallingREvictions;
                DeadlockWrite("Unable to evict line for bus read request");
                return FAILED;
            }

            COMMIT { ++m_numREvictions; }
            return DELAYED;
        }

        TraceWrite(req.address, "Processing Bus Read Request: Miss; Sending Read Request");

        // Reset the line
        COMMIT
        {
            line->state    = LINE_LOADING;
            line->tag      = tag;
            line->tokens   = 0;
            line->dirty    = false;
            line->updating = 0;
            line->access   = GetKernel()->GetCycleNo();
            std::fill(line->valid, line->valid + m_lineSize, false);
        }
        
        // Send a request out
        Message* msg = NULL;
        COMMIT
        {
            msg = new Message;
            msg->type      = Message::REQUEST;
            msg->address   = req.address;
            msg->ignore    = false;
            msg->tokens    = 0;
            msg->sender    = m_id;
        }
            
        if (!SendMessage(msg, MINSPACE_INSERTION))
        {
            ++m_numStallingRLoads;
            DeadlockWrite("Unable to buffer read request for next node");
            return FAILED;
        }
        
        // Statistics
        COMMIT { ++m_numRLoads; }

    }
    // Read hit
    else if (line->state == LINE_FULL)
    {
        // Line is present and full
        TraceWrite(req.address, "Processing Bus Read Request: Full Hit");

        // Return the data
        char data[m_lineSize];

        COMMIT
        {
            std::copy(line->data, line->data + m_lineSize, data);

            // Update LRU information
            line->access = GetKernel()->GetCycleNo();
            
            ++m_numRFullHits;
        }

        if (!OnReadCompleted(req.address, data))
        {
            ++m_numStallingRHits;
            DeadlockWrite("Unable to notify clients of read completion");
            return FAILED;
        }
    }
    else
    {
        // The line is already being loaded.
        TraceWrite(req.address, "Processing Bus Read Request: Loading Hit");

        // We can ignore this request; the completion of the earlier load
        // will put the data on the bus so this requester will also get it.
        assert(line->state == LINE_LOADING);
        
        // Counts as a miss because we have to wait
        COMMIT{ ++m_numLoadingRMisses; }
    }
    return SUCCESS;
}

Result COMA::Cache::DoRequests()
{
    // Handle incoming requests from below
    assert(!m_requests.Empty());
    const Request& req = m_requests.Front();
    Result result = (req.write) ? OnWriteRequest(req) : OnReadRequest(req);
    if (result == SUCCESS)
    {
        m_requests.Pop();

        // Statistics
        COMMIT {
            if (req.write)
                ++m_numWAccesses;
            else
                ++m_numRAccesses;
        }
    }
    return (result == FAILED) ? FAILED : SUCCESS;
}
    
Result COMA::Cache::DoReceive()
{
    // Handle received message from prev
    assert(!m_incoming.Empty());

    COMMIT{ ++m_numReceivedMessages; }

    if (!OnMessageReceived(m_incoming.Front()))
    {
        return FAILED;
    }
    m_incoming.Pop();
    return SUCCESS;
}

COMA::Cache::Cache(const std::string& name, COMA& parent, Clock& clock, CacheID id, Config& config) :
    Simulator::Object(name, parent),
    Node(name, parent, clock, config),
    m_selector (parent.GetBankSelector()),
    m_lineSize (config.getValue<size_t>("CacheLineSize")),
    m_assoc    (config.getValue<size_t>(parent, "L2CacheAssociativity")),
    m_sets     (m_selector.GetNumBanks()),
    m_id       (id),
    p_lines    (*this, clock, "p_lines"),

    m_numRAccesses(0),
    m_numHardRConflicts(0),
    m_numStallingREvictions(0),
    m_numREvictions(0),
    m_numStallingRLoads(0),
    m_numRLoads(0),
    m_numRFullHits(0),
    m_numStallingRHits (0),
    m_numLoadingRMisses(0),
    m_numWAccesses(0),
    m_numHardWConflicts(0),
    m_numStallingWEvictions(0),
    m_numWEvictions(0),
    m_numStallingWLoads(0),
    m_numWLoads(0),
    m_numStallingWHits (0),
    m_numWEHits(0),
    m_numLoadingWUpdates(0),
    m_numSharedWUpdates(0),
    m_numStallingWUpdates(0),
    m_numReceivedMessages(0),
    m_numIgnoredMessages(0),
    m_numForwardStalls(0),
    m_numNetworkRHits(0),
    m_numRCompletions (0),
    m_numStallingRCompletions(0),
    m_numInjectedEvictions(0),
    m_numMergedEvictions(0),
    m_numStallingWCompletions(0),
    m_numWCompletions(0),
    m_numNetworkWHits(0),
    m_numStallingWSnoops(0),

    p_Requests (*this, "requests", delegate::create<Cache, &Cache::DoRequests>(*this)),
    p_In       (*this, "incoming", delegate::create<Cache, &Cache::DoReceive>(*this)),
    p_bus      (*this, clock, "p_bus"),
    m_requests ("b_requests", *this, clock, config.getValue<BufferSize>(*this, "RequestBufferSize")),
    m_responses("b_responses", *this, clock, config.getValue<BufferSize>(*this, "ResponseBufferSize"))
{
    
    RegisterSampleVariableInObject(m_numRAccesses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numHardRConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingREvictions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numREvictions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingRLoads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numRLoads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numRFullHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingRHits , SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numLoadingRMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numWAccesses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numHardWConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWEvictions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numWEvictions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWLoads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numWLoads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWHits , SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numWEHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numLoadingWUpdates, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numSharedWUpdates, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWUpdates, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numReceivedMessages, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numIgnoredMessages, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numForwardStalls, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numNetworkRHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numRCompletions , SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingRCompletions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numInjectedEvictions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numMergedEvictions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWCompletions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numWCompletions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numNetworkWHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWSnoops, SVC_CUMULATIVE);

    // Create the cache lines
    m_lines.resize(m_assoc * m_sets);
    m_data.resize(m_lines.size() * m_lineSize);
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        Line& line = m_lines[i];
        line.state = LINE_EMPTY;
        line.data  = &m_data[i * m_lineSize];
    }

    m_requests.Sensitive(p_Requests);   
    m_incoming.Sensitive(p_In);
    
    p_lines.AddProcess(p_In);
    p_lines.AddProcess(p_Requests);

    p_bus.AddPriorityProcess(p_In);                   // Update triggers write completion
    p_bus.AddPriorityProcess(p_Requests);             // Read or write hit

    config.registerObject(*this, "cache");
    config.registerProperty(*this, "assoc", (uint32_t)m_assoc);
    config.registerProperty(*this, "sets", (uint32_t)m_sets);
    config.registerProperty(*this, "lsz", (uint32_t)m_lineSize);
    config.registerProperty(*this, "freq", (uint32_t)clock.GetFrequency());
}

void COMA::Cache::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*args*/) const
{
    out <<
    "The L2 Cache in a COMA system is connected to the processors with a bus and to\n"
    "the rest of the COMA system via a ring network.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Print global information such as hit-rate\n"
    "  and cache configuration.\n"
    "- inspect <component> lines [fmt [width [address]]]\n"
    "  Read the cache lines themselves.\n"
    "  * fmt can be b/w/c and indicates formatting by bytes, words, or characters.\n"
    "  * width indicates how many bytes are printed on each line (default: entire line).\n"
    "  * address if specified filters the output to the specified cache line.\n" 
    "- inspect <component> buffers\n"
    "  Reads and displays the buffers in the cache\n";
}

void COMA::Cache::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "buffers")
    {
        // Read the buffers
        out << "Bus requests:" << endl << endl
            << "      Address      | Type  |" << endl
            << "-------------------+-------+" << endl;
        for (Buffer<Request>::const_iterator p = m_requests.begin(); p != m_requests.end(); ++p)
        {
            out << hex << "0x" << setw(16) << setfill('0') << p->address << " | "
                << (p->write ? "Write" : "Read ") << " | "
                << endl;
        }
        
        out << endl << "Ring interface:" << endl << endl;
        Print(out);        
        return;
    }
    else if (arguments.empty())
    {
        out << "Cache type:       ";
        if (m_assoc == 1) {
            out << "Direct mapped" << endl;
        } else if (m_assoc == m_lines.size()) {
            out << "Fully associative" << endl;
        } else {
            out << dec << m_assoc << "-way set associative" << endl;
        }
        
        out << "L2 bank mapping:  " << m_selector.GetName() << endl
            << "Cache size:       " << dec << (m_lineSize * m_lines.size()) << " bytes" << endl
            << "Cache line size:  " << dec << m_lineSize << " bytes" << endl
            << endl;
        

        uint64_t numRHits     = m_numRFullHits;
        uint64_t numRMisses   = m_numRAccesses - numRHits;

        uint64_t numRMessages = m_numREvictions + m_numRLoads;

        uint64_t numWHits     = m_numWEHits;
        uint64_t numWMisses   = m_numWAccesses - numWHits;

        uint64_t numWMessages = m_numWEvictions + m_numWLoads + m_numLoadingWUpdates + m_numSharedWUpdates;

        uint64_t numMessages  = numWMessages + numRMessages;

        uint64_t numRStalls_above   = m_numStallingREvictions + m_numStallingRLoads + m_numStallingRHits;
        uint64_t numWStalls_above   = m_numStallingWEvictions + m_numStallingWLoads + m_numStallingWUpdates;
        uint64_t numStalls_above    = numRStalls_above + numWStalls_above;

        uint64_t numRStalls_below   = m_numStallingRCompletions;
        uint64_t numWStalls_below   = m_numStallingWCompletions + m_numStallingWSnoops;
        uint64_t numStalls_below    = numRStalls_below + numWStalls_below + m_numForwardStalls;

        uint64_t numConsumedMessages  = m_numRCompletions + m_numWCompletions + m_numMergedEvictions + m_numInjectedEvictions;
        uint64_t numProcessedMessages = m_numNetworkRHits + m_numNetworkWHits;
        uint64_t numUsedMessages      = m_numReceivedMessages - m_numIgnoredMessages;
        
#define PRINTVAL(X, q) dec << (X) << " (" << setprecision(2) << fixed << (X) * q << "%)"
        
        if (m_numRAccesses == 0 && m_numWAccesses == 0 && numUsedMessages == 0)
            out << "No accesses so far, cannot provide statistical data." << endl;
        else
        {
            out << "***********************************************************" << endl
                << "                      Summary                              " << endl
                << "***********************************************************" << endl
                << endl
                << "Number of read requests from downstream:   " << m_numRAccesses << endl
                << "Number of write requests from downstream:  " << m_numWAccesses << endl
                << "Number of messages issued to upstream:     " << numMessages  << endl
                << "Number of messages received from upstream: " << m_numReceivedMessages << endl
                << "Stall cycles while processing requests:    " << numStalls_above << endl
                << "Stall cycles while processing messages:    " << numStalls_below << endl 
                << endl;
                
            float r_factor = 100.0f / m_numRAccesses;
            out << "***********************************************************" << endl
                << "              Cache reads from downstream                  " << endl
                << "***********************************************************" << endl 
                << endl
                << "Number of reads from downstream:         " << m_numRAccesses << endl
                << "Read hits:                               " << PRINTVAL(numRHits, r_factor) << endl
                << "Read misses:                             " << PRINTVAL(numRMisses, r_factor) << endl
                << "Breakdown of reads:" << endl             
                << "- reads causing an eviction:             " << PRINTVAL(m_numREvictions, r_factor) << endl
                << "- reads causing a read message upstream: " << PRINTVAL(m_numRLoads, r_factor) << endl
                << "- reads delayed on line already loading: " << PRINTVAL(m_numLoadingRMisses, r_factor) << endl
                << "(percentages relative to " << m_numRAccesses << " read requests)" << endl
                << endl;
                
            float w_factor = 100.0f / m_numWAccesses;
            out << "***********************************************************" << endl
                << "              Cache writes from downstream                 " << endl
                << "***********************************************************" << endl
                << endl
                << "Number of writes from downstream:             " << m_numWAccesses << endl
                << "Write hits:                                   " << PRINTVAL(numWHits, w_factor) << endl 
                << "Write misses:                                 " << PRINTVAL(numWMisses, w_factor) << endl
                << "Breakdown of writes:" << endl                 
                << "- writes causing an eviction:                 " << PRINTVAL(m_numWEvictions, w_factor) << endl
                << "- writes causing a read message upstream:     " << PRINTVAL(m_numWLoads, w_factor) << endl
                << "- writes to a shared line causing an update:  " << PRINTVAL(m_numSharedWUpdates, w_factor) << endl
                << "- writes to a loading line causing an update: " << PRINTVAL(m_numLoadingWUpdates, w_factor) << endl
                << "(percentages relative to " << m_numWAccesses << " write requests)" << endl
                << endl;

            float m_factor = 100.f / numMessages;                
            out << "***********************************************************" << endl
                << "                   Messages to upstream                    " << endl
                << "***********************************************************" << endl
                << endl
                << "Number of messages issued to upstream:        " << numMessages  << endl
                << "Read-related messages:                        " << PRINTVAL(numRMessages, m_factor) << endl
                << "Write-related messages:                       " << PRINTVAL(numWMessages, m_factor) << endl
                << "Breakdown of read-related messages:" << endl
                << "- line evictions:                             " << PRINTVAL(m_numREvictions, m_factor) << endl
                << "- read requests to empty or evicted line:     " << PRINTVAL(m_numRLoads, m_factor) << endl
                << "Breakdown of write-related messages:" << endl
                << "- line evictions:                             " << PRINTVAL(m_numWEvictions, m_factor) << endl
                << "- cache line requests before write:           " << PRINTVAL(m_numWLoads, m_factor) << endl
                << "- update messages upon write to loading line: " << PRINTVAL(m_numLoadingWUpdates, m_factor) << endl
                << "- update messages upon write to shared line:  " << PRINTVAL(m_numSharedWUpdates, m_factor) << endl
                << "(percentages relative to " << numMessages << " messages to upstream)" << endl
                << endl;

            float m2_factor = 100.f / m_numReceivedMessages;                
            out << "***********************************************************" << endl
                << "                 Messages from upstream                    " << endl
                << "***********************************************************" << endl
                << endl
                << "Number of messages received from upstream: " << m_numReceivedMessages  << endl
                << "Number of consumed messages:               " << PRINTVAL(numConsumedMessages, m2_factor) << endl
                << "Number of processed messages:              " << PRINTVAL(numProcessedMessages, m2_factor) << endl
                << "Number of ignored messages:                " << PRINTVAL(m_numIgnoredMessages, m2_factor) << endl
                << "Breakdown of consumed messages:" << endl
                << "- read completions for downstream:         " << PRINTVAL(m_numRCompletions, m2_factor) << endl
                << "- write completions for downstream:        " << PRINTVAL(m_numWCompletions, m2_factor) << endl
                << "- merged evictions:                        " << PRINTVAL(m_numMergedEvictions, m2_factor) << endl
                << "- injected evictions:                      " << PRINTVAL(m_numInjectedEvictions, m2_factor) << endl
                << "Breakdown of processed messages:" << endl
                << "- read hits from other caches:             " << PRINTVAL(m_numNetworkRHits, m2_factor) << endl
                << "- merged updates:                          " << PRINTVAL(m_numNetworkWHits, m2_factor) << endl
                << "(percentages relative to " << m_numReceivedMessages << " messages from upstream)" << endl
                << endl;
                
            if (numStalls_above != 0)
            {
                float s_factor = 100.f / numStalls_above;   
                out << "***********************************************************" << endl
                    << "       Stalls while processing requests from below.        " << endl
                    << "***********************************************************" << endl
                    << endl
                    << "Stall cycles while processing requests from below: " << numStalls_above << endl
                    << "Stalls while processing read requests:             " << PRINTVAL(numRStalls_above, s_factor) << endl
                    << "Stalls while processing write requests:            " << PRINTVAL(numWStalls_above, s_factor) << endl
                    << "Breakdown of read stalls:" << endl
                    << "- stalled evictions during reads:                  " << PRINTVAL(m_numStallingREvictions, s_factor) << endl
                    << "- stalled loads during reads:                      " << PRINTVAL(m_numStallingRLoads, s_factor) << endl
                    << "- stalled read hit notification to downstream:     " << PRINTVAL(m_numStallingRHits, s_factor) << endl
                    << "Breakdown of write stalls:" << endl
                    << "- stalled evictions during writes:                 " << PRINTVAL(m_numStallingWEvictions, s_factor) << endl
                    << "- stalled loads during writes:                     " << PRINTVAL(m_numStallingWLoads, s_factor) << endl
                    << "- stalled updates during writes:                   " << PRINTVAL(m_numStallingWUpdates, s_factor) << endl
                    << "(stall percentages relative to " << numStalls_above << " cycles)" << endl
                    << endl; 
            }

            if (numStalls_below != 0)
            {
                float s_factor = 100.f / numStalls_below;
                out << "***********************************************************" << endl
                    << "       Stalls while processing requests from above.        " << endl
                    << "***********************************************************" << endl
                    << endl
                    << "Stall cycles while processing requests from above:     " << numStalls_below << endl
                    << "Stalls while processing reads:                         " << PRINTVAL(numRStalls_below, s_factor) << endl
                    << "Stalls while processing writes/updates:                " << PRINTVAL(numWStalls_below, s_factor) << endl
                    << "Stalls while forwarding messages:                      " << PRINTVAL(m_numForwardStalls, s_factor) << endl
                    << "Breakdown of read stalls:" << endl
                    << "- stalled read completion notification to downstream:  " << PRINTVAL(m_numStallingRCompletions, s_factor) << endl
                    << "Breakdown of write/update stalls:" << endl
                    << "- stalled write completion notification to downstream: " << PRINTVAL(m_numStallingWCompletions, s_factor) << endl
                    << "- stalled write snoops to downstream:                  " << PRINTVAL(m_numStallingWSnoops, s_factor) << endl
                    << "(stall percentages relative to " << numStalls_below << " cycles)" << endl
                    << endl; 
            }
                
                       
        }

       

        
        
       


    }
    else if (arguments[0] == "lines")
    {
        enum { fmt_bytes, fmt_words, fmt_chars } fmt = fmt_words;
        size_t bytes_per_line = m_lineSize;
        bool specific = false;
        MemAddr seladdr = 0;
        if (arguments.size() > 1)
        {
            if (arguments[1] == "b") fmt = fmt_bytes;
            else if (arguments[1] == "w") fmt = fmt_words;
            else if (arguments[1] == "c") fmt = fmt_chars;
            else
            {
                out << "Invalid format: " << arguments[1] << ", expected b/w/c" << endl;
                return;
            }
        }
        if (arguments.size() > 2)
        {
            bytes_per_line = strtoumax(arguments[2].c_str(), 0, 0);
        }
        if (arguments.size() > 3)
        {
            errno = 0;
            seladdr = strtoumax(arguments[3].c_str(), 0, 0); 
            if (errno != EINVAL)
                specific = true;
            seladdr = (seladdr / m_lineSize) * m_lineSize;
        }
        
        out << "Set |       Address      | LDU   | Tokens |                       Data" << endl;
        out << "----+--------------------+-------+--------+--------------------------------------------------" << endl;
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            const size_t set = i / m_assoc;
            const Line& line = m_lines[i];
            MemAddr lineaddr = m_selector.Unmap(line.tag, set) * m_lineSize;
            if (specific && lineaddr != seladdr)
                continue;
            
            out << setw(3) << setfill(' ') << dec << right << set;
            
            if (line.state == LINE_EMPTY) {
                out << " |                    |       |        |";
            } else {
                out << " | "
                    << hex << "0x" << setw(16) << setfill('0') << lineaddr
                    << " | "
                    << ((line.state == LINE_LOADING) ? "L" : " ")
                    << (line.dirty ? "D" : " ")
                    << setw(3) << setfill(' ') << dec << line.updating
                    << " | "
                    << setfill(' ') << setw(6) << line.tokens << " |";
                
                // Print the data
                out << hex << setfill('0');
                for (size_t y = 0; y < m_lineSize; y += bytes_per_line)
                {
                    for (size_t x = y; x < y + bytes_per_line; ++x) {
                        if ((fmt == fmt_bytes) || ((fmt == fmt_words) && (x % sizeof(Integer) == 0))) 
                            out << " ";
                        
                        if (line.valid[x]) {
                            char byte = line.data[x];
                            if (fmt == fmt_chars)
                                out << (isprint(byte) ? byte : '.');
                            else
                                out << setw(2) << (unsigned)(unsigned char)byte;
                        } else {
                            out << ((fmt == fmt_chars) ? " " : "  ");
                        }
                    }
                    
                    if (y + bytes_per_line < m_lineSize) {
                        // This was not yet the last line
                        out << endl << "    |                    |       |        |";
                    }
                }
            }
            out << endl;
        }
    }
}
    
}
