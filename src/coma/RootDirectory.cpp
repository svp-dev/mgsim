#include "RootDirectory.h"
#include "DDR.h"
#include "../config.h"
#include <iomanip>
using namespace std;

namespace Simulator
{

COMA::RootDirectory::Line* COMA::RootDirectory::FindLine(MemAddr address, bool check_only)
{
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    // Find the line
    Line* empty = NULL;
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line* line = &m_lines[set + i];

        if (line->state == LINE_EMPTY)
        {
            // Empty, unused line, remember this one
            empty = line;
        }
        else if (line->tag == tag)
        {
            // The wanted line was in the cache
            return line;
        }
    }

    // The line could not be found, allocate the empty line or replace an existing line
    Line* line = NULL;
    if (!check_only && empty != NULL)
    {
        // Reset the line
        line = empty;
        line->tag   = tag;
        line->state = LINE_EMPTY;
    }
    return line;
}

const COMA::RootDirectory::Line* COMA::RootDirectory::FindLine(MemAddr address) const
{
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        const Line* line = &m_lines[set + i];
        if (line->state != LINE_EMPTY && line->tag == tag)
        {
            return line;
        }
    }
    return NULL;
}

bool COMA::RootDirectory::OnReadCompleted(MemAddr address, const MemData& data)
{
    // We should have a loading line for this
    Line* line = FindLine(address, true);
    assert(line != NULL);
    assert(line->state == LINE_LOADING);

    TraceWrite(address, "Sending Read Response");
    
    // Since this comes from memory, the reply has all tokens
    Message* reply = NULL;
    COMMIT
    {
        reply = new Message;
        reply->type    = Message::RESPONSE_READ;
        reply->address = address;
        reply->tokens  = m_numCaches;
        reply->data    = data;
        reply->hops    = line->hops;
    }
    
    if (!m_outgoing.Push(reply))
    {
        DeadlockWrite("Unable to push reply into send buffer");
        return false;
    }
    
    // The line has now been read
    COMMIT{ line->state = LINE_FULL; }   
    return true;
}

bool COMA::RootDirectory::OnRequestReceived(Message* msg)
{
    assert(msg != NULL);

    switch (msg->type)
    {
    case Message::REQUEST_READ:
    {
        // Cache-line read request
        assert(msg->data.size == m_lineSize);
        
        // See if a cache below this directory has the line
        Line* line = FindLine(msg->address, false);
        if (line->state != LINE_FULL)
        {
            if (line->state == LINE_LOADING)
            {
                // Same as a cache-hit on a loading line with forward flag.
                // Update the hop count and send a response back indicating
                // that the forward flag should be set.
                TraceWrite(msg->address, "Received Read Request; Loading Hit; Sending Forward Response");
    
                COMMIT
                {
                    msg->type   = Message::RESPONSE_FORWARD;
                    msg->tokens = line->hops - (1 + msg->hops);
                    line->hops  = msg->hops;
                }
            
                if (!m_prev.Send(msg))
                {
                    return false;
                }
            }
            else
            {
                // Line has not been read yet it; queue the read
                assert(line->state == LINE_EMPTY);

                TraceWrite(msg->address, "Received Read Request; Miss; Queuing request");
                MemRequest req;
                req.address = msg->address;
                req.data.size = 0;
                if (!m_incoming.Push(req))
                {
                    return false;
                }

                COMMIT
                {
                    line->state = LINE_LOADING;
                    line->hops  = msg->hops;
                    delete msg;
                }
            }
            return true;
        }
        break;
    }

    case Message::REQUEST_EVICT:
        assert(msg->tokens > 0);
        if ((unsigned)msg->tokens == m_numCaches)
        {
            // Evict message with all tokens, discard and remove the line from the system.
            Line* line = FindLine(msg->address, true);
            assert(line != NULL);
            assert(line->state == LINE_FULL);
            
            if (msg->dirty)
            {
                TraceWrite(msg->address, "Received Evict Request; All tokens; Writing back and clearing line from system");
                
                // Line has been modified, queue the writeback
                MemRequest req;
                req.address = msg->address;
                req.data = msg->data;
                if (!m_incoming.Push(req))
                {
                    return false;
                }
            }
            else
            {
                TraceWrite(msg->address, "Received Evict Request; All tokens; Clearing line from system");
            }
            
            COMMIT
            {
                line->state = LINE_EMPTY;
                delete msg;
            }
            return true;
        }
        
        // This eviction does not have all the tokens.
        // Forward it around to be merged.
        break;

    case Message::REQUEST_KILL_TOKENS:
        // Just forward it to be merged.
        break;

    case Message::REQUEST_UPDATE:
        // Just forward it
        break;
                    
    default:
        assert(false);
        break;
    }

    // Forward the request
    if (!m_next.Send(msg))
    {
        DeadlockWrite("Unable to forward request");
        return false;
    }
    return true;
}

bool COMA::RootDirectory::OnResponseReceived(Message* msg)
{
    assert(msg != NULL);
    
    // Forward the response
    if (!m_prev.Send(msg))
    {
        DeadlockWrite("Unable to forward response");
        return false;
    }
    return true;
}

Result COMA::RootDirectory::DoInPrevBottom()
{
    // Handle incoming request on bottom ring from previous node
    assert(!m_prev.incoming.Empty());
    if (!OnRequestReceived(m_prev.incoming.Front()))
    {
        return FAILED;
    }
    m_prev.incoming.Pop();
    return SUCCESS;
}

