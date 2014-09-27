#include "RootDirectory.h"
#include <arch/mem/DDR.h>
#include <sim/config.h>

#include <iomanip>
using namespace std;

namespace Simulator
{

// When we shortcut a message over the ring, we want at least one slots
// available in the buffer to avoid deadlocking the ring network. This
// is not necessary for forwarding messages.
static const size_t MINSPACE_SHORTCUT = 2;
static const size_t MINSPACE_FORWARD  = 1;

CDMA::RootDirectory::Line* CDMA::RootDirectory::FindLine(MemAddr address)
{
    auto linei = m_dir.find(address);
    if (linei == m_dir.end())
        return NULL;
    return &linei->second;
}

static
CDMA::RootDirectory::Line pseudoline = { CDMA::RootDirectory::LINE_EMPTY, 0, (size_t)-1 };

CDMA::RootDirectory::Line* CDMA::RootDirectory::AllocateLine(MemAddr address)
{
    Line *line = &pseudoline;
    COMMIT {
        auto np = m_dir.insert(make_pair(address, Line()));
        assert(np.second == true);
        assert(m_dir.size() <= m_maxNumLines);

        line = &(np.first->second);
    }

    return line;
}

bool CDMA::RootDirectory::OnReadCompleted()
{
    assert (!m_active.empty());

    Message* msg = m_active.front();
    COMMIT
    {
        msg->type = Message::REQUEST_DATA_TOKEN;
        msg->dirty = false;

        static_cast<VirtualMemory&>(m_parent).Read(msg->address, msg->data.data, m_lineSize);

        m_active.pop();
    }

    if (!m_responses.Push(msg))
    {
        DeadlockWrite("Unable to push reply into send buffer");
        return false;
    }

    return true;
}

bool CDMA::RootDirectory::IsLocalAddress(MemAddr addr) const
{
    return (((addr / m_lineSize) % m_numRoots) == m_id);
}

bool CDMA::RootDirectory::OnMessageReceived(Message* msg)
{
    assert(msg != NULL);

    const MemAddr msg_addr = msg->address;

    if (IsLocalAddress(msg_addr))
    {
        // This message is for us
        TraceWrite(msg_addr, "Received message for this directory: %s", msg->str().c_str());

        if (!p_lines.Invoke())
        {
            DeadlockWrite("Unable to acquire lines");
            return false;
        }

        switch (msg->type)
        {
        case Message::REQUEST:
        {
            // Cache-line read request

            // Find or allocate the line
            Line* line = FindLine(msg_addr);
            if (line == NULL)
            {
                // Line has not been read yet it; queue the read
                TraceWrite(msg_addr, "Received Read Request; Miss; Queuing request");

                if (!m_requests.Push(msg))
                {
                    DeadlockWrite("Unable to queue read request to memory");
                    return false;
                }

                line = AllocateLine(msg_addr);

                COMMIT
                {
                    line->state  = LINE_LOADING;
                    line->sender = msg->sender;
                }
                return true;
            } else {
                // We already have an entry for the line, meaning
                // it is being loaded or is somewhere in cache already.
                // Let the message go around:
                // it will pick its data on a cache "later on".
                TraceWrite(msg_addr, "Received Read Request; Hit; Not loaded yet: going around");
            }
            break;
        }

        case Message::REQUEST_DATA:
        {
            // Cache-line read request with data

            // Find or allocate the line. This should not fail.
            Line* line = FindLine(msg_addr);
            if (line == NULL)
                line = AllocateLine(msg_addr);

            if (line->state == LINE_EMPTY)
            {
                // It's possible that a read request with data grabs data from the last copy of a cache-line,
                // which is then evicted from the system before this request comes to the root directory.
                // In that case, we simply reintroduce the tokens into the system (the data is already in
                // the system, so no need to read it from memory).
                TraceWrite(msg_addr, "Received Read Request with data; Miss; Introducing and attaching %u tokens", (unsigned)m_parent.GetTotalTokens());

                COMMIT
                {
                    msg->type   = Message::REQUEST_DATA_TOKEN;
                    msg->tokens = m_parent.GetTotalTokens();
                    line->state = LINE_FULL;
                }
            }
            else if (line->tokens > 0)
            {
                // Give the request the tokens that we have
                TraceWrite(msg_addr, "Received Read Request with data; Hit; Attaching %u tokens", line->tokens);

                COMMIT
                {
                    msg->type    = Message::REQUEST_DATA_TOKEN;
                    msg->tokens  = line->tokens;
                    line->tokens = 0;
                }
            }
            else
            {
                TraceWrite(msg_addr, "Received Read Request with data; Hit; No tokens: going around");
            }
            break;
        }

        case Message::EVICTION:
        {
            Line* line = FindLine(msg_addr);
            assert(line != NULL);
            assert(line->state == LINE_FULL);

            unsigned int tokens = msg->tokens + line->tokens;
            assert(tokens <= m_parent.GetTotalTokens());

            if (tokens < m_parent.GetTotalTokens())
            {
                // We don't have all the tokens, so just store the new token count
                TraceWrite(msg_addr, "Received Evict Request; Adding its %u tokens to directory's %u tokens", msg->tokens, line->tokens);
                COMMIT
                {
                    line->tokens = tokens;
                    delete msg;
                }
            }
            else
            {
                // Evict message with all tokens, discard and remove the line from the system.
                if (msg->dirty)
                {
                    TraceWrite(msg_addr, "Received Evict Request; All tokens; Writing back and clearing line from system");

                    // Line has been modified, queue the writeback
                    if (!m_requests.Push(msg))
                    {
                        DeadlockWrite("Unable to queue eviction to memory");
                        return false;
                    }
                }
                else
                {
                    TraceWrite(msg_addr, "Received Evict Request; All tokens; Clearing line from system");
                    COMMIT{ delete msg; }
                }
                COMMIT{
                    m_dir.erase(msg_addr);
                }
            }
            return true;
        }

        case Message::UPDATE:
        case Message::REQUEST_DATA_TOKEN:
            // Just forward it
            break;

        default:
            UNREACHABLE;
            break;
        }
    }

    // Forward the request
    if (!SendMessage(msg, MINSPACE_SHORTCUT))
    {
        // Can't shortcut the message, go the long way
        COMMIT{ msg->ignore = true; }
        if (!m_requests.Push(msg))
        {
            DeadlockWrite("Unable to forward request");
            return false;
        }
    }
    return true;
}

Result CDMA::RootDirectory::DoIncoming()
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

Result CDMA::RootDirectory::DoRequests()
{
    assert(!m_requests.Empty());

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
        // Since we stripe cache lines across root directories, adjust the
        // address before we send it to memory for timing.
        const MemAddr msg_addr = msg->address;
        const MemAddr mem_address = (msg_addr / m_lineSize) / m_numRoots * m_lineSize;

        if (msg->type == Message::REQUEST)
        {
            // It's a read

#if 1 // set to 0 to shortcut DDR

            if (!m_memory->Read(mem_address, m_lineSize))
            {
                return FAILED;
            }

            COMMIT{
                ++m_nreads;
                m_active.push(msg);
            }
#else
            COMMIT
            {
                ++m_nreads;

                msg->type = Message::REQUEST_DATA_TOKEN;
                msg->dirty = false;

                m_parent.Read(msg_addr, msg->data.data, m_lineSize);
            }

            if (!m_responses.Push(msg))
            {
                DeadlockWrite("Unable to push reply into send buffer");
                return FAILED;
            }
#endif
        }
        else
        {
            // It's a write
            assert(msg->type == Message::EVICTION);

#if 1 // set to 0  to shortcut DDR

            if (!m_memory->Write(mem_address, m_lineSize))
            {
                return FAILED;
            }
#endif
            COMMIT {

                static_cast<VirtualMemory&>(m_parent).Write(msg_addr, msg->data.data, 0, m_lineSize);

                ++m_nwrites;
                delete msg;
            }
        }
    }
    m_requests.Pop();
    return SUCCESS;
}

