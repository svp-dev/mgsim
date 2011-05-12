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

void ZLCOMA::Cache::RegisterClient(PSize pid, IMemoryCallback& callback, const Process* processes[])
{
    size_t index = pid % m_clients.size();
    assert(m_clients[index] == NULL);
    m_clients[index] = &callback;

    for (size_t i = 0; processes[i] != NULL; ++i)
    {
        p_bus.AddProcess(*processes[i]);
    }
}

void ZLCOMA::Cache::UnregisterClient(PSize pid)
{
    size_t index = pid % m_clients.size();
    assert(m_clients[index] != NULL);
    m_clients[index] = NULL;
}

// Called from the processor on a memory read (typically a whole cache-line)
// Just queues the request.
bool ZLCOMA::Cache::Read(PSize pid, MemAddr address, MemSize size)
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
        return false;
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
bool ZLCOMA::Cache::Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid)
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

ZLCOMA::Cache::Line* ZLCOMA::Cache::FindLine(MemAddr address)
{
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line& line = m_lines[set + i];
        if (line.valid && line.tag == tag)
        {
            // The wanted line was in the cache
            return &line;
        }
    }
    return NULL;
}

const ZLCOMA::Cache::Line* ZLCOMA::Cache::FindLine(MemAddr address) const
{
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        const Line& line = m_lines[set + i];
        if (line.valid && line.tag == tag)
        {
            // The wanted line was in the cache
            return &line;
        }
    }
    return NULL;
}

ZLCOMA::Cache::Line* ZLCOMA::Cache::GetEmptyLine(MemAddr address)
{
    const size_t set = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    // Return the first found empty line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line& line = m_lines[set + i];
        if (!line.valid)
        {
            return &line;
        }
    }
    return NULL;
}

// function for find replacement line
ZLCOMA::Cache::Line* ZLCOMA::Cache::GetReplacementLine(MemAddr address)
{
    Line *linelruw = NULL; // replacement line for write-back request
    Line *linelrue = NULL; // replacement line for eviction request

    const size_t set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;
    for (unsigned int i = 0; i < m_assoc; i++)
    {
        Line& line = m_lines[set + i];
        
        // Return the first found empty one
        if (!line.valid)
            return &line;

        // Pending lines don't count as normal replacable lines
        if (!line.pending_read && !line.pending_write)
        {
            if (!line.dirty)
            {
                if (linelrue == NULL || line.time < linelrue->time)
                    linelrue = &line;
            }
            else
            {
                if (linelruw == NULL || line.time < linelruw->time)
                    linelruw = &line;
            }
        }
    }
    
    // Prefer to to evict non-dirty lines since they don't require writeback to off-chip memory.
    return (linelrue != NULL) ? linelrue : linelruw;
}

Result ZLCOMA::Cache::OnMessageReceived(Message* msg)
{
    if (msg->ignore)
    {
        // Just pass it on
        if (!SendMessage(msg, MINSPACE_FORWARD))
        {
            return FAILED;
        }
		return SUCCESS;
    }
    
    switch (msg->type)
    {
    case Message::ACQUIRE_TOKENS:
		return (msg->source == m_id)
			? OnAcquireTokensRet(msg)
			: OnAcquireTokensRem(msg);

    case Message::READ:
		return (msg->source == m_id)
			? OnReadRet(msg)
			: OnReadRem(msg);

    case Message::EVICTION:
        return OnEviction(msg);

    case Message::LOCALDIR_NOTIFICATION:
        // Request is meant for directory, we shouldn't receive it again
        assert(msg->source != m_id);
    
        // Just pass it on
        if (!SendMessage(msg, MINSPACE_FORWARD))
        {
            return FAILED;
        }
        return SUCCESS;

    default:
        assert(false);
        break;
    }
    return FAILED;
}

bool ZLCOMA::Cache::ClearLine(Line* line)
{
    // Send line invalidation to caches
    if (!p_bus.Invoke())
    {
        DeadlockWrite("Unable to acquire the bus for sending invalidation");
        return false;
    }

    size_t  set     = (line - &m_lines[0]) / m_assoc;
    MemAddr address = (line->tag * m_sets + set) * m_lineSize;
    
    for (std::vector<IMemoryCallback*>::const_iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (*p != NULL && !(*p)->OnMemoryInvalidated(address))
        {
            DeadlockWrite("Unable to send invalidation to clients");
            return false;
        }
    }
    
    COMMIT{ line->valid = false; }
    
    return true;
}

