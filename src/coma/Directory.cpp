#include "Directory.h"
#include "../config.h"
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

COMA::DirectoryTop::DirectoryTop(const std::string& name, COMA& parent, Clock& clock, const Config& config)
    : Simulator::Object(name, parent),
    // COMA::Object(name, parent),
    Node(name, parent, clock, config)
{
}

COMA::DirectoryBottom::DirectoryBottom(const std::string& name, COMA& parent, Clock& clock, const Config& config)
    : Simulator::Object(name, parent),
    // COMA::Object(name, parent),
    Node(name, parent, clock, config)
{
}

bool COMA::Directory::IsBelow(CacheID id) const
{
    return (id >= m_firstCache) && (id <= m_lastCache);
}

// Performs a lookup in this directory's table to see whether
// the wanted address exists in the ring below this directory.
COMA::Directory::Line* COMA::Directory::FindLine(MemAddr address)
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

// Performs a lookup in this directory's table to see whether
// the wanted address exists in the ring below this directory.
const COMA::Directory::Line* COMA::Directory::FindLine(MemAddr address) const
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

// Marks the specified address as present in the directory
COMA::Directory::Line* COMA::Directory::AllocateLine(MemAddr address)
{
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line* line = &m_lines[set + i];
        if (!line->valid)
        {
            COMMIT
            {                
                line->valid  = true;
                line->tag    = tag;
                line->tokens = 0;
            }
            return line;
        }
    }
    assert(false);
    return NULL;
}

bool COMA::Directory::OnMessageReceivedBottom(Message* msg)
{
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
            Line* line = FindLine(msg->address);
            assert(line != NULL);
            assert(line->tokens >= msg->tokens);
            
            COMMIT
            {
                line->tokens -= msg->tokens;
                if (line->tokens == 0)
                {
                    // No more tokens left; clear the line too
                    line->valid = false;
                }
            }
            break;
        }
            
        case Message::REQUEST:
        case Message::REQUEST_DATA:
        case Message::UPDATE:
            break;
            
        default:
            assert(false);
            break;
        }
    }
    
    // We can stop ignoring it now
    COMMIT{ msg->ignore = false; }
    
    // Put the message on the higher-level ring
    if (!m_top.SendMessage(msg, MINSPACE_FORWARD))
    {
        DeadlockWrite("Unable to buffer request for next node on top ring");
        return false;
    }

    return true;
}

bool COMA::Directory::OnMessageReceivedTop(Message* msg)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to get access to lines");
        return false;
    }
    
    // See if a cache below this directory has the line
    Line* line = NULL;
    switch (msg->type)
    {
    case Message::REQUEST:
    case Message::REQUEST_DATA:
    case Message::UPDATE:
        line = FindLine(msg->address);
        break;

    case Message::REQUEST_DATA_TOKEN:
        if (IsBelow(msg->sender))
        {
            // This directory contains the sender cache.
            // In case the line doesn't exist yet, allocate it.
            line = FindLine(msg->address);
            if (line == NULL)
            {
                line = AllocateLine(msg->address);
            }
            
            // We now have more tokens in this ring
            COMMIT{ line->tokens += msg->tokens; }
        }
        break;
        
    case Message::EVICTION:
        // Evicts are always forwarded
        break;
        
    default:
        assert(false);
        break;
    }
    
    if (line == NULL)
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

Result COMA::Directory::DoInBottom()
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

Result COMA::Directory::DoInTop()
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

COMA::Directory::Directory(const std::string& name, COMA& parent, Clock& clock, CacheID firstCache, CacheID lastCache, const Config& config) :
    Simulator::Object(name, parent),
    COMA::Object(name, parent),
    m_bottom(name + ".bottom", parent, clock, config),
    m_top(name + ".top", parent, clock, config),
    p_lines     (*this, clock, "p_lines"),
    m_lineSize  (config.getInteger<size_t>("CacheLineSize",           64)),
    m_assoc     (config.getInteger<size_t>("L2CacheAssociativity",     4) * (lastCache - firstCache + 1)),
    m_sets      (config.getInteger<size_t>("L2CacheNumSets",         128)),
    m_firstCache(firstCache),
    m_lastCache (lastCache),
    p_InBottom  ("bottom_incoming", delegate::create<Directory, &Directory::DoInBottom >(*this)),
    p_InTop     ("top_incoming",    delegate::create<Directory, &Directory::DoInTop    >(*this))
{
    // Create the cache lines
    // We need as many cache lines in a directory to cover all caches below it
    m_lines.resize(m_assoc * m_sets);
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        Line& line = m_lines[i];
        line.valid = false;
    }

    m_bottom.m_incoming.Sensitive(p_InBottom);
    m_top.m_incoming.Sensitive(p_InTop);
    
    p_lines.AddProcess(p_InTop);
    p_lines.AddProcess(p_InBottom);   
}

void COMA::Directory::Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const
{
    out <<
    "The Directory in a COMA system is connected via other nodes in the COMA\n"
    "system via a ring network.\n\n"
    "Supported operations:\n"
    "- read <component>\n"
    "  Reads and displays the directory lines, and global information such as hit-rate\n"
    "  and directory configuration.\n"
    "- read <component> buffers\n"
    "  Reads and displays the buffers in the directory\n";
}

void COMA::Directory::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
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

    out << "Cache type:  ";
    if (m_assoc == 1) {
        out << "Direct mapped" << endl;
    } else if (m_assoc == m_lines.size()) {
        out << "Fully associative" << endl;
    } else {
        out << dec << m_assoc << "-way set associative" << endl;
    }
    out << "Cache range: " << m_firstCache << " - " << m_lastCache << endl;
    out << endl;
    
    // No more than 4 columns per row and at most 1 set per row
    const size_t width = std::min<size_t>(m_assoc, 4);

    out << "Set |";
    for (size_t i = 0; i < width; ++i) out << "       Address      | Tokens |";
    out << endl << "----";
    std::string seperator = "+";
    for (size_t i = 0; i < width; ++i) seperator += "--------------------+--------+";
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
            if (line.valid) {
                out << hex << "0x" << setw(16) << setfill('0') << (line.tag * m_sets + set) * m_lineSize << " | "
                    << dec << setfill(' ') << setw(6) << line.tokens;
            } else {
                out << "                   |       ";
            }
            out << " | ";
        }
        out << endl
            << ((index + width) % m_assoc == 0 ? "----" : "    ")
            << seperator << endl;
    }
}

}