Result CDMA::RootDirectory::DoResponses()
{
    assert(!m_responses.Empty());
    Message* msg = m_responses.Front();

    // We need this arbitrator for the output channel anyway,
    // even if we don't need or modify any line.
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to acquire lines");
        return FAILED;
    }

    if (!msg->ignore)
    {
        // We should have a loading line for this
        const MemAddr msg_addr = msg->address;
        Line* line = FindLine(msg_addr);
        assert(line != NULL);
        assert(line->state == LINE_LOADING);

        TraceWrite(msg_addr, "Sending Read Response with %u tokens", (unsigned)m_parent.GetTotalTokens());

        COMMIT
        {
            // Since this comes from memory, the reply has all tokens
            msg->tokens = m_parent.GetTotalTokens();
            msg->sender = line->sender;

            // The line has now been read
            line->state = LINE_FULL;
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

void CDMA::RootDirectory::Initialize()
{
    Node* first = GetPrevNode();
    Node* last = GetNextNode();
    assert(first->GetNextNode() == this);
    assert(last->GetPrevNode() == this);

    for (Node *p = first; p != this; p = p->GetPrevNode())
    {
        // Grow the directory as needed by caches or sub-dirs
        m_maxNumLines += p->GetNumLines();

        // Also count the number of sibling directories
        if (dynamic_cast<RootDirectory*>(p) != NULL)
            ++m_numRoots;
    }
}

CDMA::RootDirectory::RootDirectory(const std::string& name, CDMA& parent, Clock& clock, size_t id, const DDRChannelRegistry& ddr) :
    Simulator::Object(name, parent),
    DirectoryBottom(name, parent, clock),
    m_dir    (),
    m_maxNumLines(0),
    m_lineSize (GetTopConf("CacheLineSize", size_t)),
    m_id       (id),
    m_numRoots (1),
    p_lines    (clock, GetName() + ".p_lines"),
    m_memory   (0),
    InitStorage(m_requests, clock, GetConf("ExternalOutputQueueSize", size_t)),
    InitStorage(m_responses, clock, GetConf("ExternalInputQueueSize", size_t)),
    m_active   (),
    InitProcess(p_Incoming, DoIncoming),
    InitProcess(p_Requests, DoRequests),
    InitProcess(p_Responses, DoResponses),
    m_nreads(0),
    m_nwrites(0)
{
    assert(m_lineSize <= MAX_MEMORY_OPERATION_SIZE);

    RegisterModelObject(*this, "rootdir");
    RegisterModelProperty(*this, "freq", (uint32_t)clock.GetFrequency());

    m_incoming.Sensitive(p_Incoming);
    m_requests.Sensitive(p_Requests);
    m_responses.Sensitive(p_Responses);

    p_lines.AddProcess(p_Responses);
    p_lines.AddProcess(p_Incoming);

    size_t ddrid = GetConfOpt("DDRChannelID", size_t, id);
    if (ddrid >= ddr.size())
    {
        throw exceptf<InvalidArgumentException>(*this, "Invalid DDR channel ID: %zu", ddrid);
    }
    m_memory = ddr[ddrid];

    StorageTraceSet sts;
    m_memory->SetClient(*this, sts, m_responses);

    p_Requests.SetStorageTraces(sts ^ m_responses);
    p_Incoming.SetStorageTraces((GetOutgoingTrace() * opt(m_requests)) ^ opt(m_requests));
    p_Responses.SetStorageTraces(GetOutgoingTrace());
}

void CDMA::RootDirectory::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*args*/) const
{
    out <<
    "The Root Directory in a CDMA system is connected via other nodes in the CDMA\n"
    "system via a ring network. It acts as memory controller for a DDR channel which\n"
    "serves as the backing store.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Reads and displays the directory lines, and global information such as hit-rate\n"
    "  and directory configuration.\n"
    "- inspect <component> buffers\n"
    "  Reads and displays the buffers in the directory\n";
}

void CDMA::RootDirectory::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "buffers")
    {
        // Print the buffers
        Print(out, "external requests", m_requests);
        Print(out, "external responses", m_responses);
        Print(out);
        return;
    }

    out << "Max directory size: " << m_maxNumLines << endl
        << "Current directory size: " << m_dir.size() << endl
        << endl;


    // No more than 4 columns per row and at most 1 set per row
    const size_t width = std::min<size_t>(m_dir.size(), 4);

    out << "Entry  |";
    for (size_t i = 0; i < width; ++i) out << "       Address      | State       |";
    out << endl << "-------";
    std::string separator = "+";
    for (size_t i = 0; i < width; ++i) separator += "--------------------+-------------+";
    out << separator << endl;

    auto p = m_dir.begin();
    for (size_t i = 0; i < m_dir.size(); i += width)
    {
        out << setw(6) << dec << right << i << " | ";
        for (size_t j = i; j < i + width; ++j)
        {
            if (p != m_dir.end())
            {
                out << hex << "0x" << setfill('0') << setw(16) << p->first << " | "
                    << dec << setfill(' ') << right;
                switch (p->second.state) {
                case LINE_EMPTY:
                    out << "E          ";
                    break;
                case LINE_LOADING:
                    out << "L      S" << setw(3) << (int)p->second.sender;
                    break;
                case LINE_FULL:
                    out << "L T" << setw(3) << p->second.tokens
                        << "     ";
                    break;
                }
                p++;

            } else {
                out << "                   |            ";
            }
            out << " | ";
        }
        out << endl;
    }
    out << "-------" << separator << endl;
}

}
