#include "Directory.h"
#include <sim/config.h>

#include <cassert>
#include <cstring>
#include <cstdio>
#include <iomanip>
using namespace std;

namespace Simulator
{

// When we shortcut a message over the ring, we want at least one slots
// available in the buffer to avoid deadlocking the ring network. This
// is not necessary for forwarding messages onto the lower ring.
static const size_t MINSPACE_SHORTCUT = 2;
static const size_t MINSPACE_FORWARD  = 1;

CDMA::DirectoryTop::DirectoryTop(const std::string& name, CDMA& parent, Clock& clock, size_t& numLines, Config& config)
    : Simulator::Object(name, parent),
      Node(name, parent, clock, (NodeID)-1, config),
      m_numLines(numLines)
{
}

size_t CDMA::DirectoryTop::GetNumLines() const
{
    return m_numLines;
}

CDMA::DirectoryBottom::DirectoryBottom(const std::string& name, CDMA& parent, Clock& clock, Config& config)
    : Simulator::Object(name, parent),
      Node(name, parent, clock, (NodeID)-1, config)
{
}

bool CDMA::Directory::IsBelow(NodeID id) const
{
    return (id >= m_firstNode) && (id <= m_lastNode);
}

// Performs a lookup in this directory's table to see whether
// the wanted address exists in the ring below this directory.
size_t* CDMA::Directory::FindLine(MemAddr address)
{
    auto linei = m_dir.find(address);
    if (linei == m_dir.end())
        return NULL;
    return &linei->second;
}

// Marks the specified address as present in the directory
static size_t pseudoline;
size_t* CDMA::Directory::AllocateLine(MemAddr address)
{
    size_t *line = &pseudoline;
    COMMIT {
        auto np = m_dir.insert(make_pair(address, 0));
        assert(np.second == true);
        assert(m_dir.size() <= m_maxNumLines);

        line = &(np.first->second);
    }
    return line;
}

bool CDMA::Directory::OnMessageReceivedBottom(Message* msg)
{
#if 1 /* set to 0 to attempt to flatten the CDMA ring, ie remove the shortcut across (DEBUG FEATURE ONLY) -- also check below */

    // We need to grab p_line because it arbitrates access to the outgoing
    // buffer on the top ring as well.
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to get access to lines");
        return false;
    }

    if (!msg->ignore)
    {
        switch (msg->type)
        {
        case Message::EVICTION:
            // Evictions always come from below since they do not go down into a ring.
            // (Except for deadlock avoidance, but then the ignore flag is set).
            assert(IsBelow(msg->sender));

        case Message::REQUEST_DATA_TOKEN:
        {
            // Reduce the token count in the dir line
            size_t* tokens = FindLine(msg->address);
            assert(tokens != NULL);
            assert(*tokens >= msg->tokens);

            COMMIT
            {
                *tokens -= msg->tokens;
                if (*tokens == 0)
                {
                    // No more tokens left; clear the line too
                    m_dir.erase(msg->address);
                }
            }
            break;
        }

        case Message::REQUEST:
        case Message::REQUEST_DATA:
        case Message::UPDATE:
            break;

        default:
            UNREACHABLE;
            break;
        }
    }

    // We can stop ignoring it now
    COMMIT{ msg->ignore = false; }
#endif

    // Put the message on the higher-level ring
    if (!m_top.SendMessage(msg, MINSPACE_FORWARD))
    {
        DeadlockWrite("Unable to buffer request for next node on top ring");
        return false;
    }

    return true;
}

bool CDMA::Directory::OnMessageReceivedTop(Message* msg)
{
#if 1 /* set to 0 to attempt to flatten the CDMA ring, ie remove the shortcut across (DEBUG FEATURE ONLY) -- also check above */
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to get access to lines");
        return false;
    }

    // See if a cache below this directory has the line
    size_t* tokens = NULL;
    switch (msg->type)
    {
    case Message::REQUEST:
    case Message::REQUEST_DATA:
    case Message::UPDATE:
        tokens = FindLine(msg->address);
        break;

    case Message::REQUEST_DATA_TOKEN:
        if (IsBelow(msg->sender))
        {
            // This directory contains the sender cache.
            // In case the line doesn't exist yet, allocate it.
            tokens = FindLine(msg->address);
            if (tokens == NULL)
            {
                tokens = AllocateLine(msg->address);
            }

            // We now have more tokens in this ring
            COMMIT{ *tokens += msg->tokens; }
        }
        break;

    case Message::EVICTION:
        // Evicts are always forwarded
        break;

    default:
        UNREACHABLE;
        break;
    }

    if (tokens == NULL)
    {
        // Miss, just forward the request on the upper ring
        if (!m_top.SendMessage(msg, MINSPACE_SHORTCUT))
        {
            // We can't shortcut on the top, send it the long way
            COMMIT{ msg->ignore = true; }
            if (!m_bottom.SendMessage(msg, MINSPACE_FORWARD))
            {
                DeadlockWrite("Unable to buffer request for next node on top ring");
                return false;
            }
        }
    }
    else
