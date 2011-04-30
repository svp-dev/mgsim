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
    
    TraceWrite(address, "Evicting with %u tokens due to miss for 0x%llx", line->tokens, (unsigned long long)req.address);
    
    Message* msg = NULL;
    COMMIT
    {
        msg = new Message;
        msg->type      = Message::EVICTION;
        msg->address   = address;
        msg->ignore    = false;
        msg->sender    = m_id;
        msg->tokens    = line->tokens;
        msg->data.size = m_lineSize;
        msg->dirty     = line->dirty;
        memcpy(msg->data.data, line->data, m_lineSize);
    }
    
    if (!SendMessage(msg, MINSPACE_INSERTION))
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
            DeadlockWrite("Unable to buffer request for next node");
            return false;
        }
        return true;
    }
    
    Line* line = FindLine(msg->address);   
    switch (msg->type)
    {
    case Message::REQUEST:
    case Message::REQUEST_DATA:
        // Some cache had a read miss. See if we have the line.
        assert(msg->data.size == m_lineSize);
        
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
                    memcpy(msg->data.data, line->data, msg->data.size);

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
                    memcpy(msg->data.data, line->data, msg->data.size);
                }
            }
        }

        // Forward the message.
        if (!SendMessage(msg, MINSPACE_FORWARD))
        {
            DeadlockWrite("Unable to buffer request for next node");
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
        MemData data(msg->data);
        COMMIT
        {
            for (Buffer<Request>::const_iterator p = m_requests.begin(); p != m_requests.end(); ++p)
            {
                unsigned int offset = p->address % m_lineSize;
                if (p->write && p->address - offset == msg->address)
                {
                    // This is a write to the same line, merge it
                    std::copy(p->data, p->data + p->size, data.data + offset);
                }
            }
        }

        if (!OnReadCompleted(msg->address, data))
        {
            DeadlockWrite("Unable to notify clients of read completion");
            return false;
        }
        
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
                }
                break;
            }
        }
        // We don't have the line, see if we have an empty spot
        else if ((line = AllocateLine(msg->address, true)) != NULL)
        {
            // Yes, place the line there
            TraceWrite(msg->address, "Storing Evict Request with %u tokens", msg->tokens);

            COMMIT
            {
                line->state    = LINE_FULL;
                line->tag      = (msg->address / m_lineSize) / m_sets;
                line->tokens   = msg->tokens;
                line->dirty    = msg->dirty;
                line->updating = 0;
                line->access   = GetKernel()->GetCycleNo();
                std::fill(line->valid, line->valid + MAX_MEMORY_OPERATION_SIZE, true);
                memcpy(line->data, msg->data.data, msg->data.size);
            }
                
            COMMIT{ delete msg; }
            break;
        }

        // Just forward it
        if (!SendMessage(msg, MINSPACE_FORWARD))
        {
            DeadlockWrite("Unable to buffer request for next node");
            return false;
        }
        break;

    case Message::UPDATE:
        if (msg->sender == m_id)
        {
            // The update has come full circle.
            // Notify the sender of write consistency.
            assert(line != NULL);
            assert(line->updating > 0);
            
            if (!m_clients[msg->client]->OnMemoryWriteCompleted(msg->tid))
            {
                return false;
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
            if (line != NULL)
            {
                COMMIT
                {
                    unsigned int offset = msg->address % m_lineSize;
                    memcpy(line->data + offset, msg->data.data, msg->data.size);
                    std::fill(line->valid + offset, line->valid + offset + msg->data.size, true);
                }
            
                // Send the write as a snoop to the processors
                for (size_t i = 0; i < m_clients.size(); ++i)
                {
                    if (m_clients[i] != NULL)
                    {
                        if (!m_clients[i]->OnMemorySnooped(msg->address, msg->data))
                        {
                            DeadlockWrite("Unable to snoop update to cache clients");
                            return false;
                        }
                    }
                }
            }
            
            if (!SendMessage(msg, MINSPACE_FORWARD))
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

        TraceWrite(req.address, "Processing Bus Write Request: Miss; Sending Read Request");
        
        // Reset the line
        COMMIT
        {
            line->state    = LINE_LOADING;
            line->tag      = (req.address / m_lineSize) / m_sets;
            line->tokens   = 0;
            line->dirty    = false;
            line->updating = 0;
            std::fill(line->valid, line->valid + MAX_MEMORY_OPERATION_SIZE, false);
        }
        
        // Send a request out for the cache-line
        Message* msg = NULL;
        COMMIT
        {
            msg = new Message;
            msg->type      = Message::REQUEST;
            msg->address   = (req.address / m_lineSize) * m_lineSize;
            msg->ignore    = false;
            msg->data.size = m_lineSize;
            msg->tokens    = 0;
            msg->sender    = m_id;
        }
            
        if (!SendMessage(msg, MINSPACE_INSERTION))
        {
            return FAILED;
        }
        
        // Now try against next cycle
        return DELAYED;
    }

    // Write hit
    // Although we may hit a loading line
    if (line->state == LINE_FULL && line->tokens == m_numCaches)
    {
        // We have all tokens, notify the sender client immediately
        TraceWrite(req.address, "Processing Bus Write Request: Exclusive Hit");
        
        if (!m_clients[req.client]->OnMemoryWriteCompleted(req.tid))
        {
            return FAILED;
        }
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
            msg->tid       = req.tid;
            msg->data.size = req.size;
            memcpy(msg->data.data, req.data, req.size);
                
            // Lock the line to prevent eviction
            line->updating++;
        }
            
        if (!SendMessage(msg, MINSPACE_INSERTION))
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

        // Reset the line
        COMMIT
        {
            line->state    = LINE_LOADING;
            line->tag      = (req.address / m_lineSize) / m_sets;
            line->tokens   = 0;
            line->dirty    = false;
            line->updating = 0;
            line->access   = GetKernel()->GetCycleNo();
            std::fill(line->valid, line->valid + MAX_MEMORY_OPERATION_SIZE, false);

            m_numMisses++;
        }
        
        // Send a request out
        Message* msg = NULL;
        COMMIT
        {
            msg = new Message;
            msg->type      = Message::REQUEST;
            msg->address   = req.address;
            msg->ignore    = false;
            msg->data.size = req.size;
            msg->tokens    = 0;
            msg->sender    = m_id;
        }
            
        if (!SendMessage(msg, MINSPACE_INSERTION))
        {
            return FAILED;
        }
    }
    // Read hit
    else if (line->state == LINE_FULL)
    {
        // Line is present and full
        TraceWrite(req.address, "Processing Bus Read Request: Full Hit");

        // Return the data
        MemData data;
        data.size = req.size;
        COMMIT
        {
            memcpy(data.data, line->data, data.size);

            // Update LRU information
            line->access = GetKernel()->GetCycleNo();
            
            m_numHits++;
        }

        if (!OnReadCompleted(req.address, data))
        {
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
        COMMIT{ m_numMisses++; }
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
    
Result COMA::Cache::DoReceive()
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

COMA::Cache::Cache(const std::string& name, COMA& parent, Clock& clock, CacheID id, size_t numCaches, Config& config) :
    Simulator::Object(name, parent),
    //COMA::Object(name, parent),
    Node(name, parent, clock, config),
    m_lineSize (config.getValue<size_t>("CacheLineSize")),
    m_assoc    (config.getValue<size_t>(parent, "L2CacheAssociativity")),
    m_sets     (config.getValue<size_t>(parent, "L2CacheNumSets")),
    m_numCaches(numCaches),
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

    p_bus.AddProcess(p_In);                   // Update triggers write completion
    p_bus.AddProcess(p_Requests);             // Read or write hit
}

void COMA::Cache::Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const
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

        if (line.state == LINE_EMPTY) {
            out << " |                     |        |                                                 |";
        } else {
            out << " | "
                << hex << "0x" << setw(16) << setfill('0') << (line.tag * m_sets + set) * m_lineSize;

            switch (line.state)
            {
                case LINE_LOADING: out << "L"; break;
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