// Evict a cache-line
bool ZLCOMA::Cache::EvictLine(Line* line, const Request& req)
{
    assert(line->valid);
    
    size_t  set     = (line - &m_lines[0]) / m_assoc;
    MemAddr address = (line->tag * m_sets + set) * m_lineSize;
     
    Message* msg = new Message();
    COMMIT
    {
        msg->transient = false;
        msg->type      = Message::EVICTION;
        msg->address   = address;
        msg->ignore    = false;
        msg->source    = m_id;    
        msg->priority  = line->priority;
        msg->dirty     = line->dirty;
        msg->tokens    = line->tokens;
        std::copy(line->data,    line->data    + m_lineSize, msg->data);
        std::copy(line->bitmask, line->bitmask + m_lineSize, msg->bitmask);
    }
    
    TraceWrite(address, "Evicting with %u tokens due to miss for 0x%llx", line->tokens, (unsigned long long)req.address);
    
    if (!SendMessage(msg, MINSPACE_FORWARD))
    {
        return false;
    }

    if (!ClearLine(line))
    {
        return false;
    }

    return true;
}

// Local Read from a memory client on the bus
Result ZLCOMA::Cache::OnReadRequest(const Request& req)
{
    assert(req.size == m_lineSize);
    
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to acquire lines");
        return FAILED;
    }

    Line* line = FindLine(req.address);
    if (line == NULL)
    {
        // We don't have the line; allocate a line and fetch the data
        line = GetReplacementLine(req.address);
        if (line == NULL)
        {
            // No cache line available; stall
            return FAILED;
        }

        if (line->valid)
        {
            // Line is already in use; evict it
            const size_t set = (line - &m_lines[0]) / m_assoc;
            const MemAddr address = (line->tag * m_sets + set) * m_lineSize;
            TraceWrite(req.address, "Processing Bus Read Request: Miss; Evicting 0x%llx", (unsigned long long)address);

            if (!EvictLine(line, req))
            {
                return FAILED;
            }
            
            return DELAYED;
        }

        TraceWrite(req.address, "Processing Bus Read Request: Miss; Sending Read Request");

        // Reset the cache-line
        COMMIT
        {
            line->tag           = (req.address / m_lineSize) / m_sets;
            line->time          = GetCycleNo();
            line->valid         = true;
            line->dirty         = false;
            line->tokens        = 0;
            line->transient     = false;
            line->priority      = false;
            line->pending_read  = false;
            line->pending_write = false;
            std::fill(line->bitmask, line->bitmask + m_lineSize, false);
        }
    }
    else if (!contains(line->bitmask, line->bitmask + m_lineSize, false))
    {
        // We have all data in the line; return it to the memory clients
        // Note that this can happen before a read or write request has
        // returned: local or remote writes can fill up the cache-line.
        TraceWrite(req.address, "Processing Bus Read Request: Full Hit");
    
        // Return the data
        MemData data;
        data.size = m_lineSize;
        COMMIT
        {
            std::copy(line->data, line->data + m_lineSize, data.data);
            
            // Update LRU time of the line
            line->time = GetCycleNo();
            
            m_numHits++;
        }
    
        if (!OnReadCompleted(req.address, data))
        {
            return FAILED;
        }
        return SUCCESS;
    }
    else if (line->pending_read)
    {
        // The line is being fetched already. When that request completes, all clients of this
        // cache will be notified, including the sender of this request. Thus, we can simply
        // drop this request.
        TraceWrite(req.address, "Processing Bus Read Request: Read Loading Hit");
        
        // Counts as a miss because we have to wait
        COMMIT{ m_numMisses++; }
        return SUCCESS;
    }
    else
    {
        TraceWrite(req.address, "Processing Bus Read Request: Miss; Sending Read Request");
        
    }

    // Send request for a copy of the cache-line
    Message* msg = NULL;
    COMMIT
    {
        msg = new Message();
        msg->type      = Message::READ;
        msg->address   = req.address;
        msg->ignore    = false;
        msg->source    = m_id;
        msg->tokens    = 0;
        msg->priority  = false;
        msg->transient = false;
        std::fill(msg->bitmask, msg->bitmask + m_lineSize, false);
        
        line->pending_read = true;

        // Counts as a miss because we have to wait
        m_numMisses++;
    }
        
    if (!SendMessage(msg, MINSPACE_INSERTION))
    {
        return FAILED;
    }
    return SUCCESS;
}

