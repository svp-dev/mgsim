#include "RootDirectory.h"
#include "DDR.h"
#include "../../config.h"
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
    assert(m_activeMsg->type == Message::ACQUIRE_TOKEN_DATA);

    COMMIT
    {
        m_activeMsg->type = Message::ACQUIRE_TOKEN_DATA;
        m_activeMsg->data = data;
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

    if (!p_lines.Invoke())
    {
        return false;
    }

    // Find the line for the request
    Line* line = FindLine(req->address);
    
    switch (req->type)
    {
    case Message::ACQUIRE_TOKEN_DATA:
        // Request for tokens and data
        if (line == NULL)
        {
            // Need to fetch a line off-chip
            line = GetEmptyLine(req->address);
            assert(line != NULL);

            // Initialize line
            line->tag      = (req->address / m_lineSize) / m_sets;
            line->valid    = true;
            line->tokens   = 0;
            line->priority = false;
            line->reserved = true;
            
            // JOYING revisit, this is to resolve the reload bug
            req->processed = false;
            if (!m_requests.Push(req))
            {
                return false;
            }
        }
        else
        {
            // Line can be found in the group, just pass the request.
        
            // Transfer any tokens that we have to the request only when
            // the request is not a request with transient tokens.
            if (!req->transient)
            {
                assert(req->tokenacquired + line->tokens <= m_numTokens);
            
                req->tokenacquired += line->tokens;
                req->priority      = req->priority || line->priority;
                line->tokens        = 0;
                line->priority      = false;
            }

            // REVISIT, will this cause too much additional traffic?
            if (!line->reserved && !req->dataavailable && (req->gettokenpermanent() == m_numTokens || req->processed))
            {
                // Send request off-chip
                line->reserved = true;
                if (!m_requests.Push(req))
                {
                    return false;
                }
            }
            else if (line->reserved)
            {
                // Append request to line
                line->requests.push(req);
            }
            else
            {
                // Forward request on network
                if (!SendMessage(req, MINSPACE_FORWARD))
                {
                    return false;
                }
            }
        }
        break;

    case Message::ACQUIRE_TOKEN:
        // Request for tokens. We should have the line.
        assert(line != NULL);
    
        // Transfer any tokens that we have to the request only when
        // the request is not a request with transient tokens.
        if (!req->transient)
        {
            assert(req->tokenacquired + line->tokens <= m_numTokens);
        
            req->tokenacquired += line->tokens;
            req->priority      = req->priority || line->priority;
            line->tokens        = 0;
            line->priority      = false;
        }

        line->tokens = 0;

        if (line->reserved)
        {
            // Append request to line
            line->requests.push(req);
        }
        else
        {
            // Forward request on network
            if (!SendMessage(req, MINSPACE_FORWARD))
            {
                return false;
            }
        }
        break;

    case Message::DISSEMINATE_TOKEN_DATA:
        // Eviction. We should have the line
        assert(line != NULL);
    
        assert(req->tokenacquired > 0);

        // Evictions always terminate at the root directory.
        if (line->reserved)
        {
            line->requests.push(req);
        }
        else
        {
            // Add the tokens in the eviction to the line
            line->tokens  += req->tokenacquired;
            line->priority = line->priority || req->priority;

            if (line->tokens == m_numTokens)
            {
                // We have all the tokens now; clear the line
                line->valid = false;
            }

            if (req->tokenrequested == 0)
            {
                // Non-dirty data; we don't have to write back
                assert(req->transient == false);
                delete req;
            }
            else 
            {
                // Dirty data; write back the data to memory
                assert(req->tokenrequested == m_numTokens);
                if (!m_requests.Push(req))
                {
                    return false;
                }
            }
        }
        break;
        
    default:
        assert(false);
        break;
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
        if (msg->type == Message::ACQUIRE_TOKEN_DATA)
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
            COMMIT{ m_activeMsg = msg; }
        }
        else
        {
            // It's a write
            assert(msg->type == Message::DISSEMINATE_TOKEN_DATA);
            if (!m_memory->Write(msg->address, msg->data.data, msg->data.size))
            {
                return FAILED;
            }
            COMMIT{ delete msg; }
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
        //assert(line->state == LINE_LOADING);

        TraceWrite(msg->address, "Sending Read Response with %u tokens", (unsigned)m_numTokens);

        COMMIT
        {
            // Since this comes from memory, the reply has all tokens
            msg->tokenacquired = m_numTokens;
            msg->source        = line->source;

            // The line has now been read
            //line->state = LINE_FULL;
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

ZLCOMA::RootDirectory::RootDirectory(const std::string& name, ZLCOMA& parent, Clock& clock, VirtualMemory& memory, size_t numCaches, const Config& config) :
    Simulator::Object(name, parent),
    ZLCOMA::Object(name, parent),
    DirectoryBottom(name, parent, clock),
    m_lineSize(config.getInteger<size_t>("CacheLineSize",           64)),
    m_assoc   (config.getInteger<size_t>("COMACacheAssociativity",   4) * numCaches),
    m_sets    (config.getInteger<size_t>("COMACacheNumSets",       128)),
    m_numTokens(numCaches),
    p_lines    (*this, clock, "p_lines"),
    m_requests (clock, INFINITE),
    m_responses(clock, INFINITE),
    m_memready (clock, true),
    m_activeMsg(NULL),
    p_Incoming ("incoming",  delegate::create<RootDirectory, &RootDirectory::DoIncoming>(*this)),
    p_Requests ("requests",  delegate::create<RootDirectory, &RootDirectory::DoRequests>(*this)),
    p_Responses("responses", delegate::create<RootDirectory, &RootDirectory::DoResponses>(*this))
{
    assert(m_lineSize <= MAX_MEMORY_OPERATION_SIZE);

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

    p_lines.AddProcess(p_Incoming);
    p_lines.AddProcess(p_Responses);

    Clock& ddrclock = GetKernel()->CreateClock( config.getInteger<size_t>("DDRMemoryFreq", 800));
    m_memory = new DDRChannel("ddr", *this, ddrclock, memory, config);
}

ZLCOMA::RootDirectory::~RootDirectory()
{
    delete m_memory;
}

void ZLCOMA::RootDirectory::Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const
{
    out <<
    "The Root Directory in a COMA system is connected via other nodes in the COMA\n"
    "system via a ring network. It acts as memory controller for a DDR channel which\n"
    "serves as the backing store.\n\n"
    "Supported operations:\n"
    "- read <component>\n"
    "  Reads and displays the directory lines, and global information such as hit-rate\n"
    "  and directory configuration.\n"
    "- read <component> buffers\n"
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
                /*if (line.state == LINE_LOADING) {
                    out << " L";
                } else*/ {
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