#endif
    {
        // We have the line; put the request on the lower ring
        if (!m_bottom.SendMessage(msg, MINSPACE_FORWARD))
        {
            DeadlockWrite("Unable to buffer request for next node on bottom ring");
            return false;
        }
    }

    return true;
}

Result CDMA::Directory::DoInBottom()
{
    // Handle incoming message on bottom ring from previous node
    assert(!m_bottom.m_incoming.Empty());
    if (!OnMessageReceivedBottom(m_bottom.m_incoming.Front()))
    {
        return FAILED;
    }
    m_bottom.m_incoming.Pop();
    return SUCCESS;
}

Result CDMA::Directory::DoInTop()
{
    // Handle incoming message on top ring from previous node
    assert(!m_top.m_incoming.Empty());
    if (!OnMessageReceivedTop(m_top.m_incoming.Front()))
    {
        return FAILED;
    }
    m_top.m_incoming.Pop();
    return SUCCESS;
}

CDMA::Directory::Directory(const std::string& name, CDMA& parent, Clock& clock, Config& config) :
    Simulator::Object(name, parent),
    CDMA::Object(name, parent),
    m_bottom    (name + ".bottom", parent, clock, config),
    m_top       (name + ".top", parent, clock, m_maxNumLines, config),
    p_lines     (clock, GetName() + ".p_lines"),
    m_dir       (),
    m_maxNumLines(0),
    m_firstNode (-1),
    m_lastNode  (-1),
    p_InBottom  (*this, "bottom-incoming", delegate::create<Directory, &Directory::DoInBottom >(*this)),
    p_InTop     (*this, "top-incoming",    delegate::create<Directory, &Directory::DoInTop    >(*this))
{

    m_bottom.m_incoming.Sensitive(p_InBottom);
    m_top.m_incoming.Sensitive(p_InTop);

    p_lines.AddProcess(p_InTop);
    p_lines.AddProcess(p_InBottom);

    p_InBottom.SetStorageTraces(m_top.GetOutgoingTrace());
    p_InTop.SetStorageTraces((m_top.GetOutgoingTrace() * opt(m_bottom.GetOutgoingTrace())) ^ m_bottom.GetOutgoingTrace());

    config.registerObject(m_top, "dt");
    config.registerProperty(m_top, "freq", clock.GetFrequency());
    config.registerObject(m_bottom, "db");
    config.registerProperty(m_bottom, "freq", clock.GetFrequency());

    config.registerBidiRelation(m_bottom, m_top, "dir");
}

void CDMA::Directory::ConnectRing(Node* first, Node* last)
{
    m_bottom.Connect(last, first);
}

void CDMA::Directory::Initialize()
{
    Node* first = m_bottom.GetPrevNode();
    Node* last = m_bottom.GetNextNode();
    assert(first->GetNextNode() == &m_bottom);
    assert(last->GetPrevNode() == &m_bottom);

    m_firstNode = first->GetNodeID();
    m_lastNode = last->GetNodeID();

    for (Node *p = first; p != &m_bottom; p = p->GetPrevNode())
    {
        // Consistency check: we required all nodes in the subring
        // to be contiguous. This may be overly restrictive; the main
        // property is that IsBelow() should be accurate and constant time.
        assert(p->GetPrevNode() == &m_bottom || p->GetPrevNode()->GetNodeID() == p->GetNodeID() + 1);
        m_maxNumLines += p->GetNumLines();
    }
}

void CDMA::Directory::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*args*/) const
{
    out <<
    "The Directory in a CDMA system is connected via other nodes in the CDMA\n"
    "system via a ring network.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Reads and displays the directory lines, and global information such as hit-rate\n"
    "  and directory configuration.\n"
    "- inspect <component> buffers\n"
    "  Reads and displays the buffers in the directory\n";
}

void CDMA::Directory::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "buffers")
    {
        // Read the buffers
        out << endl << "Top ring interface:" << endl << endl;
        m_top.Print(out);

        out << endl << "Bottom ring interface:" << endl << endl;
        m_bottom.Print(out);

        return;
    }

    out << "Max directory size: " << m_maxNumLines << endl
        << "Current directory size: " << m_dir.size() << endl
        << "Range of node IDs on lower ring: " << m_firstNode << " - " << m_lastNode << endl
        << endl;

    // No more than 4 columns per row and at most 1 set per row
    const size_t width = std::min<size_t>(m_dir.size(), 4);

    out << "Entry  |";
    for (size_t i = 0; i < width; ++i) out << "       Address      | Tokens |";
    out << endl << "-------";
    std::string separator = "+";
    for (size_t i = 0; i < width; ++i) separator += "--------------------+--------+";
    out << separator << endl;

    auto p = m_dir.begin();
    for (size_t i = 0; i < m_dir.size(); i += width)
    {
        out << setw(6) << setfill(' ') << dec << right << i << " | ";
        for (size_t j = i; j < i + width; ++j)
        {
            if (p != m_dir.end())
            {
                out << hex << "0x" << setw(16) << setfill('0') << p->first << " | "
                    << dec << setfill(' ') << setw(6) << p->second;
                p++;
            } else {
                out << "                   |       ";
            }
            out << " | ";
        }
        out << endl;
    }
    out << "-------" << separator << endl;
}

}