// Local Write from a memory client on the bus
Result ZLCOMA::Cache::OnWriteRequest(const Request& req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to acquire lines");
        return FAILED;
    }
    
    bool newline = false;

    Line* line = FindLine(req.address);
    if (line == NULL)
    {
        // We don't have the line; allocate a line
        line = GetReplacementLine(req.address);
        if (line == NULL)
        {
            // No line available; stall
            return FAILED;
        }

        if (line->valid)
        {
            // Line is already in use; evict it
            if (!EvictLine(line, req))
            {
                return FAILED;
            }
            return DELAYED;
        }
        
        // Reset the line
        COMMIT
        {
            line->tag           = (req.address / m_lineSize) / m_sets;
            line->valid         = true;
            line->dirty         = false;
            line->tokens        = 0;
            line->transient     = false;
            line->priority      = false;
            line->pending_read  = false;
            line->pending_write = false;
            std::fill(line->bitmask, line->bitmask + MAX_MEMORY_OPERATION_SIZE, false);
        }
        
        newline = true;
    }
    
    // Update line; write data
    COMMIT
    {
        line->time = GetCycleNo();
        line->dirty = true;
        
        unsigned int offset = req.address % m_lineSize;
        
        std::copy(req.data, req.data + req.size, line->data + offset);
        std::fill(line->bitmask + offset, line->bitmask + offset + req.size, true);
    }
    
    if (!newline && !line->transient && line->tokens == m_numTokens)
    {
        assert(line->priority);
            
        // This line has all the tokens.
        // We can acknowledge directly after writing.
        TraceWrite(req.address, "Processing Bus Write Request: Exclusive Hit");
        
        if (!m_clients[req.client]->OnMemoryWriteCompleted(req.tid))
        {
            return FAILED;
        }
            
        return SUCCESS;
    }

    // Save acknowledgement. When we get all tokens later, these will get acknowledged.
    COMMIT{ line->ack_queue.push_back(WriteAck(req.client, req.tid)); }
    
    if (line->pending_write)
    {
        // There's already a write pending on this line; a token acquisition message has already been
        // sent out. We don't send another one.
        TraceWrite(req.address, "Processing Bus Write Request: Pending Hit; Queuing acknowledgement");
        return SUCCESS;
    }
    
    // Send message to acquire all tokens (and, optionally, data)
    if (newline)
        TraceWrite(req.address, "Processing Bus Write Request: Miss; Acquiring tokens");
    else
        TraceWrite(req.address, "Processing Bus Write Request: Hit; Acquiring tokens");
        
    Message* msg = NULL;
    COMMIT
    {
        msg = new Message();
        msg->type      = Message::ACQUIRE_TOKENS;
        msg->address   = req.address;
        msg->ignore    = false;
        msg->source    = m_id;
        msg->tokens    = 0;
        msg->priority  = false;
        msg->transient = false;
            
        // Send our current (updated) data with the message
        std::copy(line->data,    line->data    + m_lineSize, msg->data);
        std::copy(line->bitmask, line->bitmask + m_lineSize, msg->bitmask);

        if (line->priority)
        {
            // Line has the priority token, give it to the request
            msg->priority = true;
            msg->tokens++;

            line->priority = false;
            line->tokens--;
        }
    
        // We have a write in transit now
        line->pending_write = true;
    }
    
    if (!SendMessage(msg, MINSPACE_INSERTION))
    {
        return FAILED;
    }
    return SUCCESS;
}

