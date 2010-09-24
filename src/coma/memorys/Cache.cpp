#include "Cache.h"
#include "mergestorebuffer.h"
#include "../../config.h"
#include "../../sampling.h"
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
        if (!line.pending)
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
    return (linelrue != NULL) ? linelrue : linelruw;
}

bool ZLCOMA::Cache::OnMessageReceived(Message* msg)
{
    switch (msg->type)
    {
    case Message::ACQUIRE_TOKEN:
		return (msg->source == m_id)
			? OnAcquireTokenRet(msg)
			: OnAcquireTokenRem(msg);

    case Message::ACQUIRE_TOKEN_DATA:
		return (msg->source == m_id)
			? OnAcquireTokenDataRet(msg)
			: OnAcquireTokenDataRem(msg);

    case Message::DISSEMINATE_TOKEN_DATA:
        return OnDisseminateTokenData(msg);

    case Message::LOCALDIR_NOTIFICATION:
        // Request is meant for directory, we shouldn't receive it again
        assert(msg->source != m_id);
    
        // Just pass it on
        if (!SendMessage(msg, MINSPACE_FORWARD))
        {
            return false;
        }
        return true;

    default:
        assert(false);
        break;
    }
    return false;
}

// Disseminate a number of tokens
bool ZLCOMA::Cache::EvictLine(Line* line)
{
    assert(line->valid);
    
    size_t  set     = (line - &m_lines[0]) / m_assoc;
    MemAddr address = (line->tag * m_sets + set) * m_lineSize;
     
    Message* reqdt = new Message();
    reqdt->processed      = false;
    reqdt->transient      = false;
    reqdt->merged         = false;
    reqdt->msbcopy        = NULL;
    reqdt->type           = Message::DISSEMINATE_TOKEN_DATA;
    reqdt->address        = address;
    reqdt->data.size      = m_lineSize;
    reqdt->source         = m_id;    
    reqdt->dataavailable  = true;
    reqdt->priority       = line->priority;
    reqdt->tokenrequested = (line->dirty ? m_numTokens : 0);
    reqdt->tokenacquired  = line->tokencount;
    std::copy(line->data,     line->data     + m_lineSize, reqdt->data.data);
    std::fill(reqdt->bitmask, reqdt->bitmask + m_lineSize, true);
    
    if (!SendMessage(reqdt, MINSPACE_FORWARD))
    {
        return false;
    }

    line->valid = false;
    return true;
}

// Local Read from a memory client on the bus
Result ZLCOMA::Cache::OnReadRequest(const Request& req)
{
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
            if (!EvictLine(line))
            {
                return FAILED;
            }
            return DELAYED;
        }

        // Reset the cache-line
        line->tag         = (req.address / m_lineSize) / m_sets;
        line->time        = GetCycleNo();
        line->valid       = true;
        line->dirty       = false;
        line->tokencount  = 0;
        line->pending     = true;
        line->invalidated = false;
        line->priority    = false;
        line->tlock       = false;
        std::fill(line->bitmask, line->bitmask + m_lineSize, false);

        // Send request for data and a token
        Message* msg = new Message();
        msg->processed      = false;
        msg->tokenacquired  = 0;
        msg->priority       = false;
        msg->transient      = false;
        msg->merged         = false;
        msg->msbcopy        = NULL;
        msg->type           = Message::ACQUIRE_TOKEN_DATA;
        msg->address        = req.address;
        msg->data.size      = req.size;
        msg->source         = m_id;
        msg->tokenrequested = 1;
        msg->dataavailable  = false;
        std::fill(msg->bitmask, msg->bitmask + m_lineSize, false);

        if (!SendMessage(msg, MINSPACE_FORWARD))
        {
            return FAILED;
        }
        return SUCCESS;
    }

    // check whether the line is already locked
    if (m_msbModule->IsSlotLocked(req.address))
    {
        // Stall
        return FAILED;
    }

    MemData data;
    data.size = req.size;
    
    if (line->tokencount > 0 && line->IsLineAtCompleteState())
    {
        // The line has valid data
        line->time = GetCycleNo();
         
        // Send data to clients
        std::copy(line->data, line->data + m_lineSize, data.data);

        // Merge with made writes, if any
        m_msbModule->LoadBuffer(req.address, data, *line);
    }
    else
    {
        // The data is not available in the cache-line
        assert(line->pending);
        
        // See if we can grab the data from the MSB instead
        if (!m_msbModule->LoadBuffer(req.address, data, *line))
        {
            // No, stall
            return FAILED;
        }
    }
    
    // Return reply to memory clients
    if (!OnReadCompleted(req.address, data))
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
        return FAILED;
    }

    // Note that writes may not be of entire cache-lines
    unsigned int offset = req.address % m_lineSize;
    
    bool wantdata = false;

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
            if (!EvictLine(line))
            {
                return FAILED;
            }
            return DELAYED;
        }
        
        // Initialize cacheline 
        line->tag         = (req.address / m_lineSize) / m_sets;
        line->valid       = true;
        line->tokencount  = 0;
        line->invalidated = false;
        line->priority    = false;
        line->tlock       = false;
        std::fill(line->bitmask, line->bitmask + MAX_MEMORY_OPERATION_SIZE, false);
        
        // To indicate that it was a miss and we want the data too
        wantdata = true;
    }
    // We have the line
    else if (!line->pending)
    {
        // This line has no pending requests
        assert(line->tokencount > 0);
        assert(!line->invalidated);
        assert(!line->tlock);

        std::fill(line->bitmask, line->bitmask + MAX_MEMORY_OPERATION_SIZE, false);
    }
    // Line with pending requests
    else if (m_msbModule->IsSlotLocked(req.address))
    {
        // MSB is locked; stall
        return FAILED;
    }
    // X-Token MSB implementation
    else if (!line->dirty && line->priority)
    {
        // Read pending with priority token
        assert(line->tokencount > 0);
        assert(!line->tlock);
        assert(!line->invalidated);
    }
    else
    {
        // Try to write to the buffer
        if (!m_msbModule->WriteBuffer(req.address, req, WriteAck(req.client, req.tid) ))
        {
            return FAILED;
        }
        return SUCCESS;
    }
        
    // Update line; write data
    line->time = GetCycleNo();
    line->dirty = true;
    memcpy(line->data + offset, req.data, req.size);
    std::fill(line->bitmask + offset, line->bitmask + offset + req.size, true);
    
    if (!line->pending && line->tokencount == m_numTokens)
    {
        // This line has all the tokens and no pending requests.
        // We can acknowledge directly after writing.
        if (!m_clients[req.client]->OnMemoryWriteCompleted(req.tid))
        {
            return FAILED;
        }
        return SUCCESS;
    }
    
    // Send request to acquire all tokens (and data in case of a miss)
    Message* msg = new Message();
    msg->processed     = false;
    msg->tokenacquired  = 0;
    msg->priority      = false;
    msg->transient     = false;
    msg->merged        = false;
    msg->msbcopy        = NULL;
    msg->type           = (wantdata) ? Message::ACQUIRE_TOKEN_DATA : Message::ACQUIRE_TOKEN;
    msg->address        = req.address;
    msg->data.size      = req.size;
    msg->client         = req.client;
    msg->tid            = req.tid;
    msg->source         = m_id;
    msg->tokenrequested = m_numTokens;
    msg->dataavailable  = false;
    memcpy(&msg->data.data + offset, req.data, req.size);
    std::fill(msg->bitmask, msg->bitmask + MAX_MEMORY_OPERATION_SIZE, false);
    std::fill(msg->bitmask + offset, msg->bitmask + offset + req.size, true);

    if (!line->pending && line->priority)
    {
        // Line has the priority token, give it to the request
        msg->priority      = line->priority;
        msg->tokenacquired += 1;

        line->tokencount = line->tokencount - 1;
        line->priority   = false;
    }
    
    // We have a message in transit now
    line->pending = true;
    
    if (!SendMessage(msg, MINSPACE_FORWARD))
    {
        return FAILED;
    }
    return SUCCESS;
}

