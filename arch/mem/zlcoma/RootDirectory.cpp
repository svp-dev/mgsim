#include "RootDirectory.h"
#include "mem/DDR.h"
#include "sim/config.h"
#include <iomanip>
using namespace std;

namespace Simulator
{

// When we shortcut a message over the ring, we want at least one slots
// available in the buffer to avoid deadlocking the ring network. This
// is not necessary for forwarding messages.
static const size_t MINSPACE_SHORTCUT = 2;
static const size_t MINSPACE_FORWARD  = 1;

ZLCOMA::RootDirectory::Line* ZLCOMA::RootDirectory::FindLine(MemAddr address)
{
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line* line = &m_lines[set + i];
        if (line->valid && line->tag == tag)
        {
            return line;
        }
    }
    return NULL;
}

const ZLCOMA::RootDirectory::Line* ZLCOMA::RootDirectory::FindLine(MemAddr address) const
{
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        const Line* line = &m_lines[set + i];
        if (line->valid && line->tag == tag)
        {
            return line;
        }
    }
    return NULL;
}

// replace only invalid lines otherwise NULL
ZLCOMA::RootDirectory::Line* ZLCOMA::RootDirectory::GetEmptyLine(MemAddr address)
{
    const size_t set = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line* line = &m_lines[set + i];
        if (!line->valid)
        {
            return line;
        }
    }
    return NULL;
}

bool ZLCOMA::RootDirectory::OnReadCompleted(MemAddr address, const MemData& data)
{
    assert(m_activeMsg != NULL);
    assert(m_activeMsg->address == address);
    assert(m_activeMsg->type == Message::READ);

    // Attach data to message, give all tokens and send
    COMMIT
    {
        for (size_t i = 0; i < m_lineSize; ++i)
        {
            if (!m_activeMsg->bitmask[i])
            {
                m_activeMsg->data[i] = data.data[i];
                m_activeMsg->bitmask[i] = true;
            }
        }
        m_activeMsg->dirty = false;
    }

    if (!m_responses.Push(m_activeMsg))
    {
        DeadlockWrite("Unable to push reply into send buffer");
        return false;
    }

    // We're done with this request
    if (!m_memready.Set())
    {
        return false;
    }
    COMMIT{ m_activeMsg = NULL; }
    return true;
}