// Network remote request to acquire all tokens and data.
// Issued by writes.
Result ZLCOMA::Cache::OnAcquireTokensRem(Message* req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to acquire lines");
        return FAILED;
    }

	Line* line = FindLine(req->address);
    if (line == NULL)
    {
        // We do not have the line, forward message
        if (!SendMessage(req, MINSPACE_FORWARD))
        {
            return FAILED;
        }
		return SUCCESS;
    }

    // See if the request has data that we don't, and vica versa
    COMMIT
    {
        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            if (!req->bitmask[i] && line->bitmask[i])
            {
                req->data[i] = line->data[i];
                req->bitmask[i] = true;
            }
            else if (req->bitmask[i] && !line->bitmask[i])
            {
                line->data[i] = req->data[i];
                line->bitmask[i] = true;
            }
        }
    }
    
    if (line->pending_read)
    {
        // The line has a pending read.
        if (!req->transient || line->priority)
        {
            // Take the line's tokens. We cannot do this if the request has transient
            // tokens, unless it gets the priority token (which makes them permanent).
            COMMIT
            {
                req->tokens += line->tokens;
                req->priority = req->priority || line->priority;
                req->transient = false;

                line->tokens = 0;
                line->priority = false;
            }
        }
    }
    else if (!line->pending_write)
    {
        // Stable line without pending requests.
        // The line will lose all its tokens and needs to be cleaned up.
        assert(line->tokens > 0);
        assert(line->transient == false);

        // Give the line's tokens to the request
        COMMIT
        {
            req->tokens += line->tokens;
            if (line->priority)
            {
                req->priority = true;
                req->transient = false;
            }
        }
        
        if (!ClearLine(line))
        {
            return FAILED;
        }
    }
    // Line has a pending write
    else if (req->priority)
    {
        // The request has the priority token and will get the tokens.
        assert(req->transient == false);
        assert(line->priority == false);
                           
        COMMIT
        {
            req->tokens += line->tokens;
            line->tokens = 0;
        }
    }
    else if (line->priority)
    {
        // The line has the priority token and will get the tokens
        assert(line->transient == false);
        assert(req->priority == false);
        
        unsigned int tokens = req->tokens;

        COMMIT
        {
            line->tokens += req->tokens;
            req->tokens = 0;
        }

        if (req->transient)
        {
            assert(tokens > 0);
        
            // We've taken transient tokens from the request and made them permanent
            Message *reqnotify = NULL;
            COMMIT
            {
                reqnotify = new Message();
                reqnotify->type    = Message::LOCALDIR_NOTIFICATION;
                reqnotify->address = req->address;
                reqnotify->ignore  = false;
                reqnotify->source  = m_id;
                reqnotify->tokens  = tokens;

                req->transient = false;
            }
            
            if (!SendMessage(reqnotify, MINSPACE_FORWARD))
            {
                return FAILED;
            }
            
            // We need to delay because we sent a message. We can't send the
            // request below in the same cycle. Note that the current state
            // of the line means that the message will not come here again.
            return DELAYED;
        }
    }
    else if (!req->transient && !line->transient)
    {
        // Neither has the priority token. Both line and request will get the same number of tokens,
        // but in a transient state. The priority token will decide which one will become permanent.
        // Note that we cannot do this if the request or line has transient tokens.
        COMMIT
        {
            req->tokens    = line->tokens    = req->tokens + line->tokens;
            req->transient = line->transient = true;
        }
    }
    
    if (!SendMessage(req, MINSPACE_FORWARD))
    {
        return FAILED;
    }
    return SUCCESS;
}

// Write request returns for all tokens and data
Result ZLCOMA::Cache::OnAcquireTokensRet(Message* req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to acquire lines");
        return FAILED;
    }

    Line* line = FindLine(req->address);
    assert(line != NULL);
    assert(line->pending_write);
    assert(line->dirty);

    // Update the cache-line with data from the request
    COMMIT
    {
        for (unsigned int i = 0 ; i < m_lineSize; ++i)
        {
            if (req->bitmask[i] && !line->bitmask[i])
            {
                line->bitmask[i] = true;
                line->data[i] = req->data[i];
            }
        }
    }
    
    unsigned int tokens = line->tokens;
    if (line->transient)
    {
        // The line's tokens have been invalidated since the request was sent
        assert(!line->priority);
        if (!req->priority)
        {
            // No priority token, so discard the transient tokens in the line
            tokens = 0;
        }
        COMMIT{ line->transient = false; }
    }
    
    if (!req->transient || line->priority)
    {
        // Give the request's tokens to the line
        tokens += req->tokens;
        
        COMMIT{ line->priority = line->priority || req->priority; }

        if (req->transient && req->tokens > 0)
        {
            // We've taken transient tokens from the request and made them permanent
            Message *reqnotify = NULL;
            COMMIT
            {
                reqnotify = new Message();
                reqnotify->type    = Message::LOCALDIR_NOTIFICATION;
                reqnotify->address = req->address;
                reqnotify->ignore  = false;
                reqnotify->source  = m_id;
                reqnotify->tokens  = req->tokens;
            }
            
            // FIXME: sending two messages (in case we also don't have all tokens)
            if (!SendMessage(reqnotify, MINSPACE_FORWARD))
            {
                return FAILED;
            }
        }
    }
    
    if (tokens < m_numTokens)
    {
        // We don't have all the tokens necessary to acknowledge the pending writes.
        // Send the request again.
        TraceWrite(req->address, "Tokens Acquisition returned; Not enough tokens; Resend");

        // FIXME: sending two messages (in case we also notify dir)
        if (!SendMessage(req, MINSPACE_FORWARD))
        {
            return FAILED;
        }
    }
    else
    {        
        // We have all the tokens now, acknowledge writes
        if (!line->ack_queue.empty())
        {
            const WriteAck& ack = line->ack_queue.back();
            TraceWrite(req->address, "Tokens Acquisition returned; All tokens; Acknowledging write %u@%u", (unsigned)ack.second, (unsigned)ack.first);
            if (!m_clients[ack.first]->OnMemoryWriteCompleted(ack.second))
            {
                return FAILED;
            }
            COMMIT{ line->ack_queue.pop_back(); }
            return DELAYED;
        }
        
        COMMIT
        {
            line->pending_write = false;
            delete req;
        }        
    }

    COMMIT{ line->tokens = tokens; }
    
    return SUCCESS;
}