// Network remote request to acquire all tokens - invalidates/IV
bool ZLCOMA::Cache::OnAcquireTokenRem(Message* req)
{
	Line* line = FindLine(req->address);
    if (line == NULL)
    {
        // We don't have the line, forward message
        if (!SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
		return true;
    }

    // This is the very first plan, by providing tokens as much as requested, if possible
    if (!line->pending) // non-pending states   // S, E, O, M
    {
        // data is already available but needs to give token to the request
        assert(line->tokencount > 0);
        assert(line->tlock == false);
        assert(req->tokenrequested > 0);
        assert(req->transient == false);

        // will need to clean up line
        assert(line->gettokenglobalvisible() <= req->tokenrequested - req->tokenacquired);

        // Give line's tokens to the request
        req->tokenacquired += line->gettokenglobalvisible();
        req->dataavailable  = true;
        req->priority      = req->priority || line->priority;

        // check the whole line with mask bits and update the request according to the cacheline
        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            if (!req->bitmask[i])
            {
                req->data.data[i] = line->data[i];
            }
        }

        // Clear line
        line->valid = false;
    }
    // pending request      // R, T, P, M, U
    else if (!line->dirty)  // reading, before  // R, T
    {
        // Get tokens from the line, and invalidate it.
        // If the line already has the priority token then take the priority token as well.

        // Make sure that if a request arrives with transient tokens, there are no tokens in the line.
        if (req->transient)
            assert(line->tokencount == 0);

        // Transfer line tokens to request
        req->tokenacquired += line->gettokenglobalvisible();
        req->priority = req->priority || line->priority;

        // check the whole line with mask bits and update the request according to the cacheline
        req->dataavailable = true;
        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            if (line->bitmask[i])
            {
                req->data.data[i] = line->data[i];
            }
        }

        line->time        = GetCycleNo();
        line->tokencount  = 0;
        line->priority    = false;        
        line->invalidated = true;
        line->tlock       = false;

        for (unsigned int i = 0; i < m_lineSize; ++i)
        {
            if (req->bitmask[i])
            {
                line->bitmask[i] = true;
                line->data[i] = req->data.data[i];
            }
        }
    }
    else    // writing, before      // P, M, U
    {
        int newtokenline;
        
        if (req->priority)
        {
            // Request has priority token and will get all the tokens
            assert(req->transient == false);
            assert(line->priority == false); 

            if (line->tlock)
            {
                assert(line->invalidated);
                
                // Locked tokens are unlocked and released to the request
                req->tokenacquired += line->tokencount;
                newtokenline = 0;
                line->tlock = false;
            }
            else if (line->invalidated)
            {
                newtokenline = line->tokencount;
            }
            else
            {
                req->tokenacquired += line->gettokenglobalvisible();
                newtokenline = 0;
            }

            req->dataavailable = true;

            line->invalidated = true;

            // check the whole line with mask bits and update the request according to the cacheline
            for (unsigned int i = 0; i < m_lineSize; i++)
            {
                if (line->bitmask[i] || line->tokencount != 0)
                {
                    req->data.data[i] = line->data[i];
                }
            }
        }
        else if (line->priority)
        {
            // Line has priority token and will get all the tokens
            assert(line->invalidated == false);
            assert(line->tlock == false);
            assert(req->priority == false);

            // transient tokens will be changed to permanent tokens
            req->transient = false;

            newtokenline = line->tokencount + req->tokenacquired;

            // update request, rip the available token off the request
            req->tokenacquired = 0;
            req->dataavailable = true;

            // check the whole line with mask bits and update the request according to the cacheline
            for (unsigned int i = 0; i < m_lineSize; i++)
            {
                if (line->bitmask[i] || line->tokencount != 0)
                if (!req->bitmask[i])
                {
                    req->data.data[i] = line->data[i];
                }
            }
        }
        else
        {
            // Both the request and the line will get the same number of tokens, but the request
            // will have its tokens transient and line will have the tokens locked.
            // Transient tokens can be transformed into permanent tokens by priority tokens
            // in the line. Locked tokens can be unlocked by priority tokens.
            newtokenline = req->tokenacquired + line->tokencount;

            req->tokenacquired = req->tokenacquired + line->tokencount;
            req->dataavailable = true;
            req->transient = true;

            line->invalidated = true;
            line->tlock = true;

            // check the whole line with mask bits and update the request according to the cacheline
            for (unsigned int i = 0; i < m_lineSize; i++)
            {
                if (line->bitmask[i] || line->tokencount != 0)
                if (!req->bitmask[i])
                {
                    req->data.data[i] = line->data[i];
                }
            }
        }

        bool bupdatealldata = (line->tokencount == 0 && newtokenline > 0);

        line->time = GetCycleNo();
        line->tokencount = newtokenline;

        // update the cacheline with the dirty data in the request
        // start from the offset and update the size from the offset
        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            // if not all the data needs to be updated. then skip the unnecessary parts, only update if required
            if (!req->bitmask[i])
            {
                if (!line->bitmask[i] && bupdatealldata)
                {
                    line->data[i] = req->data.data[i];
                }
            }
            // if the mask is already on there in the cache line then dont update
            // otherwise update
            else if (!line->bitmask[i] || bupdatealldata)
            {
                line->data[i] = req->data.data[i];
                line->bitmask[i] = true;
            }
            //// we dont care about the cacheline bit mask, we just write the updated data.
        }
    }

    // Forward message
    if (!SendMessage(req, MINSPACE_FORWARD))
    {
        return false;
    }
    return true;
}

