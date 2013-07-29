#include "Cache.h"
#include <sim/config.h>
#include <sim/sampling.h>

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

MCID ZLCDMA::Cache::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages)
{
    MCID index = m_clients.size();
    m_clients.resize(index + 1);

    m_clients[index] = &callback;

    p_bus.AddCyclicProcess(process);
    traces = m_requests;

    m_storages *= opt(storages);
    p_Requests.SetStorageTraces(opt(m_storages ^ GetOutgoingTrace()));
    p_In.SetStorageTraces(opt(m_storages ^ GetOutgoingTrace()));

    return index;
}

void ZLCDMA::Cache::UnregisterClient(MCID id)
{
    assert(m_clients[id] != NULL);
    m_clients[id] = NULL;
}

// Called from the processor on a memory read (typically a whole cache-line)
// Just queues the request.
bool ZLCDMA::Cache::Read(MCID id, MemAddr address)
{
    assert(address % m_lineSize == 0);

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
bool ZLCDMA::Cache::Write(MCID id, MemAddr address, const MemData& data, WClientID wid)
{

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
    req.client  = id;
    req.wid     = wid;
    COMMIT{
    std::copy(data.data, data.data + m_lineSize, req.mdata.data);
    std::copy(data.mask, data.mask + m_lineSize, req.mdata.mask);
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
            if (!client->OnMemorySnooped(req.address, req.mdata.data, req.mdata.mask))
            {
                DeadlockWrite("Unable to snoop data to cache clients");
                return false;
            }
        }
    }

    return true;
}