Result COMA::RootDirectory::DoInNextBottom()
{
    // Handle incoming response on bottom ring from next node
    assert(!m_next.incoming.Empty());
    if (!OnResponseReceived(m_next.incoming.Front()))
    {
        return FAILED;
    }
    m_next.incoming.Pop();
    return SUCCESS;
}

Result COMA::RootDirectory::DoOutNextBottom()
{
    // Send outgoing message on bottom ring to next node
    assert(!m_next.outgoing.Empty());
    if (!m_next.node->ReceiveMessagePrev(m_next.outgoing.Front()))
    {
        return FAILED;
    }
    m_next.outgoing.Pop();
    return SUCCESS;
}

Result COMA::RootDirectory::DoOutPrevBottom()
{
    // Send outgoing message on bottom ring to previous node
    assert(!m_prev.outgoing.Empty());
    if (!m_prev.node->ReceiveMessageNext(m_prev.outgoing.Front()))
    {
        return FAILED;
    }
    m_prev.outgoing.Pop();
    return SUCCESS;
}

Result COMA::RootDirectory::DoIncoming()
{
    assert(!m_incoming.Empty());
    const MemRequest& req = m_incoming.Front();
    if (req.data.size == 0)
    {
        // It's a read
        if (!m_memory->Read(req.address, m_lineSize))
        {
            return FAILED;
        }
    }
    else
    {
        // It's a write
        if (!m_memory->Write(req.address, req.data.data, req.data.size))
        {
            return FAILED;
        }
    }
    m_incoming.Pop();
    return SUCCESS;
}

Result COMA::RootDirectory::DoOutgoing()
{
    assert(!m_outgoing.Empty());
    Message* msg = m_outgoing.Front();
    if (!m_prev.Send(msg))
    {
        return FAILED;
    }
    m_outgoing.Pop();
    return SUCCESS;
}

COMA::RootDirectory::RootDirectory(const std::string& name, COMA& parent, VirtualMemory& memory, size_t numCaches, const Config& config) :
    Simulator::Object(name, parent),
    COMA::Object(name, parent),
    DirectoryBottom(name, parent),
    p_lines   (*this, "p_lines"),
    m_lineSize(config.getInteger<size_t>("CacheLineSize",           64)),
    m_assoc   (config.getInteger<size_t>("COMACacheAssociativity",   4) * numCaches),
    m_sets    (config.getInteger<size_t>("COMACacheNumSets",       128)),
    m_numCaches(numCaches),
    m_incoming(*parent.GetKernel(), INFINITE),
    m_outgoing(*parent.GetKernel(), INFINITE),

    p_InPrevBottom ("bottom-incoming-prev", delegate::create<RootDirectory, &RootDirectory::DoInPrevBottom >(*this)),
    p_InNextBottom ("bottom-incoming-next", delegate::create<RootDirectory, &RootDirectory::DoInNextBottom >(*this)),
    p_OutNextBottom("bottom-outgoing-next", delegate::create<RootDirectory, &RootDirectory::DoOutNextBottom>(*this)),
    p_OutPrevBottom("bottom-outgoing-prev", delegate::create<RootDirectory, &RootDirectory::DoOutPrevBottom>(*this)),
    p_Incoming     ("incoming",             delegate::create<RootDirectory, &RootDirectory::DoIncoming     >(*this)),
    p_Outgoing     ("outgoing",             delegate::create<RootDirectory, &RootDirectory::DoOutgoing     >(*this))
{
    assert(m_lineSize <= MAX_MEMORY_OPERATION_SIZE);
    
    // Create the cache lines
    // We need as many cache lines in the directory to cover all caches below it
    m_lines.resize(m_assoc * m_sets);
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        m_lines[i].state = LINE_EMPTY;
    }

    m_prev.incoming.Sensitive(p_InPrevBottom);
    m_next.incoming.Sensitive(p_InNextBottom);
    m_next.outgoing.Sensitive(p_OutNextBottom);
    m_prev.outgoing.Sensitive(p_OutPrevBottom);
    m_incoming.Sensitive(p_Incoming);
    m_outgoing.Sensitive(p_Outgoing);
    
    p_lines.AddProcess(p_InPrevBottom);
    p_lines.AddProcess(p_InNextBottom);
    
    m_next.arbitrator.AddProcess(p_InPrevBottom);
    m_prev.arbitrator.AddProcess(p_InPrevBottom);
    m_prev.arbitrator.AddProcess(p_InNextBottom);
    m_prev.arbitrator.AddProcess(p_Outgoing);

    m_memory = new DDRChannel("ddr", *this, memory);
}

COMA::RootDirectory::~RootDirectory()
{
    delete m_memory;
}

void COMA::RootDirectory::Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const
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

void COMA::RootDirectory::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "buffers")
    {
        // Read the buffers
        out << endl << "Interface to memory:" << endl << endl;
        out << "+--------------------+-------+\n";
        out << "|       Address      | Type  |\n";
        out << "+--------------------+-------+\n";
        for (Buffer<MemRequest>::const_iterator p = m_incoming.begin(); p != m_incoming.end(); ++p)
        {
            out << "| 0x" << hex << setfill('0') << setw(16) << p->address << " | "
                << (p->data.size == 0 ? "Read " : "Write") << " |"
                << endl;
        }
        out << "+--------------------+\n";

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
            if (line.state == LINE_EMPTY) {
                out << "                    ";
            } else {
                out << hex << "0x" << setw(16) << setfill('0') << (line.tag * m_sets + set) * m_lineSize;
                if (line.state == LINE_LOADING) {
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