// network return request to acquire token - IV return or DE
bool ZLCOMA::Cache::OnAcquireTokenRet(Message* req)
{
    Line* line = FindLine(req->address);
    assert(line != NULL);
    assert(line->pending);
    assert(line->dirty);

    // check whether the line is already invalidated or not
    // or say, false sharing or races situation
    if (line->invalidated)
    {
        assert(!line->priority);
        
        if (!req->priority)
        {
            // We did not got the priority token, clear the locked tokens
            line->tokencount = 0;
        }

        line->tokencount += req->gettokenpermanent();
        line->time = GetCycleNo();
        line->pending = false;
        line->invalidated = false;
        line->priority = true;
        line->tlock = false;
        
        if (line->tokencount == 0) {
            // We have no tokens left; clear line
            line->valid = false;
        } else {
            // Otherwise, we should have all tokens now
            assert(line->tokencount == m_numTokens);
        }
    }
    else
    {
        // the request can have transient request, 
        // in case during the false-sharing the current line has the priority token
        if (req->transient)
        {
            assert(line->priority);

            // transfer the transient tokens
            req->transient = false;

            if (req->tokenacquired > 0)
            {
                Message *reqnotify = new Message();
                reqnotify->type          = Message::LOCALDIR_NOTIFICATION;
                reqnotify->address       = req->address;
                reqnotify->source        = m_id;
                reqnotify->tokenacquired = req->tokenacquired;

                if (!SendMessage(reqnotify, MINSPACE_FORWARD))
                {
                    return false;
                }
            }
        }
        // resolve evicted lines short of data problem in directory configuration
        // need to resend the request again
        // REVISIT JXXX, maybe better solutions
        // JOYING or maybe just delay a little bit
        else if (line->tokencount + req->tokenacquired < m_numTokens)
        {
            req->processed = true;

            // just send it again
            if (!SendMessage(req, MINSPACE_FORWARD))
            {
                return false;
            }
            return true;
        }

        req->processed = false;

        assert(line->tokencount + req->tokenacquired == m_numTokens);
        assert(line->tlock == false);

        line->time = GetCycleNo();
        line->tokencount += req->tokenacquired;
        line->pending = false;
        line->invalidated = false;
        line->priority = true;
    }

    // Acknowledge the write
    if (!m_clients[req->client]->OnMemoryWriteCompleted(req->tid))
    {
        return false;
    }

    OnPostAcquirePriorityToken(line, req->address);
    return true;
}