// Remote request to acquire copy of cache line
Result ZLCOMA::Cache::OnReadRem(Message* req)
{
    assert(req->transient == false);    // Read requests never carry transient tokens

    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to acquire lines");
        return FAILED;
    }

	Line* line = FindLine(req->address);
    if (line == NULL)
    {
        // We do not have the line, forward message
        if (!SendMessage(req, MINSPACE_FORWARD))
        {
            return FAILED;
        }
		return SUCCESS;
    }
    
    // Exchange data between line and request
    for (unsigned int i = 0; i < m_lineSize; i++)
    {
        if (line->bitmask[i])
        {
            req->data[i] = line->data[i];
            req->bitmask[i] = true;
        }
        else if (req->bitmask[i])
        {
            line->data[i] = req->data[i];
            line->bitmask[i] = true;
        }
    }
    
    if (line->pending_write)
    {
        // The line has a pending write, which means it wants all the tokens.
        // Give all tokens in this request to the line.
        //
        // If the line has been invalidated, we can only do this if
        // the line gets the priority token from the request.
        if (!line->transient || req->priority)
        {
            TraceWrite(req->address, "Received Read Request; Stealing tokens for pending write");
            
            line->tokens += req->tokens;
            line->priority = line->priority || req->priority;
            line->transient = false;
        
            // The request continues on without tokens
            req->tokens = 0;
            req->priority = false;
        }
        else
        {
            TraceWrite(req->address, "Received Read Request; Attaching data");
        }
    }
    // Stable line without pending requests OR line with a pending read.
    // Either way, grab a token if we can.
    else if (req->tokens == 0 && line->tokens > 1)
    {
        TraceWrite(req->address, "Received Read Request; Attaching data and tokens");

        // Give a token to the request
        line->tokens--;
        req->tokens++;
    }
    
    if (!SendMessage(req, MINSPACE_FORWARD))
    {
        return FAILED;
    }
    return SUCCESS;
}

// Read request returns
Result ZLCOMA::Cache::OnReadRet(Message* req)
{
    assert(req->transient == false);    // Read requests never carry transient tokens

    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to acquire lines");
        return FAILED;
    }

    Line* line = FindLine(req->address);
    assert(line != NULL);
    assert(line->pending_read);

    // See if the line will be full
    unsigned int missing_bytes = 0;
    for (unsigned int i = 0; i < m_lineSize; i++)
    {
        if (!line->bitmask[i] && !req->bitmask[i])
        {
            missing_bytes++;
        }
    }
    
    if (missing_bytes > 0)
        TraceWrite(req->address, "Received Read Response with %u tokens; Sending request for remaining %u bytes", req->tokens, missing_bytes);
    else        
        TraceWrite(req->address, "Received Read Response with %u tokens; Read completed", req->tokens);        
        
    COMMIT
    {
        // Update the line with the request's data
        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            if (!line->bitmask[i] && req->bitmask[i])
            {
                line->data[i] = req->data[i];
                line->bitmask[i] = true;
            }
        }

        // Give tokens to the line
        line->tokens += req->tokens;
        line->priority = line->priority || req->priority;

        req->tokens = 0;
        req->priority = false;
    }
    
    // If we still have no full line, send the read request out again.
    // We need the entire line to acknowledge the pending read to the line.
    if (missing_bytes > 0)
    {
        if (!SendMessage(req, MINSPACE_FORWARD))
        {
            return FAILED;
        }
    }
    else
    {
        // Return the data
        MemData data;
        data.size = m_lineSize;

        COMMIT
        {
            line->pending_read = false;

            std::copy(line->data, line->data + m_lineSize, data.data);            
        }
        
        // Acknowledge the read to the memory clients
        if (!OnReadCompleted(req->address, data))
        {
            return FAILED;
        }
    
        COMMIT{ delete req; }
    }
    return SUCCESS;
}

