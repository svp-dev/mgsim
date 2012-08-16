#include "Directory.h"
#include "sim/config.h"
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

ZLCOMA::DirectoryTop::DirectoryTop(const std::string& name, ZLCOMA& parent, Clock& clock)
  : Simulator::Object(name, parent),
    Node(name, parent, clock)
{
}

ZLCOMA::DirectoryBottom::DirectoryBottom(const std::string& name, ZLCOMA& parent, Clock& clock)
  : Simulator::Object(name, parent),
    Node(name, parent, clock)
{
}

// this probably only works with current naive configuration
bool ZLCOMA::Directory::IsBelow(CacheID id) const
{
    return (id >= m_firstCache) && (id <= m_lastCache);
}

ZLCOMA::Directory::Line* ZLCOMA::Directory::FindLine(MemAddr address)
{
    MemAddr tag;
    size_t setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

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

const ZLCOMA::Directory::Line* ZLCOMA::Directory::FindLine(MemAddr address) const
{
    MemAddr tag;
    size_t setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

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

ZLCOMA::Directory::Line* ZLCOMA::Directory::AllocateLine(MemAddr address)
{
    MemAddr tag;
    size_t setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line* line = &m_lines[set + i];
        if (!line->valid)
        {
            line->tag    = tag;
            line->valid  = true;
            line->tokens = 0;
            return line;
        }
    }
    assert(false);
    return NULL;
}

bool ZLCOMA::Directory::OnMessageReceivedBottom(Message* req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to get access to lines");
        return false;
    }

    if (!req->ignore)
    {
        Line* line = FindLine(req->address);
        switch(req->type)
        {
        case Message::READ:
            assert(req->transient == false);
            break;

        case Message::ACQUIRE_TOKENS:
            break;        
        
        case Message::EVICTION:
            assert(req->tokens > 0);            // Should have tokens
            assert(req->transient == false);    // Evictions never have transient tokens
            assert(IsBelow(req->source));       // Evictions never go down into a group
            break;
        
        case Message::LOCALDIR_NOTIFICATION:
            // Transient tokens have been made permanent in this group
            assert(line != NULL);

            // Add the tokens and terminate request
            COMMIT
            {
                line->tokens += req->tokens;
                delete req;
            }
            return true;

        default:
            assert(false);
            break;
        }
    
        if (req->tokens > 0 && !req->transient)
        {
            // If we're losing tokens, we should have the line
            assert(line != NULL);
    
            // There's tokens leaving the group
            COMMIT
            {
                line->tokens -= req->tokens;
                if (line->tokens == 0)
                {
                    // Clear line
                    line->valid = false;
                }
            }
        }
    }
            
    // We can stop ignoring it now
    COMMIT{ req->ignore = false; }
    
    // Forward request onto upper ring
    if (!m_top.SendMessage(req, MINSPACE_FORWARD))
    {
        DeadlockWrite("Unable to buffer request for next node on top ring");
        return false;
    }
    return true;
}


bool ZLCOMA::Directory::OnMessageReceivedTop(Message* req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to get access to lines");
        return false;
    }

    Line* line = NULL;
    switch (req->type)
    {
    case Message::READ:
        assert(req->transient == false);
        line = FindLine(req->address);
        if (IsBelow(req->source) && line == NULL)
        {
            line = AllocateLine(req->address);
        }    
        break;

    case Message::ACQUIRE_TOKENS:
        line = FindLine(req->address);
        if (IsBelow(req->source) && line == NULL)
        {
            line = AllocateLine(req->address);
        }   
        break;
        
    case Message::EVICTION:
        assert(req->tokens > 0);
        assert(req->transient == false);
        // Evictions are always forwarded
        break;

    default:
        assert(false);
        break;
    }

    if (IsBelow(req->source) && !req->transient)
    {
        assert(line != NULL);
        COMMIT{ line->tokens += req->tokens; }
    }
    
    if (line == NULL)
    {
        // Forward request onto upper ring
        if (!m_top.SendMessage(req, MINSPACE_SHORTCUT))
        {
            COMMIT{ req->ignore = true; }
            if (!m_bottom.SendMessage(req, MINSPACE_FORWARD))
            {
                DeadlockWrite("Unable to buffer request for next node on top ring");
                return false;
            }
        }
    }
    else
    {
        if (!m_bottom.SendMessage(req, MINSPACE_FORWARD))
        {
            DeadlockWrite("Unable to buffer request for next node on bottom ring");
            return false;
        }
    }
    return true;
}

Result ZLCOMA::Directory::DoInBottom()
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

Result ZLCOMA::Directory::DoInTop()
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

ZLCOMA::Directory::Directory(const std::string& name, ZLCOMA& parent, Clock& clock, CacheID firstCache, Config& config) :
    Simulator::Object(name, parent),
    ZLCOMA::Object(name, parent),
    m_bottom(name + ".bottom", parent, clock),
    m_top(name + ".top", parent, clock),
    m_selector  (parent.GetBankSelector()),
    p_lines     (*this, clock, "p_lines"),
    m_lineSize  (config.getValue<size_t>("CacheLineSize")),
    m_assoc     (config.getValue<size_t>(parent, "L2CacheAssociativity") * config.getValue<size_t>(parent, "NumL2CachesPerRing")),
    m_sets      (m_selector.GetNumBanks()),
    m_firstCache(firstCache),
    m_lastCache (firstCache + config.getValue<size_t>(parent, "NumL2CachesPerRing") - 1),
    p_InBottom  (*this, "bottom_incoming", delegate::create<Directory, &Directory::DoInBottom >(*this)),
    p_InTop     (*this, "top_incoming",    delegate::create<Directory, &Directory::DoInTop    >(*this))
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

    p_InBottom.SetStorageTraces(m_top.GetOutgoingTrace());
    p_InTop.SetStorageTraces((m_top.GetOutgoingTrace() * opt(m_bottom.GetOutgoingTrace())) ^ m_bottom.GetOutgoingTrace());

    config.registerObject(m_top, "dt");
    config.registerProperty(m_top, "freq", clock.GetFrequency());
    config.registerObject(m_bottom, "db");
    config.registerProperty(m_bottom, "freq", clock.GetFrequency());

    config.registerBidiRelation(m_bottom, m_top, "dir");
}

void ZLCOMA::Directory::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*args*/) const
{
    out <<
    "The Directory in a COMA system is connected via other nodes in the COMA\n"
    "system via a ring network.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Reads and displays the directory lines, and global information such as hit-rate\n"
    "  and directory configuration.\n"
    "- inspect <component> buffers\n"
    "  Reads and displays the buffers in the directory\n";
}

void ZLCOMA::Directory::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
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
                out << hex << "0x" << setw(16) << setfill('0') << m_selector.Unmap(line.tag, set) * m_lineSize << " | "
                    << dec << setfill(' ') << setw(6) << 0;//line.tokens;
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