// network remote request to acquire token and data     // RE, RS, SR, ER
bool ZLCOMA::Cache::OnAcquireTokenDataRem(Message* req)
{
    assert(req->tokenrequested <= m_numTokens);

	Line* line = FindLine(req->address);
    if (line == NULL)
    {
        // We do not have the line, forward message
        if (!SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
		return true;
    }

    // this is the very first plan, by providing tokens as much as requested, if possible
    if (!line->pending) // non-pending states   // S, E, O, M
    {
        // data is already available but needs to give token to the request
        assert(line->tokencount > 0);
        assert(req->tokenrequested > 0);
        assert(line->tlock == false);
        assert(line->invalidated == false);

        if (line->gettokenglobalvisible() <= (req->tokenrequested - req->tokenacquired))
        {
            // line tokens are not enough; will need to clean up line
            if (!req->transient)
            {
                // if the request is read which requires only one token and the line has only one token, 
                // this may work only when total token number == cache number
                if (req->tokenrequested == 1 && req->tokenacquired == 0)
                {
                    req->dataavailable = true;
                    for (unsigned int i = 0; i < m_lineSize; i++)
                    {
                        req->data.data[i] = line->data[i];
                        req->bitmask[i] = true;
                    }
                    // JOYING distinguish about modified data and normal data
                }
                else
                {
                    // Update request
                    req->tokenacquired += line->gettokenglobalvisible();
                    req->dataavailable = true;
                    req->priority = req->priority || line->priority;

                    for (unsigned int i = 0; i < m_lineSize; i++)
                    {
                        if (!req->bitmask[i])
                        {
                            req->data.data[i] = line->data[i];

                            // update read request bitmaks, write request bitmask will not be updated.
                            if (req->tokenrequested != m_numTokens)
                            {
                                req->bitmask[i] = true;
                            }
                        }
                    }

                    // Clear line
                    line->valid = false;
                }
            }
            else
            {
                assert(line->priority == true);

                // update request
                req->tokenacquired += line->gettokenglobalvisible();
                req->dataavailable = true;
                req->transient = false;
                req->priority = req->priority || line->priority;

                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i])
                    {
                        req->data.data[i] = line->data[i];

                        // update read request bitmaks, write request bitmask will not be updated.
                        if (req->tokenrequested != m_numTokens)
                        {
                            req->bitmask[i] = true;
                        }
                    }
                }

                // Clear line
                line->valid = false;
            }
        }
        else    // only give out some tokens 
        {
            assert(req->transient == false);

            unsigned int newlinetoken = line->gettokenglobalvisible() - req->tokenrequested + req->tokenacquired;

            // update request
            req->tokenacquired = req->tokenrequested;
            req->dataavailable = true;

            for (unsigned int i = 0; i < m_lineSize; i++)
            {
                if (line->bitmask[i])
                {
                    req->data.data[i] = line->data[i];
    
                    // update read request bitmaks, write request bitmask will not be updated.
                    if (req->tokenrequested != m_numTokens)
                    {
                        req->bitmask[i] = true;
                    }
                }
            }

            // update line  ??
            // check the update request and line data about the consistency !!!! XXXX JXXX !!!???
            line->time = GetCycleNo();
            line->tokencount = newlinetoken;
        }

        // save the current request
        if (!SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    // pending request      // R, T, P, W, U
    else if (req->tokenrequested < m_numTokens)
    {
        // read  // RS, SR
        assert(req->transient == false);

        if (!line->dirty)  // reading, before  // R, T
        {
            assert(line->tlock == false);
            
            if (line->invalidated)  // T 
            {
                assert(line->priority == false);
                
                // the line has only ghost token for local use not anytoken can be acquired or invalidated
                // get the data if available, and token if possible. otherwise go on
                // CHKS: ALERT: some policy needs to be made to accelerate the process                 

                // update request
                req->dataavailable = true;

                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i])
                    {
                        req->data.data[i] = line->data[i];
                        req->bitmask[i] = true;
                    }
                    else
                    {
                        line->data[i] = req->data.data[i];
                        line->bitmask[i] = true;
                    }
                }

                // update line  ??? no update?
                line->time = GetCycleNo();
            }
            else    // R
            {
                // get the data if available, and token if possible. otherwise go on
                assert(req->tokenrequested > 0);

                if (req->tokenacquired > req->tokenrequested && line->tokencount == 0)
                {
                    // Request has more than enough tokens, give one to the line
                    req->tokenacquired--;
                    line->tokencount++;
                }
                else if (req->tokenacquired < req->tokenrequested && line->tokencount > 1)
                {
                    // Line has more than enough tokens, give one to the request
                    req->tokenacquired++;
                    line->tokencount--;
                }

                if (!contains(line->bitmask, line->bitmask + m_lineSize, false))
                {
                    req->dataavailable = true;

                    for (unsigned int i = 0; i < m_lineSize; i++)
                    {
                        req->data.data[i] = line->data[i];
                        req->bitmask[i] = true;
                    }
                }
                else if (req->dataavailable)
                {
                    for (unsigned int i = 0; i < m_lineSize; ++i)
                    {
                        if (!line->bitmask[i])
                        {
                            line->data[i] = req->data.data[i];
                            line->bitmask[i] = true;
                        }
                    }
                }

                line->time = GetCycleNo();
            }
        }
        else    // writing, before, // P, W, U
        {
            unsigned int newtokenline;

            bool oldpriority = line->priority;

            if (line->invalidated)
            {
                assert(line->priority == false);

                // the request might have the priority, 
                // in this case all locked tokens in the line will be unlocked
                // the line should be un-invalidated and get all the tokens

                if (req->priority)
                {
                    // mkae the req transfer the priority token to the line
                    // get rid of the invalidated flag
                    // no lines are locked
                    newtokenline = req->tokenacquired;
                    if (line->tlock)
                    {
                        newtokenline += line->tokencount;
                    }

                    line->invalidated = false;
                    line->tlock = false;
                }
                else
                {
                    // there willl be nothing to lose in this case
                    assert(req->tokenacquired == 0);
                    newtokenline = line->tokencount;
                }
            }
            else
            {
                // the line will get all the tokens anyway
                newtokenline = line->tokencount + req->tokenacquired;
            }
            
            line->priority = line->priority || req->priority;
            req->priority = false;

            bool acquiredp = (!oldpriority && line->priority);

            // get the data if available, and no token will be granted.
            // update request, get more token or get own tokens ripped off
            req->tokenacquired = 0;

            // if nothing to be updated
            if (line->tokencount != 0)
            {
                req->dataavailable = true;

                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i])
                    {
                        req->data.data[i] = line->data[i];
                        req->bitmask[i] = true;
                    }
                }
            }

            // update line  ??? no update?
            if (line->tlock)
                assert(newtokenline == line->tokencount);

            line->tokencount = newtokenline;

            if (req->dataavailable && line->tokencount != 0)
            {
                for (unsigned int i = 0; i < m_lineSize; ++i)
                {
                    if (!line->bitmask[i])
                    {
                        line->data[i] = req->data.data[i];
                        line->bitmask[i] = true;
                    }
                }
            }

            if (acquiredp)
                OnPostAcquirePriorityToken(line, req->address);
                
            line->time = GetCycleNo();
        }

        // save the current request
        if (!SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    else    // write        // RE, ER
    {
        unsigned int newtokenline=0;
        unsigned int newtokenreq = 0;

        // the line must have less token than required, since request require all the thokens
        assert(line->tokencount <= req->tokenrequested - req->tokenacquired);

        if (!line->dirty)  // reading, before  // R, T
        {
            assert(!line->tlock);

            // get tokens if any. set invalidated flag
            if (req->transient)
            {
                // in this case the line must be already invalidated, 
                // so the tokens can be acquired here is absolutely 0
                assert(line->invalidated);
            }

            // the gettokenpermanent in the case above will return 0
            newtokenreq = req->gettokenpermanent() + line->tokencount;
            newtokenline = 0;
            //newtokenline = line->tokencount;
            line->invalidated = true;

            // update request
            req->tokenacquired = newtokenreq;
            req->dataavailable = true;
            req->priority = req->priority || line->priority;

            if (line->IsLineAtCompleteState())
            {
                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i])
                    {
                        req->data.data[i] = line->data[i];
                    }
                }
            }

            // update line
            line->time = GetCycleNo();
            line->tokencount = newtokenline;
            line->invalidated = true;
            line->priority = false;

            for (unsigned int i = 0 ; i < m_lineSize; ++i)
            {
                if (req->tokenacquired > 0 || req->bitmask[i])
                {
                    line->bitmask[i] = true;
                    line->data[i] = req->data.data[i];
                }
            }
        }
        else    // writing, before      // P, W, U
        {
            bool bupdatealldata;
            bool blinecomplete = (line->tokencount != 0);
            
            line->time = GetCycleNo();

            // 1. req has priority token
            // req will get all the tokens
            if (req->priority)
            {
                bupdatealldata = false;
                
                assert(req->transient == false);
                assert(line->priority == false);

                if (line->tlock || !line->invalidated)
                {
                    // Tokens are transfered to request and unlocked, if necessary
                    req->tokenacquired += line->tokencount;
                    line->tlock = false;
                }

                req->dataavailable = true;

                line->tokencount = 0;
                line->invalidated = true;
            }
            // 2. line has the priority, then the line will take all 
            else if (line->priority)
            {
                bupdatealldata = (line->tokencount == 0 && req->tokenacquired > 0);

                assert(line->invalidated == false);
                assert(line->tlock == false);

                req->tokenacquired = 0;
                req->dataavailable = true;
                req->transient = false;

                line->tokencount += req->tokenacquired;
            }
            else
            {
                bupdatealldata = (line->tokencount == 0 && req->tokenacquired > 0);

                // both will get the same number of tokens, req will be at transient situation
                // and line will have the tokens locked.
                // All of them are only visible locally, cannot be transfered.
                // Transient tokens can later be transformed into permanent tokens by priority tokens in the line
                // locked tokesn can be unlocked by priority tokens.
                // Permanent tokens can later by transfered or used remotely.
                req->tokenacquired = req->tokenacquired + line->tokencount;
                req->dataavailable = true;
                req->transient = true;

                line->tokencount = req->tokenacquired;
                line->invalidated = true;
                line->tlock = true;
            }
            
            // check the whole line with mask bits and update the request according to the cacheline
            for (unsigned int i = 0; i < m_lineSize; i++)
            {
                if (line->bitmask[i] || blinecomplete)
                if (!req->bitmask[i])
                {
                    req->data.data[i] = line->data[i];
                }
            }
            
            // update the cacheline with the dirty data in the request
            // start from the offset and update the size from the offset
            for (unsigned int i = 0; i < m_lineSize; i++)
            {
                // if not all the data needs to be updated. then skip the unnecessary parts, only update if required
                if (!req->bitmask[i])
                {
                    if (!line->bitmask[i] && bupdatealldata)
                    {
                        line->data[i] = req->data.data[i];
                    }
                }
                // if the mask is already on there in the cache line then dont update
                // otherwise update
                else if (!line->bitmask[i] || bupdatealldata)
                {
                    line->data[i] = req->data.data[i];
                    line->bitmask[i] = true;
                }
                //// we dont care about the cacheline bit mask, we just write the updated data.
            }
        }

        // save the current request
        if (!SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    return true;
}