bool ZLCOMA::RootDirectory::OnMessageReceived(Message* req)
{
    assert(req != NULL);

    if (((req->address / m_lineSize) % m_numRoots) == m_id)
    {
        // This message is for us
        if (!p_lines.Invoke())
        {
            return false;
        }

        // Find the line for the request
        Line* line = FindLine(req->address);    
        switch (req->type)
        {
        case Message::READ:
            // Request for tokens and data
            assert(req->transient == false);
            if (line == NULL)
            {
                // Need to fetch a line off-chip
                line = GetEmptyLine(req->address);
                assert(line != NULL);
            
                assert(req->tokens == 0);
                assert(req->priority == false);

                TraceWrite(req->address, "Received Read Request; Miss; Queuing request");
            
                // Initialize line
                COMMIT
                {
                    line->tag      = (req->address / m_lineSize) / m_sets;
                    line->valid    = true;
                    line->data     = false;
                    line->tokens   = 0;
                    line->priority = false;
                    line->loading  = true;

                    // Introduce all tokens as well
                    req->tokens   = m_numTokens;
                    req->priority = true;
                }
            
                if (!m_requests.Push(req))
                {
                    return false;
                }
                return true;
            }
            
            if (line->loading)
            {
                // The line ia lready being loaded, just continue and try again later
                break;
            }
            
            if (!contains(req->bitmask, req->bitmask + m_lineSize, false))
            {
                // The message itself contains all data, which means it already exists in the system
                // without going through the root directory (i.e., writes).
                // Mark the line as having data and forward the message.
                //
                // (This is an optimization to avoid the read to external memory only to discard
                // the data during the merge).
                COMMIT { line->data = true; }
            }
            else if (!line->data)
            {
                // We have the line, but not the data, read it
                // Also add any tokens we may have.
                TraceWrite(req->address, "Received Read Request; Reading and attaching %u tokens", line->tokens);

                COMMIT
                {
                    req->tokens += line->tokens;
                    req->priority = req->priority || line->priority;
                
                    line->tokens = 0;
                    line->priority = false;

                    line->loading = true;
                }

                if (!m_requests.Push(req))
                {
                    return false;
                }
                return true;
            }
            
            TraceWrite(req->address, "Received Read Request; Attaching %u tokens", line->tokens);
            
            COMMIT
            {
                req->tokens += line->tokens;
                req->priority = req->priority || line->priority;
                    
                line->tokens = 0;
                line->priority = false;
            }

            // Note that if the line is currently being loaded, the request
            // can go all the way around without finding anything. Eventually,
            // though the line will be loaded and this request will hit a cache
            // that has the data.
            // Theoretically we could buffer all additional requests for loading
            // lines, but that's a bit unfeasible. The network is the buffer now.
            break;

        case Message::ACQUIRE_TOKENS:
            // Transfer any tokens that we have to the request only when
            // the request is not a request with transient tokens.
            if (!req->transient)
            {
                if (line == NULL)
                {
                    // Line didn't exist yet
                    line = GetEmptyLine(req->address);
                    assert(line != NULL);
                
                    assert(req->tokens == 0);
                    assert(req->priority == false);
                
                    TraceWrite(req->address, "Received Token Request; Miss; Introducing %u tokens", (unsigned)m_numTokens);

                    // Introduce all tokens into the system, but don't read the data from memory
                    COMMIT
                    {
                        line->tag      = (req->address / m_lineSize) / m_sets;
                        line->valid    = true;
                        line->data     = false;
                        line->tokens   = 0;
                        line->priority = false;
                        line->loading  = false;
                    
                        req->tokens = m_numTokens;
                        req->priority = true;
                    }
                }
                else if (line->tokens > 0)
                {
                    TraceWrite(req->address, "Received Token Request; Attaching %u tokens", line->tokens);
            
                    COMMIT
                    {
                        req->tokens += line->tokens;
                        req->priority = req->priority || line->priority;
                
                        line->tokens = 0;
                        line->priority = false;
                    }
                }
            }
        
            // Note that if the line is currently being loaded, the request
            // can go all the way around without finding anything. Eventually,
            // though the line will be loaded and this request will hit a cache
            // that has the data.
            // Theoretically we could buffer all additional requests for loading
            // lines, but that's a bit unfeasible. The network is the buffer now.
            break;

        case Message::EVICTION:
            // Eviction. We should have the line.
            // Since we're evicting a line, we should've already loaded the line.
            // Evictions always terminate at the root directory.
            assert(line != NULL);
            assert(!line->loading);
            assert(req->tokens > 0);

            // Add the tokens in the eviction to the line
            COMMIT
            {
                line->tokens += req->tokens;
                line->priority = line->priority || req->priority;
            
                if (line->tokens == m_numTokens)
                {
                    TraceWrite(req->address, "Received Evict Request; All tokens; Clearing line from system");
                
                    // We have all the tokens now; clear the line
                    assert(line->priority);
                    line->valid = false;
                }
                else
                {
                    TraceWrite(req->address, "Received Evict Request; Adding its %u tokens to directory's %u tokens", req->tokens, line->tokens);
                }
            }
        
            if (!req->dirty)
            {
                // Non-dirty data; we don't have to write back
                COMMIT{ delete req; }
            }
            // Dirty data; write back the data to memory
            else if (!m_requests.Push(req))
            {
                return false;
            }
            return true;
        
        default:
            assert(false);
            break;
        }
    }

    // Forward the request
    if (!SendMessage(req, MINSPACE_SHORTCUT))
    {
        // Can't shortcut the message, go the long way
        COMMIT{ req->ignore = true; }
        if (!m_requests.Push(req))
        {
            DeadlockWrite("Unable to forward request");
            return false;
        }
    }
    return true;
}

Result ZLCOMA::RootDirectory::DoIncoming()
{
    // Handle incoming message from previous node
    assert(!m_incoming.Empty());
    if (!OnMessageReceived(m_incoming.Front()))
    {
        return FAILED;
    }
    m_incoming.Pop();
    return SUCCESS;
}

Result ZLCOMA::RootDirectory::DoRequests()
{
    assert(!m_requests.Empty());

    if (!m_memready.IsSet())
    {
        // We're currently processing a read that will produce a reply, stall
        return FAILED;
    }
    assert(m_activeMsg == NULL);

    Message* msg = m_requests.Front();
    if (msg->ignore)
    {
        // Ignore this message; put on responses queue for re-insertion into global ring
        if (!m_responses.Push(msg))
        {
            return FAILED;
        }
    }
    else
    {
        if (msg->type == Message::READ)
        {
            // It's a read
            if (!m_memory->Read(msg->address, m_lineSize))
            {
                return FAILED;
            }

            if (!m_memready.Clear())
            {
                return FAILED;
            }
            COMMIT{ 
                ++m_nreads;
                m_activeMsg = msg; 
            }
        }
        else
        {
            // It's a write
            assert(msg->type == Message::EVICTION);
            if (!m_memory->Write(msg->address, msg->data, m_lineSize))
            {
                return FAILED;
            }
            COMMIT{
                ++m_nwrites;
                delete msg; 
            }
        }
    }
    m_requests.Pop();
    return SUCCESS;
}