ZLCDMA::Cache::Line* ZLCDMA::Cache::FindLine(MemAddr address)
{
    MemAddr tag;
    size_t setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

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

const ZLCDMA::Cache::Line* ZLCDMA::Cache::FindLine(MemAddr address) const
{
    MemAddr tag;
    size_t setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

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

ZLCDMA::Cache::Line* ZLCDMA::Cache::GetEmptyLine(MemAddr address, MemAddr& tag)
{
    size_t setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

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
ZLCDMA::Cache::Line* ZLCDMA::Cache::GetReplacementLine(MemAddr address, MemAddr& tag)
{
    Line *linelruw = NULL; // replacement line for write-back request
    Line *linelrue = NULL; // replacement line for eviction request

    size_t setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

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

Result ZLCDMA::Cache::OnMessageReceived(Message* msg)
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
        UNREACHABLE;
        break;
    }
    return FAILED;
}

bool ZLCDMA::Cache::ClearLine(Line* line)
{
    // Send line invalidation to caches
    if (!p_bus.Invoke())
    {
        DeadlockWrite("Unable to acquire the bus for sending invalidation");
        return false;
    }

    size_t  set     = (line - &m_lines[0]) / m_assoc;
    MemAddr address = m_selector.Unmap(line->tag, set) * m_lineSize;

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
bool ZLCDMA::Cache::EvictLine(Line* line, const Request& req)
{
    assert(line->valid);

    size_t  set     = (line - &m_lines[0]) / m_assoc;
    MemAddr address = m_selector.Unmap(line->tag, set) * m_lineSize;

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
Result ZLCDMA::Cache::OnReadRequest(const Request& req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Lines busy, cannot process bus read request");
        return FAILED;
    }

    Line* line = FindLine(req.address);
    if (line == NULL)
    {
        MemAddr tag;

        // We don't have the line; allocate a line and fetch the data
        line = GetReplacementLine(req.address, tag);
        if (line == NULL)
        {
            ++m_numConflicts;
            DeadlockWrite("Unable to allocate line for bus read request");
            return FAILED;
        }

        if (line->valid)
        {
            // Line is already in use; evict it
            TraceWrite(req.address, "Processing Bus Read Request: Miss; Evicting line with tag %#016llx",
                       (unsigned long long)line->tag);

            if (!EvictLine(line, req))
            {
                ++m_numConflicts;
                DeadlockWrite("Unable to evict line for bus read request");
                return FAILED;
            }

            COMMIT { ++m_numConflicts; ++m_numResolved; }
            return DELAYED;
        }

        TraceWrite(req.address, "Processing Bus Read Request: Miss; Sending Read Request");

        // Reset the cache-line
        COMMIT
        {
            line->tag           = tag;
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
        char data[m_lineSize];
        COMMIT
        {
            std::copy(line->data, line->data + m_lineSize, data);

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
Result ZLCDMA::Cache::OnWriteRequest(const Request& req)
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
        MemAddr tag;

        // We don't have the line; allocate a line
        line = GetReplacementLine(req.address, tag);
        if (line == NULL)
        {
            ++m_numConflicts;
            DeadlockWrite("Unable to allocate line for bus write request");
            return FAILED;
        }

        if (line->valid)
        {
            // Line is already in use; evict it
            if (!EvictLine(line, req))
            {
                ++m_numConflicts;
                DeadlockWrite("Unable to evict line for bus write request");
                return FAILED;
            }

            COMMIT { ++m_numConflicts; ++m_numResolved; }
            return DELAYED;
        }

        // Reset the line
        COMMIT
        {
            line->tag           = tag;
            line->valid         = true;
            line->dirty         = false;
            line->tokens        = 0;
            line->transient     = false;
            line->priority      = false;
            line->pending_read  = false;
            line->pending_write = false;
            std::fill(line->bitmask, line->bitmask + m_lineSize, false);
        }

        newline = true;
    }

    // Update line; write data
    COMMIT
    {
        line->time = GetCycleNo();
        line->dirty = true;

        line::blit(line->data, req.mdata.data, req.mdata.mask, m_lineSize);
        line::setif(line->bitmask, true, req.mdata.mask, m_lineSize);
    }

    if (!newline && !line->transient && line->tokens == m_parent.GetTotalTokens())
    {
        assert(line->priority);

        // This line has all the tokens.
        // We can acknowledge directly after writing.
        TraceWrite(req.address, "Processing Bus Write Request: Exclusive Hit");

        if (!m_clients[req.client]->OnMemoryWriteCompleted(req.wid))
        {
            return FAILED;
        }

        return SUCCESS;
    }

    // Save acknowledgement. When we get all tokens later, these will get acknowledged.
    COMMIT{ line->ack_queue.push_back(WriteAck(req.client, req.wid)); }

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
        DeadlockWrite("Unable to buffer write request for next node");
        return FAILED;
    }
    return SUCCESS;
}

// Network remote request to acquire all tokens and data.
// Issued by writes.
Result ZLCDMA::Cache::OnAcquireTokensRem(Message* req)
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
Result ZLCDMA::Cache::OnAcquireTokensRet(Message* req)
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

    if (tokens < m_parent.GetTotalTokens())
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
Result ZLCDMA::Cache::OnReadRem(Message* req)
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
Result ZLCDMA::Cache::OnReadRet(Message* req)
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
        char data[m_lineSize];

        COMMIT
        {
            line->pending_read = false;

            std::copy(line->data, line->data + m_lineSize, data);
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
Result ZLCDMA::Cache::OnEviction(Message* req)
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
        MemAddr tag;
        line = GetEmptyLine(req->address, tag);
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
            line->tag           = tag;
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
            line::blitnot(line->data, req->data, line->bitmask, m_lineSize);
            line::blitnot(line->bitmask, req->bitmask, line->bitmask, m_lineSize);

            line->tokens += req->tokens;
            line->priority = line->priority || req->priority;
            line->dirty = line->dirty || req->dirty;
            delete req;
        }
    }
    return SUCCESS;
}

bool ZLCDMA::Cache::AcknowledgeQueuedWrites(Line* line)
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

bool ZLCDMA::Cache::OnReadCompleted(MemAddr addr, const char* data)
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

Result ZLCDMA::Cache::DoRequests()
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

Result ZLCDMA::Cache::DoReceive()
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

ZLCDMA::Cache::Cache(const std::string& name, ZLCDMA& parent, Clock& clock, CacheID id, Config& config) :
    Simulator::Object(name, parent),
    Node(name, parent, clock),
    m_selector (parent.GetBankSelector()),
    m_lineSize (config.getValue<size_t>("CacheLineSize")),
    m_assoc    (config.getValue<size_t>(parent, "L2CacheAssociativity")),
    m_sets     (m_selector.GetNumBanks()),
    m_inject   (config.getValue<bool>(parent, "EnableCacheInjection")),
    m_id       (id),
    m_clients  (),
    m_storages (),
    p_lines    (*this, clock, "p_lines"),
    m_lines    (m_assoc * m_sets),
    m_numHits  (0),
    m_numMisses(0),
    m_numConflicts(0),
    m_numResolved(0),
    p_Requests (*this, "requests", delegate::create<Cache, &Cache::DoRequests>(*this)),
    p_In       (*this, "incoming", delegate::create<Cache, &Cache::DoReceive>(*this)),
    p_bus      (*this, clock, "p_bus"),
    m_requests ("b_requests", *this, clock, config.getValue<BufferSize>(*this, "RequestBufferSize")),
    m_responses("b_responses", *this, clock, config.getValue<BufferSize>(*this, "ResponseBufferSize"))
{
    RegisterSampleVariableInObject(m_numHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numResolved, SVC_CUMULATIVE);

    // Create the cache lines
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        m_lines[i].valid = false;
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

void ZLCDMA::Cache::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*args*/) const
{
    out <<
    "The L2 Cache in a CDMA system is connected to the processors with a bus and to\n"
    "the rest of the CDMA system via a ring network.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Reads and displays the cache-lines, and global information such as hit-rate\n"
    "  and cache configuration.\n"
    "- inspect <component> buffers\n"
    "  Reads and displays the buffers in the cache\n";
}

void ZLCDMA::Cache::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
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

    if (m_numHits + m_numMisses == 0)
        out << "No accesses so far, cannot compute hit/miss/conflict rates." << endl;
    else
    {
        float factor = 100.0f / (m_numHits + m_numMisses);

        out << "Current hit rate:    " << setprecision(2) << fixed << m_numHits * factor
            << "% (" << dec << m_numHits << " hits, " << m_numMisses << " misses)" << endl
            << "Current soft conflict rate: "
            << setprecision(2) << fixed << m_numResolved * factor
            << "% (" << dec << m_numResolved << " non-stalling conflicts)" << endl
            << "Current hard conflict rate: "
            << setprecision(2) << fixed << m_numConflicts * factor
            << "% (" << dec << m_numConflicts << " stalling conflicts)"
            << endl;
    }
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
                << hex << "0x" << setw(16) << setfill('0') << m_selector.Unmap(line.tag, set) * m_lineSize
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