// network request return, with token and data  // RS, SR, RE, ER
bool ZLCOMA::Cache::OnAcquireTokenDataRet(Message* req)
{
    Line* line = FindLine(req->address);
    assert(line != NULL);
    assert(line->pending);     // non-pending states   // S, E, O, M

    // pending states       // R, T, P, U, W
    if (req->tokenrequested < m_numTokens)   // read, // RS, SR
    {
        if (!line->dirty)  // reading, before  // R, T
        {
            // resolve evicted lines short of data problem in directory configuration
            // need to resend the request again
            // REVISIT JXXX, maybe better solutions
            if (!req->dataavailable && !contains(line->bitmask, line->bitmask + m_lineSize, false))
            {
                req->processed = true;

                // just send it again
                if (!SendMessage(req, MINSPACE_FORWARD))
                {
                    return false;
                }
                return true;
            }

            assert(req->transient == false);

            req->processed = false;

            unsigned int tokenlinenew = line->tokencount + req->tokenacquired;
            bool newlinepriority = line->priority || req->priority;
            unsigned int evictthereturnedtokens = (line->invalidated) ? req->tokenacquired : 0;

            // instead of updating the cache line, the request should be updated first
            // update request from the line
            if (!contains(line->bitmask, line->bitmask + m_lineSize, false))
            {
                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i])
                    {
                        req->data.data[i] = line->data[i];
                    }
                }
            }

            // update time and state
            if (line->invalidated || tokenlinenew == 0)
            {
                line->valid = false;
                // TOFIX JXXX backward invalidate L1 caches.

                // in multi-level configuration, there's a chance that an AD request could travel twice(first time reload), and return at invalidated state with non-transient tokens. 
                // in this case, the non-transient tokens should be dispatched again with disseminate request
                if (evictthereturnedtokens > 0)
                {
                    Message *reqevresend = new Message();
                    
                    reqevresend->ignore         = req->ignore;
                    reqevresend->processed     = req->processed;
                    reqevresend->merged        = req->merged;
                    reqevresend->tokenacquired  = req->tokenacquired;
                    reqevresend->tokenrequested = req->tokenrequested;
                    reqevresend->msbcopy        = NULL;
                    reqevresend->priority      = false;
                    reqevresend->transient     = false;
                    reqevresend->type           = Message::DISSEMINATE_TOKEN_DATA;
                    reqevresend->address        = req->address;
                    reqevresend->data.size      = m_lineSize;
                    reqevresend->dataavailable  = true;
                    reqevresend->source         = m_id;
                    std::fill(reqevresend->bitmask, reqevresend->bitmask + m_lineSize, true);
                    memcpy(reqevresend->data.data, req->data.data, m_lineSize);

                    reqevresend->tokenacquired = evictthereturnedtokens;
                    if (evictthereturnedtokens == m_numTokens)
                    {
                        reqevresend->tokenrequested = m_numTokens;
                        reqevresend->priority = true;
                    }
                    else
                    {
                        reqevresend->tokenrequested = 0;
                        reqevresend->priority = false;
                    }

                    if (!SendMessage(reqevresend, MINSPACE_FORWARD))
                    {
                        return false;
                    }
                }
            }
            else
            {
                if (line->tokencount == 0 || contains(line->bitmask, line->bitmask + m_lineSize, false))
                {
                    assert(!line->breserved);
                    for (unsigned int i = 0 ; i < m_lineSize; ++i)
                    {
                        if (req->bitmask[i])
                        {
                            line->bitmask[i] = true;
                            line->data[i] = req->data.data[i];
                        }
                    }
                }
                else if (line->breserved)
                {
                    line->dirty = true;
                }
                
                line->time = GetCycleNo();
                line->tokencount = tokenlinenew;
                line->pending = false;
                line->invalidated = false;
                line->priority = newlinepriority;
                line->tlock = false;
            }

            line->breserved = false;

            OnPostAcquirePriorityToken(line, req->address);

            if (!OnReadCompleted(req->address, req->data))
            {
                return false;
            }
        }
        else    // writing, before  // P, U, W
        {
            // just collect the tokens, 
            // it must be because an EV request dispatched a owner-ev to the R,T line
            assert(line->priority);
            assert(req->transient == false);
            assert(!req->priority);

            unsigned int tokenlinenew = line->tokencount + req->tokenacquired;
            bool newlinepriority = line->priority || req->priority;


            line->tag = (req->address / m_lineSize) / m_sets;
            line->time = GetCycleNo();
            line->tokencount = tokenlinenew;
            line->priority = newlinepriority;
            line->tlock = false;
                                                    
            // instead of updating the cache line, the request should be updated first
            // update request from the line
            if (line->tokencount != 0)
            {
                // check the whole line with mask bits and update the request according to the cacheline
                for (unsigned int i = 0; i < m_lineSize; i++)
                {
                    if (line->bitmask[i])
                    {
                        req->data.data[i] = line->data[i];
                    }
                }
            }

            line->breserved = false;

            // save reply request
            if (!OnReadCompleted(req->address, req->data))
            {
                return false;
            }
        }
    }
    // write, // RE, ER, (or maybe RS, SR when m_numTokens == 1)
    else if (!line->dirty)  // actually reading, before  // R, T
    {
        assert(m_numTokens == 1);
        // line is shared but also exclusive (not dirty not owner)
           
        assert(!line->invalidated); 
        assert(line->tlock == false);
        assert(req->transient == false);

        // update time and state
        assert(line->tokencount == 0);
        line->time = GetCycleNo();
        line->tokencount += req->tokenacquired;
        line->pending = false;
        line->invalidated = false;
        line->priority = true;
        line->tlock = false;

        for (unsigned int i = 0 ; i < m_lineSize; ++i)
        {
            if (req->tokenacquired > 0 || req->bitmask[i])
            {
                line->bitmask[i] = true;
                line->data[i] = req->data.data[i];
            }
        }

        // instead of updating the cache line, the reqeust should be updated first
        // update request from the line
        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            if (line->bitmask[i])
            {
                req->data.data[i] = line->data[i];
            }
        }

        // save reply request
        if (!OnReadCompleted(req->address, req->data))
        {
            return false;
        }
        OnPostAcquirePriorityToken(line, req->address);
    }
    else    // writing, before  // P, U, W
    {
        unsigned int newtokenline = 0;
        unsigned int newtokenreq  = 0;
        unsigned int tokennotify  = (req->transient) ? req->tokenacquired : 0;

         // check whether the line is already invalidated or not
        // or say, false sharing or races situation
        if (line->invalidated) // U state
        {
            assert(!line->priority);
            if (req->priority)
            {
                // all locked tokens are unclocked
                assert(!req->transient);

                line->tlock = false;
                line->invalidated = false;
            }

            newtokenline = line->gettokenglobalvisible() + req->gettokenpermanent();    // this can be zero
            newtokenreq = 0;

            line->time = GetCycleNo();
            
            // continue_here
            if (newtokenline == 0)
            {
                // Clear line
                line->valid = false;
            }
            else
            {
                assert(newtokenline == m_numTokens);
                
                if (line->tokencount == 0)
                {
                    for (unsigned int i = 0 ; i < m_lineSize; ++i)
                    {
                        if (req->tokenacquired > 0 || req->bitmask[i])
                        {
                            line->bitmask[i] = true;
                            line->data[i] = req->data.data[i];
                        }
                    }
                }

                line->dirty = true;
                line->tokencount = newtokenline;
                line->pending = false;
                line->invalidated = false;
                line->priority = true;
                line->tlock = false;
            }
        }
        else
        {
            // resolve evicted lines short of data problem in directory configuration
            // need to resend the request again
            // REVISIT JXXX, maybe better solutions
            // JOYING or maybe just delay a little bit
            if (line->tokencount + req->tokenacquired < m_numTokens)
            {
                req->processed = true;

                // just send it again
                if (!SendMessage(req, MINSPACE_FORWARD))
                {
                    return false;
                }
                return true;
            }

            req->processed = false;

            // double check the request and the line get all the tokens
            assert(line->tokencount + req->tokenacquired == m_numTokens);
            assert(line->tlock == false);
            if (req->transient)
                assert(line->priority);

            if (line->tokencount == 0)
            {
                for (unsigned int i = 0 ; i < m_lineSize; ++i)
                {
                    if (req->tokenacquired > 0 || req->bitmask[i])
                    {
                        line->bitmask[i] = true;
                        line->data[i] = req->data.data[i];
                    }
                }
            }
            
            line->time = GetCycleNo();
            line->tokencount += req->tokenacquired;
            line->pending = false;
            line->invalidated = false;
            line->priority = true;
            line->tlock = false;
        }

        if (req->merged)
        {
            for (unsigned int i = 0; i < req->msbcopy->size(); ++i)
            {
                if (!m_clients[(*req->msbcopy)[i].first]->OnMemoryWriteCompleted((*req->msbcopy)[i].second))
                {
                    return FAILED;
                }
            }
            OnPostAcquirePriorityToken(line, req->address);
            delete req;
        }
        else
        {
            if (!m_clients[req->client]->OnMemoryWriteCompleted(req->tid))
            {
                return FAILED;
            }
            OnPostAcquirePriorityToken(line, req->address);
        }
    }
    return true;
}