// network disseminate token and data, EV, WB, include IJ
Result ZLCOMA::Cache::OnEviction(Message* req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to acquire lines");
        return FAILED;
    }

    Line* line = FindLine(req->address);
    if (line == NULL)
    {
        // We don't have the line
        if (!m_inject)
        {
            // Do not try to inject
            if (!SendMessage(req, MINSPACE_FORWARD))
            {
                return FAILED;
            }
            return SUCCESS;
        }
        
        // Try to allocate an empty line to inject the evicted line
        line = GetEmptyLine(req->address);
        if (line == NULL)
        {
            // No free line
            if (!SendMessage(req, MINSPACE_FORWARD))
            {
                return FAILED;
            }
            return SUCCESS;
        }
        
        // Store evicted line in the allocated line
        COMMIT
        {
            line->tag           = (req->address / m_lineSize) / m_sets;
            line->time          = GetCycleNo();
            line->dirty         = req->dirty;
            line->tokens        = req->tokens;
            line->pending_read  = false;
            line->pending_write = false;
            line->transient     = false;
            line->priority      = req->priority;

            std::copy(req->data, req->data + m_lineSize, line->data);
            std::fill(line->bitmask, line->bitmask + m_lineSize, true);
            
            delete req;
        }
    }
    // We have the line
    else if (line->transient)
    {
        // We can't merge with invalidated lines
        if (!SendMessage(req, MINSPACE_FORWARD))
        {
            return FAILED;
        }
    }
    else
    {
        // Merge the data and tokens from the eviction with the line
        COMMIT
        {
            for (unsigned int i = 0; i < m_lineSize; i++)
            {
                if (!line->bitmask[i])
                {
                    line->data[i]    = req->data[i];
                    line->bitmask[i] = req->bitmask[i];
                }
            }

            line->tokens += req->tokens;
            line->priority = line->priority || req->priority;
            line->dirty = line->dirty || req->dirty;
            delete req;
        }
    }
    return SUCCESS;
}

bool ZLCOMA::Cache::AcknowledgeQueuedWrites(Line* line)
{
    for (size_t i = 0; i < line->ack_queue.size(); ++i)
    {
        if (!m_clients[line->ack_queue[i].first]->OnMemoryWriteCompleted(line->ack_queue[i].second))
        {
            return false;
        }
    }
    COMMIT{ line->ack_queue.clear(); }
    return true;
}

bool ZLCOMA::Cache::OnReadCompleted(MemAddr addr, const MemData& data)
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

Result ZLCOMA::Cache::DoRequests()
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

Result ZLCOMA::Cache::DoReceive()
{
    // Handle received message from prev
    assert(!m_incoming.Empty());
    Result result = OnMessageReceived(m_incoming.Front());
    if (result == SUCCESS)
    {
        m_incoming.Pop();
    }
    return (result == FAILED) ? FAILED : SUCCESS;
}