Result ZLCOMA::RootDirectory::DoResponses()
{
    assert(!m_responses.Empty());
    Message* msg = m_responses.Front();

    // We need this arbitrator for the output channel anyway,
    // even if we don't need or modify any line.
    if (!p_lines.Invoke())
    {
        return FAILED;
    }

    if (!msg->ignore)
    {
        // We should have a loading line for this
        Line* line = FindLine(msg->address);
        assert(line != NULL);
        assert(line->loading);

        TraceWrite(msg->address, "Sending Read Response with %u tokens", (unsigned)m_numTokens);

        COMMIT
        {
            // The line has now been read
            line->loading = false;
            line->data    = true;
        }
    }

    COMMIT{ msg->ignore = false; }

    if (!SendMessage(msg, MINSPACE_FORWARD))
    {
        return FAILED;
    }

    m_responses.Pop();
    return SUCCESS;
}

ZLCOMA::RootDirectory::RootDirectory(const std::string& name, ZLCOMA& parent, Clock& clock, VirtualMemory& memory, size_t numCaches, size_t id, size_t numRoots, const DDRChannelRegistry& ddr, Config& config) :
    Simulator::Object(name, parent),
    DirectoryBottom(name, parent, clock),
    m_lineSize(config.getValue<size_t>("CacheLineSize")),
    m_assoc   (config.getValue<size_t>(parent, "L2CacheAssociativity") * numCaches),
    m_sets    (config.getValue<size_t>(parent, "L2CacheNumSets")),
    m_numTokens(numCaches),
    m_id       (id),
    m_numRoots (numRoots),
    p_lines    (*this, clock, "p_lines"),
    m_requests ("b_requests", *this, clock, config.getValue<size_t>(*this, "ExternalOutputQueueSize")),
    m_responses("b_responses", *this, clock, config.getValue<size_t>(*this, "ExternalInputQueueSize")),
    m_memready ("f_memready", *this, clock, true),
    m_activeMsg(NULL),
    p_Incoming ("incoming",  delegate::create<RootDirectory, &RootDirectory::DoIncoming>(*this)),
    p_Requests ("requests",  delegate::create<RootDirectory, &RootDirectory::DoRequests>(*this)),
    p_Responses("responses", delegate::create<RootDirectory, &RootDirectory::DoResponses>(*this)),
    m_nreads(0),
    m_nwrites(0)
{
    assert(m_lineSize <= MAX_MEMORY_OPERATION_SIZE);

    config.registerObject(*this, "rootdir");
    config.registerProperty(*this, "freq", (uint32_t)clock.GetFrequency());
    
    // Create the cache lines
    // We need as many cache lines in the directory to cover all caches below it
    m_lines.resize(m_assoc * m_sets);
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        m_lines[i].valid = false;
    }

    m_incoming.Sensitive(p_Incoming);
    m_requests.Sensitive(p_Requests);
    m_responses.Sensitive(p_Responses);

    p_lines.AddProcess(p_Responses);
    p_lines.AddProcess(p_Incoming);

    size_t ddrid = config.getValue<size_t>(*this, "DDRChannelID", id);
    if (ddrid >= ddr.size())
    {
        throw exceptf<InvalidArgumentException>(*this, "Invalid DDR channel ID: %zu", ddrid);
    }
    m_memory = ddr[ddrid];
    m_memory->SetClient(*this);
}

void ZLCOMA::RootDirectory::Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const
{
    out <<
    "The Root Directory in a COMA system is connected via other nodes in the COMA\n"
    "system via a ring network. It acts as memory controller for a DDR channel which\n"
    "serves as the backing store.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Reads and displays the directory lines, and global information such as hit-rate\n"
    "  and directory configuration.\n"
    "- inspect <component> buffers\n"
    "  Reads and displays the buffers in the directory\n";
}

void ZLCOMA::RootDirectory::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "buffers")
    {
        // Print the buffers
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
    out << endl;

    // No more than 4 columns per row and at most 1 set per row
    const size_t width = std::min<size_t>(m_assoc, 4);

    out << "Set |";
    for (size_t i = 0; i < width; ++i) out << "        Address       |";
    out << endl << "----";
    std::string seperator = "+";
    for (size_t i = 0; i < width; ++i) seperator += "----------------------+";
    out << seperator << endl;

    for (size_t i = 0; i < m_lines.size() / width; ++i)
    {
        const size_t index = (i * width);
        const size_t set   = index / m_assoc;

        if (index % m_assoc == 0) {
            out << setw(3) << setfill(' ') << dec << right << set;
        } else {
            out << "   ";
        }

        out << " | ";
        for (size_t j = 0; j < width; ++j)
        {
            const Line& line = m_lines[index + j];
            if (!line.valid) {
                out << "                    ";
            } else {
                out << hex << "0x" << setw(16) << setfill('0') << (line.tag * m_sets + set) * m_lineSize;
                if (line.loading) {
                    out << " L";
                } else {
                    out << "  ";
                }
            }
            out << " | ";
        }
        out << endl
            << ((index + width) % m_assoc == 0 ? "----" : "    ")
            << seperator << endl;
    }
}

}