// network disseminate token and data, EV, WB, include IJ
bool ZLCOMA::Cache::OnDisseminateTokenData(Message* req)
{
    Line* line = FindLine(req->address);

    if (line == NULL)
    {
        // We don't have the line
        if (!m_inject)
        {
            // Do not try to inject
            if (!SendMessage(req, MINSPACE_FORWARD))
            {
                return false;
            }
            return true;
        }
        
        // Try to allocate an empty line to inject the evicted line
        line = GetEmptyLine(req->address);
        if (line == NULL)
        {
            // No free line
            if (!SendMessage(req, MINSPACE_FORWARD))
            {
                return false;
            }
            return true;
        }
        
        // Store evicted line in the allocated line
        line->tag = (req->address / m_lineSize) / m_sets;
        line->time = GetCycleNo();
        line->dirty = (req->tokenrequested == m_numTokens);
        line->tokencount = req->tokenacquired;
        line->pending = false;
        line->invalidated = false;
        line->priority = req->priority;
        line->tlock = false;

        for (unsigned int i = 0; i < m_lineSize; i++)
        {
            if (!line->bitmask[i])
            {
                line->data[i] = req->data.data[i];
            }
        }
        std::fill(line->bitmask, line->bitmask + m_lineSize, true);

        delete req;
        return true;
    }
    
    // We have the line

    if (!line->pending)     // non-pending states   // S, E, O, M
    {
        // give the token of the request to the line 
        assert(line->tlock == false);
        
        line->time = GetCycleNo();
        if (req->tokenrequested == m_numTokens)
        {
            line->dirty = true;
        }
        line->tokencount += req->tokenacquired;
        if (req->priority && !line->priority)
        {
            line->priority = true;
            OnPostAcquirePriorityToken(line, req->address);
        }
        delete req;
    }
    // pending states       // R, T, P, U, W
    else if (line->invalidated)      // T, U
    {
        // do not give the tokens to the T line, but U will decide whether the line should stay
        // the situation will never happen, check the label : label_tokenacquired_always_zero
        //
        // [the original line sent out the DD should have already had been invalidated if the line is U]
        // or [the DD will met a non-invalidated line first, as P, W] 
        assert(!line->dirty);

        if (!SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    else
    {
        // R, P, W
        assert(line->tlock == false);

        if (line->tokencount == 0)
        {
            for (unsigned int i = 0; i < m_lineSize; i++)
            {
                if (!line->bitmask[i])
                {
                    line->data[i] = req->data.data[i];
                }
            }
            std::fill(line->bitmask, line->bitmask + m_lineSize, true);
        }

        // give all the tokens of the request to the line
        line->time = GetCycleNo();
        line->tokencount += req->tokenacquired;

        if (req->priority && !line->priority)
        {
            line->priority = true;
            OnPostAcquirePriorityToken(line, req->address);
        }

        if (!line->dirty && req->tokenrequested == m_numTokens)
        {
            // Special case to reserve the line to be transferred to owner line after reply received.
            line->breserved = true;
        }
        delete req;
    }
    
    return true;
}

bool ZLCOMA::Cache::OnPostAcquirePriorityToken(Line* line, MemAddr address)
{
    assert(line->invalidated == false);

    char data   [MAX_MEMORY_OPERATION_SIZE];
    bool bitmask[MAX_MEMORY_OPERATION_SIZE];
    vector<WriteAck> queuedrequests;
    
    if (!m_msbModule->DumpMergedLine(address, data, bitmask, queuedrequests))
    {
        // No data in the MSB for this line; nothing to do
        return true;
    }
    
    // Merge the changes from the buffer back into the line
    for (unsigned int i = 0; i < m_lineSize; i++)
    {
        if (bitmask[i])
        {
            line->bitmask[i] = true;
            line->data[i] = data[i];
        }
    }
    
    // if the merged request can be handled directly. 
    if (line->dirty)
    {
        assert(line->tokencount != 0);

        // Acknowledge all buffered writes immediately
        for (size_t i = 0; i < queuedrequests.size(); i++)
        {
            if (!m_clients[queuedrequests[i].first]->OnMemoryWriteCompleted(queuedrequests[i].second))
            {
                return false;
            }
        }
    }
    else
    {
        assert(line->pending == false);
        
        line->dirty = true;
        line->pending = true;

        // Send the request and the write acknowledgements.
        // When the request comes back, the acknowledgements will be sent to the clients.
        Message* merge2send = new Message();
        merge2send->processed     = false;
        merge2send->dataavailable  = false;
        merge2send->priority      = false;
        merge2send->transient     = false;
        merge2send->type           = Message::ACQUIRE_TOKEN_DATA;           // NEED TO CHANGE, JONY XXXXXXX
        merge2send->tokenacquired  = 0;
        merge2send->tokenrequested = m_numTokens;
        merge2send->address        = (address / m_lineSize) * m_lineSize;
        merge2send->data.size      = 4;            /// NEED TO CHANGE  JONY XXXXXXX
        std::copy(data,    data    + MAX_MEMORY_OPERATION_SIZE, merge2send->data.data);
        std::copy(bitmask, bitmask + MAX_MEMORY_OPERATION_SIZE, merge2send->bitmask);
        merge2send->merged        = true;
        merge2send->source         = m_id;
        merge2send->msbcopy        = new vector<WriteAck>(queuedrequests);

        if (!SendMessage(merge2send, MINSPACE_FORWARD))
        {
            return false;
        }
    }

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
    if (!OnMessageReceived(m_incoming.Front()))
    {
        return FAILED;
    }
    m_incoming.Pop();
    return SUCCESS;
}

ZLCOMA::Cache::Cache(const std::string& name, ZLCOMA& parent, Clock& clock, CacheID id, size_t numTokens, const Config& config) :
    Simulator::Object(name, parent),
    ZLCOMA::Object(name, parent),
    Node(name, parent, clock),
    m_lineSize (config.getInteger<size_t>("CacheLineSize",           64)),
    m_assoc    (config.getInteger<size_t>("COMACacheAssociativity",   4)),
    m_sets     (config.getInteger<size_t>("COMACacheNumSets",       128)),
    m_numTokens(numTokens),
    m_inject   (config.getBoolean("EnableCacheInjection", true)),
    m_id       (id),
    m_clients  (std::max<size_t>(1, config.getInteger<size_t>("NumProcessorsPerCache", 4)), NULL),
    p_lines    (*this, clock, "p_lines"),
    m_numHits  (0),
    m_numMisses(0),
    p_Requests ("requests", delegate::create<Cache, &Cache::DoRequests>(*this)),
    p_In       ("incoming", delegate::create<Cache, &Cache::DoReceive>(*this)),
    p_bus      (*this, clock, "p_bus"),
    m_requests (clock, config.getInteger<BufferSize>("COMACacheRequestBufferSize",  INFINITE)),
    m_responses(clock, config.getInteger<BufferSize>("COMACacheResponseBufferSize", INFINITE)),
    m_msbModule(new MergeStoreBuffer(3, m_lineSize))
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
}

void ZLCOMA::Cache::Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const
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

        if (line.valid) {
            out << " |                     |        |                                                 |";
        } else {
            out << " | "
                << hex << "0x" << setw(16) << setfill('0') << (line.tag * m_sets + set) * m_lineSize;

            /*switch (line.state)
            {
                default: out << " ";
            }*/
            out << " | " << dec << setfill(' ') << setw(6) << line.tokencount << " |";

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
