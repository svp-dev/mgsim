#include "Cache.h"
#include "../config.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <iomanip>
using namespace std;

namespace Simulator
{

void COMA::Cache::RegisterClient(PSize pid, IMemoryCallback& callback, const Process* processes[])
{
    size_t index = pid % m_clients.size();
    assert(m_clients[index] == NULL);
    m_clients[index] = &callback;
    
    for (size_t i = 0; processes[i] != NULL; ++i)
    {
        p_bus.AddProcess(*processes[i]);
    }
}

void COMA::Cache::UnregisterClient(PSize pid)
{
    size_t index = pid % m_clients.size();
    assert(m_clients[index] != NULL);
    m_clients[index] = NULL;
}

// Called from the processor on a memory read (typically a whole cache-line)
// Just queues the request.
bool COMA::Cache::Read(PSize pid, MemAddr address, MemSize size)
{
    if (size != m_lineSize)
    {
        throw InvalidArgumentException("Read size is not a single cache-line");
    }

    assert(size <= MAX_MEMORY_OPERATION_SIZE);
    assert(address % m_lineSize == 0);
    assert(size == m_lineSize);
    
    if (address % m_lineSize != 0)
    {
        throw InvalidArgumentException("Read address is not aligned to a cache-line");
    }
    
    // This method can get called by several 'listeners', so we need
    // to arbitrate and store the request in a buffer to handle it.
    if (!p_bus.Invoke())
    {
        // Arbitration failed
        DeadlockWrite("Unable to acquire bus for read");
        return FAILED;
    }
    
    Request req;
    req.address = address;
    req.write   = false;
    req.size    = size;
    
    // Client should have been registered
    assert(m_clients[pid % m_clients.size()] != NULL);

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
bool COMA::Cache::Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid)
{
    if (size > m_lineSize || size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Write size is too big");
    }

    if (address / m_lineSize != (address + size - 1) / m_lineSize)
    {
        throw InvalidArgumentException("Write request straddles cache-line boundary");
    }

    // This method can get called by several 'listeners', so we need
    // to arbitrate and store the request in a buffer to handle it.
    if (!p_bus.Invoke())
    {
        // Arbitration failed
        DeadlockWrite("Unable to acquire bus for write");
        return false;
    }
    
    Request req;
    req.address = address;
    req.write   = true;
    req.size    = size;
    req.client  = pid % m_clients.size();
    req.tid     = tid;
    memcpy(req.data, data, (size_t)size);
    
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
            if (!client->OnMemorySnooped(req.address, req))
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
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;    

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
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;    

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
COMA::Cache::Line* COMA::Cache::AllocateLine(MemAddr address, bool empty_only)
{
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;    

    // Find the line
    Line* empty   = NULL;
    Line* replace = NULL;
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line& line = m_lines[set + i];
        if (line.state == LINE_EMPTY)
        {
            // Empty, unused line, remember this one
            empty = &line;
        }
        else if (!empty_only)
        {
            // We're also considering non-empty lines; use LRU
            assert(line.tag != tag);
            if (line.state != LINE_LOADING && line.updating == 0 && (replace == NULL || line.access < replace->access))
            {
                // The line is available to be replaced and has a lower LRU rating,
                // remember it for replacing
                replace = &line;
            }
        }
    }
    
    // The line could not be found, allocate the empty line or replace an existing line
    return (empty != NULL) ? empty : replace;
}

bool COMA::Cache::EvictLine(Line* line, const Request& req)
{
    // We never evict loading or updating lines
    assert(line->state != LINE_LOADING);
    assert(line->updating == 0);
        
    size_t set = (line - &m_lines[0]) / m_assoc;
    MemAddr address = (line->tag * m_sets + set) * m_lineSize;
    
    TraceWrite(address, "Evicting with %d tokens due to read miss for 0x%llx", line->tokens, (unsigned long long)req.address);
    
    Message* msg = NULL;
    COMMIT
    {
        msg = new Message;
        msg->type      = Message::REQUEST_EVICT;
        msg->address   = address;
        msg->hops      = 0;
        msg->tokens    = line->tokens;
        msg->data.size = m_lineSize;
        msg->dirty     = line->dirty;
        memcpy(msg->data.data, line->data, m_lineSize);
    }
    
    if (!m_next.Send(msg))
    {
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
bool COMA::Cache::OnRequestReceived(Message* msg)
{
    assert(msg != NULL);
    
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to acquire lines");
        return false;
    }
    
    Line* line = FindLine(msg->address);   
    switch (msg->type)
    {
    case Message::REQUEST_READ:
        // Some cache had a read miss. See if we have the line.
        assert(msg->data.size == m_lineSize);
        
        if (line == NULL)
        {
            // We don't have this line, forward the message.
            // Increase the caches count in the message when a cache cannot service a request.
            COMMIT
            {
                msg->hops++;
            }
            
            if (!m_next.Send(msg))
            {
                DeadlockWrite("Unable to buffer request for next node");
                return false;
            }
        }
        // We have this line
        else if (line->state == LINE_FULL)
        {
            // We have the data
            assert(line->tokens > 0);
            const char* trace = NULL;
            if ((unsigned int)line->tokens > 1 + msg->hops)
            {
                // We have the data and free tokens, answer the request.
                // We reserve one token for ourselves and every cache miss
                // on the way back and send the rest.            
                TraceWrite(msg->address, "Received Read Request: Full Hit; Enough tokens available; Sending Read Response");
                
                COMMIT
                {
                    msg->type   = Message::RESPONSE_READ;
                    msg->tokens = line->tokens - (1 + msg->hops);
                    memcpy(msg->data.data, line->data, msg->data.size);

                    // Set our token count. One for ourselves, and one for each missed cache upstream.
                    line->tokens = 1 + msg->hops;
                
                    // Also update last access time.
                    line->access = GetKernel()->GetCycleNo();                    
                }
            }
            else
            {
                // We have the data, but not enough tokens. This means another cache
                // sits between us and the requester, so send a forward response to
                // that cache.
                TraceWrite(msg->address, "Received Read Request: Full Hit; Not enough tokens available; Sending Forward Response");
                
                COMMIT
                {
                    msg->type   = Message::RESPONSE_FORWARD;
                    msg->tokens = line->tokens - msg->hops;
                    msg->hops   = line->tokens - 1;
                }
            }
            
            if (!m_prev.Send(msg))
            {
                DeadlockWrite("Unable to buffer request for previous node");
                return false;
            }
        }
        else
        {
            // We're loading the data; don't forward the request.
            // When the get the data, we'll forward it.
            // Also remember how many tokens to reserve for ourselves.
            assert(line->state == LINE_LOADING);
            const char* trace = NULL;
            if (!line->forward)
            {
                // Set the forward flag and remember how many caches
                // are between us and the next cache that wants it.
                TraceWrite(msg->address, "Received Read Request: Loading Hit; Setting Forward flag");
                
                COMMIT
                {
                    line->forward = true;
                    line->hops    = msg->hops;
                    delete msg;                    
                }
            }
            else
            {
                // Forward flag was already set. Update the cache count
                // and send a response back indicating that the data
                // should be forwarded.
                TraceWrite(msg->address, "Received Read Request: Loading Hit; Forward; Sending Forward Response");
                
                COMMIT
                {
                    msg->type   = Message::RESPONSE_FORWARD;
                    msg->tokens = line->hops - (1 + msg->hops);
                    
                    line->hops = msg->hops;                    
                }
                
                if (!m_prev.Send(msg))
                {
                    DeadlockWrite("Unable to buffer response for previous node");
                    return false;
                }
            }
        }
        break;

    case Message::REQUEST_KILL_TOKENS:
        // Just add the token count to the line.
        // If FULL, then the line will just have less tokens.
        // If LOADING, then the completion will use the token field to adjust
        // the received tokens (before possibly forwarding).
        assert(msg->tokens > 0);
        if (line == NULL)
        {
            // We don't have this line, forward the message.
            // Increase the caches count in the message when a cache cannot service a request.
            COMMIT{ msg->hops++; }
            
            if (!m_next.Send(msg))
            {
                DeadlockWrite("Unable to buffer request for next node");
                return false;
            }
        }
        else
        {
            TraceWrite(msg->address, "Received Kill Request for %u tokens", msg->tokens);
            COMMIT
            {
                line->tokens -= msg->tokens;
                delete msg;
            }
        }
        break;
        
    case Message::REQUEST_EVICT:
        if (line != NULL)
        {
            // We have the line, merge it.
            TraceWrite(msg->address, "Merging Evict Request with %u tokens", msg->tokens);
            
            // Just add the token count to the line.
            // If FULL, then the line will just have more tokens.
            // If LOADING, then the completion will use the token field to adjust
            // the received tokens (before possibly forwarding).
            assert(msg->tokens > 0);
            COMMIT
            {
                line->tokens += msg->tokens;
                
                // Combine the dirty flags
                line->dirty = line->dirty || msg->dirty;
                
                delete msg;
            }
        }
        else
        {
            // We don't have the line, see if we have an empty spot
            line = AllocateLine(msg->address, true);
            if (line == NULL)
            {
                // No, just forward it
                COMMIT{ msg->hops++; }
            }
            else
            {
                // Yes, place the line there and send out a token kill request
                COMMIT
                {
                    // This line moved over by 'hops + 1' caches, so add that many tokens
                    line->state    = LINE_FULL;
                    line->tag      = (msg->address / m_lineSize) / m_sets;
                    line->tokens   = msg->tokens + msg->hops + 1;
                    line->forward  = false; 
                    line->dirty    = msg->dirty;
                    line->updating = 0;
                    line->access   = GetKernel()->GetCycleNo();
                    std::fill(line->valid, line->valid + MAX_MEMORY_OPERATION_SIZE, true);
                    memcpy(line->data, msg->data.data, msg->data.size);
                    
                    // Also send out a kill request to the next cache for the created tokens
                    msg->type   = Message::REQUEST_KILL_TOKENS;
                    msg->tokens = msg->hops + 1;
                    msg->hops   = 0;
                }

                TraceWrite(msg->address, "Storing Evict Request. Sending Token Kill Request for %u tokens", msg->tokens);
            }
            
            if (!m_next.Send(msg))
            {
                DeadlockWrite("Unable to buffer request for next node");
                return false;
            }
        }
        break;

    case Message::REQUEST_UPDATE:
        assert(msg->hops <= m_numCaches);
        if (msg->hops == m_numCaches - 1)
        {
            // The update has come full circle.
            // Notify the sender of write consistency.
            assert(line != NULL);
            assert(line->updating > 0);
            
            if (!m_clients[msg->client]->OnMemoryWriteCompleted(msg->tid))
            {
                return FAILED;
            }

            COMMIT
            {
                line->updating--;
                delete msg;
            }
        }
        else
        {
            // Update the line, if we have it, and forward the message
            COMMIT
            {
                if (line != NULL)
                {
                    unsigned int offset = msg->address % m_lineSize;
                    memcpy(line->data + offset, msg->data.data, msg->data.size);
                    std::fill(line->valid + offset, line->valid + offset + msg->data.size, true);
                }
            
                msg->hops++;
            }
            
            if (!m_next.Send(msg))
            {
                DeadlockWrite("Unable to buffer request for next node");
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
    
// Called when a response has been received from the next node in the chain
bool COMA::Cache::OnResponseReceived(Message* msg)
{
    assert(msg != NULL);
    
    if (msg->hops > 0)
    {
        // It's not for us, forward it
        COMMIT{ msg->hops--; }
        if (!m_prev.Send(msg))
        {
            DeadlockWrite("Unable to buffer response for previous node");
            return false;
        }
        return true;
    }

    // It's for us, handle it
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to acquire lines");
        return false;
    }
    
    Line* line = FindLine(msg->address);
    
    switch (msg->type)
    {
    case Message::RESPONSE_READ:
    {
        // We received a line for a previous read miss on this cache
        assert(line != NULL);
        assert(line->state == LINE_LOADING);
        assert(msg->tokens > 0);
        
        COMMIT
        {
            // Store the data, masked by the already-valid bitmask
            for (size_t i = 0; i < msg->data.size; ++i)
            {
                if (!line->valid[i])
                {
                    line->data[i] = msg->data.data[i];
                    line->valid[i] = true;
                }
                else
                {
                    // This byte has been overwritten by processor.
                    // Update the message. This will ensure the response
                    // gets the latest value, and other processors too
                    // (which is fine, according to non-determinism).
                    msg->data.data[i] = line->data[i];
                }
            }
            line->state = LINE_FULL;
        }
        
        // Put the data on the bus for the processors
        if (!OnReadCompleted(msg->address, msg->data))
        {
            DeadlockWrite("Unable to notify clients of read completion");
            return false;
        }
        
        // Before we do anything, we combine the tokens from the response
        // with the token change from evictions and/or kills in the line.
        // We should end up with a positive token count.
        assert(msg->tokens + line->tokens > 0);
        const unsigned int tokens = msg->tokens + line->tokens;

        if (line->forward)
        {
            // We should have enough tokens for ourself and every
            // cache between us and the cache that wants the data.
            assert(tokens > 1 + line->hops);
            
            TraceWrite(msg->address, "Received Read Response with %u tokens; Forwarding %u tokens", msg->tokens, tokens - (1 + line->hops));
            
            // Forward the reply
            // When we forward a copy, we keep the reserved tokens and forward the rest
            COMMIT
            {
                msg->hops   = line->hops;
                msg->tokens = tokens - (1 + line->hops);
            
                line->tokens = 1 + line->hops;
            }
            
            if (!m_prev.Send(msg))
            {
                DeadlockWrite("Unable to buffer response for previous node");
                return false;
            }
        }
        else
        {
            TraceWrite(msg->address, "Received Read Response with %u tokens", msg->tokens);
        
            // Store all tokens for ourselves
            COMMIT
            {
                line->tokens = tokens;
                delete msg;
            }
        }

        break;
    }

    case Message::RESPONSE_FORWARD:
        if (line != NULL)
        {
            const unsigned int hops = msg->tokens;
            if (line->state == LINE_FULL)
            {
                // We have the data, just pretend that this is a read request hit.
                assert(line->tokens > 0);
                if ((unsigned int)line->tokens > 1 + hops)
                {
                    // We have the data and free tokens, answer the request.
                    // We reserve one token for ourselves and every cache miss
                    // on the way back and send the rest.
                    TraceWrite(msg->address, "Received Forward Response; Full Hit; Enough tokens available");            
                    COMMIT
                    {
                        msg->type   = Message::RESPONSE_READ;
                        msg->tokens = line->tokens - (1 + hops);
                        memcpy(msg->data.data, line->data, msg->data.size);

                        // Set our token count. One for ourselves, and one for each missed cache upstream.
                        line->tokens = 1 + hops;

                        // Also update last access time.
                        line->access = GetKernel()->GetCycleNo();
                    }
                }
                else
                {
                    // We have the data, but not enough tokens. This means another cache
                    // sits between us and the requester, so send a forward response to
                    // that cache.
                    TraceWrite(msg->address, "Received Forward Response; Full Hit; Not enough tokens available");            
                    COMMIT
                    {
                        msg->type   = Message::RESPONSE_FORWARD;
                        msg->tokens = line->tokens - hops;
                        msg->hops   = line->tokens - 1;
                    }
                }
                
                if (!m_prev.Send(msg))
                {
                    DeadlockWrite("Unable to buffer response for previous node");
                    return false;
                }
            }
            // Loading hit
            else if (line->forward)
            {
                // Another cache has already gotten here and set the forward flag.
                TraceWrite(msg->address, "Received Forward Response; Loading Hit; Sending Forward Response");            
                
                COMMIT
                {
                    if (hops > 1 + line->hops)
                    {
                        // The current forwarding cache is closer than the desired cache.
                        // We need to forward the message to the other cache with new update info.
                        msg->hops   = line->hops;
                        msg->tokens = hops - (1 + line->hops);
                    }
                    else
                    {
                        assert(msg->hops != line->hops);
                        // The desired cache is closer than the current forwarding cache.
                        // We need to forward the message to the desired cache to set up a forward there.
                        msg->hops   = hops;
                        msg->tokens = line->hops - (1 + hops);
                    }
                }
            
                if (!m_prev.Send(msg))
                {
                    DeadlockWrite("Unable to buffer response for previous node");
                    return false;
                }
            }
            else
            {
                // Store the new hop count (in the token count) as the hop count
                TraceWrite(msg->address, "Received Forward Response; Loading Hit; Setting Forward flag");            
                COMMIT
                {
                    line->forward = true;
                    line->hops    = msg->tokens;
                    delete msg;
                }
            }
        }
        else
        {
            // The line has been evicted since. Send the response back as a read request.
            // i.e., retry.
            TraceWrite(msg->address, "Received Forward Response; Miss; Resending Read Request");
            
            // Use the message's updated token count as the hop count to the source cache.
            // This way the message pretends to still be from the original cache.
            COMMIT
            {
                msg->type   = Message::REQUEST_READ;
                msg->hops   = msg->tokens + 1;
                msg->tokens = 0;
            }
            
            // FIXME: DANGER DANGER DANGER!!!
            // Deadlock possibility here? Response channel depending on request channel
            // Request channel already depends on response channel.
            printf("Uh oh?\n");
            if (!m_next.Send(msg))
            {
                DeadlockWrite("Unable to buffer request for next node");
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

bool COMA::Cache::OnReadCompleted(MemAddr addr, const MemData& data)
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
        return FAILED;
    }
    
    // Note that writes may not be of entire cache-lines
    unsigned int offset = req.address % m_lineSize;
    
    Line* line = FindLine(req.address);
    if (line == NULL)
    {
        // Write miss; write-allocate
        line = AllocateLine(req.address, false);
        if (line == NULL)
        {
            // Couldn't allocate a line
            return FAILED;
        }

        if (line->state != LINE_EMPTY)
        {
            // We're overwriting another line, evict the old line
            if (!EvictLine(line, req))
            {
                return FAILED;
            }
            return DELAYED;
        }

        // Reset the line
        COMMIT
        {
            line->state    = LINE_LOADING;
            line->tag      = (req.address / m_lineSize) / m_sets;
            line->tokens   = 0;
            line->forward  = false;
            line->dirty    = false;
            line->updating = 0;
            std::fill(line->valid, line->valid + MAX_MEMORY_OPERATION_SIZE, false);
        }
        
        // Send a request out for the cache-line
        Message* msg = NULL;
        COMMIT
        {
            msg = new Message;
            msg->type      = Message::REQUEST_READ;
            msg->address   = (req.address / m_lineSize) * m_lineSize;
            msg->data.size = m_lineSize;
            msg->tokens    = 0;
            msg->hops      = 0;
        }
            
        if (!m_next.Send(msg))
        {
            return FAILED;
        }
        
        // Now try against next cycle
        return DELAYED;
    }

    // Write hit
    // Although we may hit a loading line
    if (line->state == LINE_FULL && line->tokens == (int)m_numCaches)
    {
        // We have all the tokens, notify the sender client immediately
        if (!m_clients[req.client]->OnMemoryWriteCompleted(req.tid))
        {
            return FAILED;
        }
    }
    else
    {
        // There are other copies out there, send out an update message
        Message* msg = NULL;
        COMMIT
        {
            msg = new Message;
            msg->address   = req.address;
            msg->type      = Message::REQUEST_UPDATE;
            msg->hops      = 0;
            msg->client    = req.client;
            msg->tid       = req.tid;
            msg->data.size = req.size;
            memcpy(msg->data.data, req.data, req.size);
                
            // Lock the line to prevent eviction
            line->updating++;
        }
            
        if (!m_next.Send(msg))
        {
            DeadlockWrite("Unable to buffer request for next node");
            return FAILED;
        }
    }
    
    // Either way, at this point we have a line, so we
    // write the data into it.
    COMMIT
    {
        memcpy(line->data + offset, req.data, req.size);
        
        // Mark the written area valid
        std::fill(line->valid + offset, line->valid + offset + req.size, true);
        
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
        return FAILED;
    }

    Line* line = FindLine(req.address);

    TraceWrite(req.address, "Processing Bus Read Request: %s", (line == NULL) ? "Miss" : (line->state == LINE_LOADING) ? "Loading Hit" : "Full Hit");

    if (line == NULL)
    {
        // Read miss, allocate a line
        line = AllocateLine(req.address, false);
        if (line == NULL)
        {
            // Couldn't allocate a line
            return FAILED;
        }

        if (line->state != LINE_EMPTY)
        {
            // We're overwriting another line, evict the old line
            if (!EvictLine(line, req))
            {
                return FAILED;
            }
            return DELAYED;
        }

        // Reset the line
        COMMIT
        {
            line->state    = LINE_LOADING;
            line->tag      = (req.address / m_lineSize) / m_sets;
            line->tokens   = 0;
            line->forward  = false;
            line->dirty    = false;
            line->updating = 0;
            line->access   = GetKernel()->GetCycleNo();
            std::fill(line->valid, line->valid + MAX_MEMORY_OPERATION_SIZE, false);
        }
        
        // Send a request out
        Message* msg = NULL;
        COMMIT
        {
            msg = new Message;
            msg->type      = Message::REQUEST_READ;
            msg->address   = req.address;
            msg->data.size = req.size;
            msg->tokens    = 0;
            msg->hops      = 0;
        }
            
        if (!m_next.Send(msg))
        {
            return FAILED;
        }
    }
    // Read hit
    else if (line->state == LINE_FULL)
    {
        // Line is present and full

        // Return the data
        MemData data;
        data.size = req.size;
        COMMIT
        {
            memcpy(data.data, line->data, data.size);

            // Update LRU information
            line->access = GetKernel()->GetCycleNo();
        }

        if (!OnReadCompleted(req.address, data))
        {
            return FAILED;
        }
    }
    else 
    {
        // The line is already being loaded.
        // We can ignore this request; the completion of the earlier load
        // will put the data on the bus so this requester will also get it.
        assert(line->state == LINE_LOADING);
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
    }
    return (result == FAILED) ? FAILED : SUCCESS;
}
    
Result COMA::Cache::DoForwardNext()
{
    // Forward requests to next
    assert(!m_next.outgoing.Empty());
    Message* msg = m_next.outgoing.Front();
    if (!m_next.node->ReceiveMessagePrev(msg))
    {
        DeadlockWrite("Unable to send request to next node");
        return FAILED;
    }
    m_next.outgoing.Pop();
    return SUCCESS;
}

Result COMA::Cache::DoForwardPrev()
{
    // Forward requests to previous
    assert(!m_prev.outgoing.Empty());
    Message* msg = m_prev.outgoing.Front();
    if (!m_prev.node->ReceiveMessageNext(msg))
    {
        DeadlockWrite("Unable to send response to previous node");
        return FAILED;
    }
    m_prev.outgoing.Pop();
    return SUCCESS;
}
    
Result COMA::Cache::DoReceivePrev()
{
    // Handle received message from prev
    assert(!m_prev.incoming.Empty());
    if (!OnRequestReceived(m_prev.incoming.Front()))
    {
        return FAILED;
    }
    m_prev.incoming.Pop();
    return SUCCESS;
}

Result COMA::Cache::DoReceiveNext()
{
    // Handle received message from next
    assert(!m_next.incoming.Empty());
    if (!OnResponseReceived(m_next.incoming.Front()))
    {
        return FAILED;
    }
    m_next.incoming.Pop();
    return SUCCESS;
}

COMA::Cache::Cache(const std::string& name, COMA& parent, size_t numCaches, const Config& config) :
    Simulator::Object(name, parent),
    COMA::Object(name, parent),
    Node(name, parent),
    m_lineSize (config.getInteger<size_t>("CacheLineSize",           64)),
    m_assoc    (config.getInteger<size_t>("COMACacheAssociativity",   4)),
    m_sets     (config.getInteger<size_t>("COMACacheNumSets",       128)),
    m_numCaches(numCaches),
    m_clients  (std::max<size_t>(1, config.getInteger<size_t>("NumProcessorsPerCache", 4)), NULL),
    p_lines    (*this, "p_lines"),
    p_Requests ("requests",      delegate::create<Cache, &Cache::DoRequests   >(*this)),
    p_OutNext  ("outgoing-next", delegate::create<Cache, &Cache::DoForwardNext>(*this)),
    p_OutPrev  ("outgoing-prev", delegate::create<Cache, &Cache::DoForwardPrev>(*this)),
    p_InPrev   ("incoming-prev", delegate::create<Cache, &Cache::DoReceivePrev>(*this)),
    p_InNext   ("incoming-next", delegate::create<Cache, &Cache::DoReceiveNext>(*this)),
    p_bus      (*this, "p_bus"),
    m_requests (*parent.GetKernel(), config.getInteger<BufferSize>("COMACacheRequestBufferSize",  INFINITE)),
    m_responses(*parent.GetKernel(), config.getInteger<BufferSize>("COMACacheResponseBufferSize", INFINITE))
{
    // Create the cache lines
    m_lines.resize(m_assoc * m_sets);
    m_data.resize(m_lines.size() * m_lineSize);
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        Line& line = m_lines[i];
        line.state = LINE_EMPTY;
        line.data  = &m_data[i * m_lineSize];
    }

    m_requests.     Sensitive(p_Requests);   
    m_next.outgoing.Sensitive(p_OutNext);
    m_prev.outgoing.Sensitive(p_OutPrev);
    m_prev.incoming.Sensitive(p_InPrev);
    m_next.incoming.Sensitive(p_InNext);
    
    p_lines.AddProcess(p_InNext);
    p_lines.AddProcess(p_InPrev);
    p_lines.AddProcess(p_Requests);

    p_bus.AddProcess(p_InNext);
    p_bus.AddProcess(p_InPrev);
    p_bus.AddProcess(p_Requests);
    
    m_prev.arbitrator.AddProcess(p_InNext);   // Response is forwarded
    m_prev.arbitrator.AddProcess(p_InPrev);   // Incoming request gets response
    m_next.arbitrator.AddProcess(p_InPrev);   // Incoming request is forwarded
    m_next.arbitrator.AddProcess(p_Requests); // Request is generated
}

void COMA::Cache::Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const
{
    out <<
    "The L2 Cache in a COMA system is connected to the processors with a bus and to\n"
    "the rest of the COMA system via a ring network.\n\n"
    "Supported operations:\n"
    "- read <component>\n"
    "  Reads and displays the cache-lines, and global information such as hit-rate\n"
    "  and cache configuration.\n"
    "- read <component> buffers\n"
    "  Reads and displays the buffers in the cache\n";
}

void COMA::Cache::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "buffers")
    {
        // Read the buffers
        out << "Bus requests:" << endl << endl
            << "      Address      | Size | Type  |" << endl
            << "-------------------+------+-------+" << endl;
        for (Buffer<Request>::const_iterator p = m_requests.begin(); p != m_requests.end(); ++p)
        {
            out << hex << "0x" << setw(16) << setfill('0') << p->address << " | "
                << dec << setw(4) << right << setfill(' ') << p->size << " | "
                << (p->write ? "Write" : "Read ") << " | "
                << endl;
        }
        
        out << endl << "Interface with next node:" << endl << endl;
        m_next.Print(out);        

        out << endl << "Interface with previous node:" << endl << endl;
        m_prev.Print(out);

        return;
    }
    
    out << "Cache type:          ";
    if (m_assoc == 1) {
        out << "Direct mapped" << endl;
    } else if (m_assoc == m_lines.size()) {
        out << "Fully associative" << endl;
    } else {
        out << dec << m_assoc << "-way set associative" << endl;
    }

    out << "Cache size:      " << dec << (m_lineSize * m_lines.size()) << " bytes" << endl;
    out << "Cache line size: " << dec << m_lineSize << " bytes" << endl;
    out << endl;

    out << "Set |       Address       | Tokens |                       Data                      |" << endl;
    out << "----+---------------------+--------+-------------------------------------------------+" << endl;
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        const size_t set = i / m_assoc;
        const Line& line = m_lines[i];
        if (i % m_assoc == 0) {
            out << setw(3) << setfill(' ') << dec << right << set;
        } else {
            out << "   ";
        }

        if (line.state == LINE_EMPTY) {
            out << " |                     |        |                                                 |";
        } else {
            out << " | "
                << hex << "0x" << setw(16) << setfill('0') << (line.tag * m_sets + set) * m_lineSize;

            switch (line.state)
            {
                case LINE_LOADING: out << (line.forward ? "F" : "L"); break;
                default: out << " ";
            }
            out << " | " << dec << setfill(' ') << setw(6) << line.tokens << " |";

            // Print the data
            out << hex << setfill('0');
            static const int BYTES_PER_LINE = 16;
            for (size_t y = 0; y < m_lineSize; y += BYTES_PER_LINE)
            {
                for (size_t x = y; x < y + BYTES_PER_LINE; ++x) {
                    out << " ";
                    if (line.valid[x]) {
                        out << setw(2) << (unsigned)(unsigned char)line.data[x];
                    } else {
                        out << "  ";
                    }
                }
                
                out << " | ";
                if (y + BYTES_PER_LINE < m_lineSize) {
                    // This was not yet the last line
                    out << endl << "    |                     |        |";
                }
            }
        }
        out << endl;
        out << ((i + 1) % m_assoc == 0 ? "----" : "    ");
        out << "+---------------------+--------+-------------------------------------------------+" << endl;
    }
}

}