ZLCOMA::Cache::Cache(const std::string& name, ZLCOMA& parent, Clock& clock, CacheID id, size_t numTokens, Config& config) :
    Simulator::Object(name, parent),
    Node(name, parent, clock),
    m_lineSize (config.getValue<size_t>("CacheLineSize")),
    m_assoc    (config.getValue<size_t>(parent, "L2CacheAssociativity")),
    m_sets     (config.getValue<size_t>(parent, "L2CacheNumSets")),
    m_numTokens(numTokens),
    m_inject   (config.getValue<bool>(parent, "EnableCacheInjection")),
    m_id       (id),
    m_clients  (config.getValue<size_t>("NumProcessorsPerL2Cache"), NULL),
    p_lines    (*this, clock, "p_lines"),
    m_numHits  (0),
    m_numMisses(0),
    p_Requests ("requests", delegate::create<Cache, &Cache::DoRequests>(*this)),
    p_In       ("incoming", delegate::create<Cache, &Cache::DoReceive>(*this)),
    p_bus      (*this, clock, "p_bus"),
    m_requests ("b_requests", *this, clock, config.getValue<BufferSize>(*this, "RequestBufferSize")),
    m_responses("b_responses", *this, clock, config.getValue<BufferSize>(*this, "ResponseBufferSize"))
{
    RegisterSampleVariableInObject(m_numHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numMisses, SVC_CUMULATIVE);

    // Create the cache lines
    m_lines.resize(m_assoc * m_sets);
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        m_lines[i].valid = false;
    }

    m_requests.Sensitive(p_Requests);
    m_incoming.Sensitive(p_In);

    p_lines.AddProcess(p_In);
    p_lines.AddProcess(p_Requests);

    p_bus.AddProcess(p_In);                   // Update triggers write completion
    p_bus.AddProcess(p_Requests);             // Read or write hit

    config.registerObject(*this, "cache");
    config.registerProperty(*this, "assoc", (uint32_t)m_assoc);
    config.registerProperty(*this, "sets", (uint32_t)m_sets);
    config.registerProperty(*this, "lsz", (uint32_t)m_lineSize);
    config.registerProperty(*this, "freq", (uint32_t)clock.GetFrequency());
}

void ZLCOMA::Cache::Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const
{
    out <<
    "The L2 Cache in a COMA system is connected to the processors with a bus and to\n"
    "the rest of the COMA system via a ring network.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Reads and displays the cache-lines, and global information such as hit-rate\n"
    "  and cache configuration.\n"
    "- inspect <component> buffers\n"
    "  Reads and displays the buffers in the cache\n";
}

void ZLCOMA::Cache::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
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

        out << endl << "Ring interface:" << endl << endl;
        Print(out);
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

    out << "Cache size:       " << dec << (m_lineSize * m_lines.size()) << " bytes" << endl;
    out << "Cache line size:  " << dec << m_lineSize << " bytes" << endl;
    out << "Current hit rate: ";
    if (m_numHits + m_numMisses > 0) {
        out << setprecision(2) << fixed << m_numHits * 100.0f / (m_numHits + m_numMisses) << "%";
    } else {
        out << "N/A";
    }
    out << " (" << dec << m_numHits << " hits, " << m_numMisses << " misses)" << endl;
    out << endl;

    out << "Set |         Address        | Tokens |                       Data                      |" << endl;
    out << "----+------------------------+--------+-------------------------------------------------+" << endl;
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        const size_t set = i / m_assoc;
        const Line& line = m_lines[i];
        if (i % m_assoc == 0) {
            out << setw(3) << setfill(' ') << dec << right << set;
        } else {
            out << "   ";
        }

        if (!line.valid) {
            out << " |                        |        |                                                 |";
        } else {
            out << " | "
                << hex << "0x" << setw(16) << setfill('0') << (line.tag * m_sets + set) * m_lineSize
                << ' '
                << (line.pending_read  ? 'R' : ' ')
                << (line.pending_write ? 'W' : ' ')
                << (line.dirty         ? 'D' : ' ')
                << " | " << dec << setfill(' ') << setw(5) << line.tokens 
                << (line.priority ? 'P' : line.transient ? 'T' : ' ')
                << " |";

            // Print the data
            out << hex << setfill('0');
            static const int BYTES_PER_LINE = 16;
            for (size_t y = 0; y < m_lineSize; y += BYTES_PER_LINE)
            {
                for (size_t x = y; x < y + BYTES_PER_LINE; ++x) {
                    out << " ";
                    if (line.bitmask[x]) {
                        out << setw(2) << (unsigned)(unsigned char)line.data[x];
                    } else {
                        out << "  ";
                    }
                }

                out << " | ";
                if (y + BYTES_PER_LINE < m_lineSize) {
                    // This was not yet the last line
                    out << endl << "    |                        |        |";
                }
            }
        }
        out << endl;
        out << ((i + 1) % m_assoc == 0 ? "----" : "    ");
        out << "+------------------------+--------+-------------------------------------------------+" << endl;
    }
}

}
