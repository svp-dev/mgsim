#include "Directory.h"
#include "../config.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <iomanip>
using namespace std;

namespace Simulator
{

COMA::DirectoryTop::DirectoryTop(const std::string& name, COMA& parent)
  : Simulator::Object(name, parent),
    COMA::Object(name, parent),
    Node(name, parent)
{
}

COMA::DirectoryBottom::DirectoryBottom(const std::string& name, COMA& parent)
  : Simulator::Object(name, parent),
    COMA::Object(name, parent),
    Node(name, parent)
{
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
                line->valid = true;
                line->tag   = tag;
            }
            return line;
        }
    }
    assert(false);
    return NULL;
}

bool COMA::Directory::OnRequestReceivedBottom(Message* msg)
{
    // Only messages from below need to do anything with the directory
    if (msg->hops < m_numCaches)
    {
        if (!p_lines.Invoke())
        {
            DeadlockWrite("Unable to get access to lines");
            return false;
        }
    
        Line* line = FindLine(msg->address);
        switch (msg->type)
        {
        case Message::REQUEST_READ:
            // Only read requests from below can allocate a line
            if (line == NULL)
            {
                AllocateLine(msg->address);
            }
            break;
        
        case Message::REQUEST_EVICT:
            // If the eviction came from below and is the last one, clear the dir line
            if (msg->hops + msg->tokens >= m_numCaches)
            {
                assert(line != NULL);
                COMMIT{ line->valid = false; }
            }
            break;
            
        case Message::REQUEST_KILL_TOKENS:
            break;
        
        case Message::REQUEST_UPDATE:
            break;
            
        default:
            assert(false);
            break;
        }
    }
        
    // Put the message on the higher-level ring
    if (!DirectoryTop::m_next.Send(msg))
    {
        DeadlockWrite("Unable to buffer request for next node on top ring");
        return false;
    }

    return true;
}

bool COMA::Directory::OnRequestReceivedTop(Message* msg)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to get access to lines");
        return false;
    }
    
    // See if a cache below this directory has the line
    Line* line = FindLine(msg->address);
    if (line == NULL)
    {
        // Miss, just forward the request on the upper ring
        COMMIT{ msg->hops += m_numCaches; }
        if (!DirectoryTop::m_next.Send(msg))
        {
            DeadlockWrite("Unable to buffer request for next node on top ring");
            return false;
        }
    }
    else
    {
        // We have the line; put the request on the lower ring    
        if (!DirectoryBottom::m_next.Send(msg))
        {
            DeadlockWrite("Unable to buffer request for next node on bottom ring");
            return false;
        }
    }
    
    return true;
}

bool COMA::Directory::OnResponseReceivedTop(Message* msg)
{
    // Check if the destination is in our ring
    if (msg->hops < m_numCaches)
    {
        // Yes, put the reply on the lower ring
        if (!DirectoryBottom::m_prev.Send(msg))
        {
            DeadlockWrite("Unable to buffer response for previous node on bottom ring");
            return false;
        }
    }
    else
    {
        // No, just forward it, and adjust hop count
        COMMIT{ msg->hops -= m_numCaches; }
        if (!DirectoryTop::m_prev.Send(msg))
        {
            DeadlockWrite("Unable to buffer response for previous node on top ring");
            return false;
        }
    }
    return true;
}

bool COMA::Directory::OnResponseReceivedBottom(Message* msg)
{
    // Forward the message on the top ring
    if (!DirectoryTop::m_prev.Send(msg))
    {
        DeadlockWrite("Unable to buffer response for previous node on top ring");
        return false;
    }
    return true;
}

Result COMA::Directory::DoInPrevBottom()
{
    // Handle incoming message on bottom ring from previous node
    assert(!DirectoryBottom::m_prev.incoming.Empty());
    if (!OnRequestReceivedBottom(DirectoryBottom::m_prev.incoming.Front()))
    {
        return FAILED;
    }
    DirectoryBottom::m_prev.incoming.Pop();
    return SUCCESS;
}

Result COMA::Directory::DoInNextBottom()
{
    // Handle incoming message on bottom ring from next node
    assert(!DirectoryBottom::m_next.incoming.Empty());
    if (!OnResponseReceivedBottom(DirectoryBottom::m_next.incoming.Front()))
    {
        return FAILED;
    }
    DirectoryBottom::m_next.incoming.Pop();
    return SUCCESS;
}    

Result COMA::Directory::DoOutNextBottom()
{
    // Send outgoing message on bottom ring to next node
    assert(!DirectoryBottom::m_next.outgoing.Empty());
    if (!DirectoryBottom::m_next.node->ReceiveMessagePrev(DirectoryBottom::m_next.outgoing.Front()))
    {
        DeadlockWrite("Unable to send request to next node on bottom ring");
        return FAILED;
    }
    DirectoryBottom::m_next.outgoing.Pop();
    return SUCCESS;
}

Result COMA::Directory::DoOutPrevBottom()
{
    // Send outgoing message on bottom ring to previous node
    assert(!DirectoryBottom::m_prev.outgoing.Empty());
    if (!DirectoryBottom::m_prev.node->ReceiveMessageNext(DirectoryBottom::m_prev.outgoing.Front()))
    {
        DeadlockWrite("Unable to send response to previous node on bottom ring");
        return FAILED;
    }
    DirectoryBottom::m_prev.outgoing.Pop();
    return SUCCESS;
}

Result COMA::Directory::DoInPrevTop()
{
    // Handle incoming message on top ring from previous node
    assert(!DirectoryTop::m_prev.incoming.Empty());
    if (!OnRequestReceivedTop(DirectoryTop::m_prev.incoming.Front()))
    {
        return FAILED;
    }
    DirectoryTop::m_prev.incoming.Pop();
    return SUCCESS;
}

Result COMA::Directory::DoInNextTop()
{
    // Handle incoming message on top ring from next node
    assert(!DirectoryTop::m_next.incoming.Empty());
    if (!OnResponseReceivedTop(DirectoryTop::m_next.incoming.Front()))
    {
        return FAILED;
    }
    DirectoryTop::m_next.incoming.Pop();
    return SUCCESS;
}
        
Result COMA::Directory::DoOutNextTop()
{
    // Send outgoing message on top ring to next node
    assert(!DirectoryTop::m_next.outgoing.Empty());
    if (!DirectoryTop::m_next.node->ReceiveMessagePrev(DirectoryTop::m_next.outgoing.Front()))
    {
        DeadlockWrite("Unable to send request to next node on top ring");
        return FAILED;
    }
    DirectoryTop::m_next.outgoing.Pop();
    return SUCCESS;
}

Result COMA::Directory::DoOutPrevTop()
{
    // Send outgoing message on top ring to previous node
    assert(!DirectoryTop::m_prev.outgoing.Empty());
    if (!DirectoryTop::m_prev.node->ReceiveMessageNext(DirectoryTop::m_prev.outgoing.Front()))
    {
        DeadlockWrite("Unable to send response to previous node on top ring");
        return FAILED;
    }
    DirectoryTop::m_prev.outgoing.Pop();
    return SUCCESS;
}

COMA::Directory::Directory(const std::string& name, COMA& parent, bool top_level, size_t numCaches, const Config& config) :
    Simulator::Object(name, parent),
    COMA::Object(name, parent),
    DirectoryBottom(name, parent),
    DirectoryTop(name, parent),
    p_lines    (*this, "p_lines"),
    m_lineSize (config.getInteger<size_t>("CacheLineSize",           64)),
    m_assoc    (config.getInteger<size_t>("COMACacheAssociativity",   4) * numCaches),
    m_sets     (config.getInteger<size_t>("COMACacheNumSets",       128)),
    m_topLevel (top_level),
    m_numCaches(numCaches),

    p_InPrevBottom ("bottom-incoming-prev", delegate::create<Directory, &Directory::DoInPrevBottom >(*this)),
    p_InNextBottom ("bottom-incoming-next", delegate::create<Directory, &Directory::DoInNextBottom >(*this)),
    p_OutNextBottom("bottom-outgoing-next", delegate::create<Directory, &Directory::DoOutNextBottom>(*this)),
    p_OutPrevBottom("bottom-outgoing-prev", delegate::create<Directory, &Directory::DoOutPrevBottom>(*this)),
    p_InPrevTop    ("top-incoming-prev",    delegate::create<Directory, &Directory::DoInPrevTop    >(*this)),
    p_InNextTop    ("top-incoming-next",    delegate::create<Directory, &Directory::DoInNextTop    >(*this)),
    p_OutNextTop   ("top-outgoing-next",    delegate::create<Directory, &Directory::DoOutNextTop   >(*this)),
    p_OutPrevTop   ("top-outgoing-prev",    delegate::create<Directory, &Directory::DoOutPrevTop   >(*this))
{
    // Create the cache lines
    // We need as many cache lines in a directory to cover all caches below it
    m_lines.resize(m_assoc * m_sets);
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        Line& line = m_lines[i];
        line.valid = false;
    }

    DirectoryBottom::m_prev.incoming.Sensitive(p_InPrevBottom);
    DirectoryBottom::m_next.incoming.Sensitive(p_InNextBottom);
    DirectoryBottom::m_next.outgoing.Sensitive(p_OutNextBottom);
    DirectoryBottom::m_prev.outgoing.Sensitive(p_OutPrevBottom);
    DirectoryTop   ::m_prev.incoming.Sensitive(p_InPrevTop);
    DirectoryTop   ::m_next.incoming.Sensitive(p_InNextTop);
    DirectoryTop   ::m_next.outgoing.Sensitive(p_OutNextTop);
    DirectoryTop   ::m_prev.outgoing.Sensitive(p_OutPrevTop);
    
    p_lines.AddProcess(p_InPrevTop);
    p_lines.AddProcess(p_InPrevBottom);
    p_lines.AddProcess(p_InNextTop);
    p_lines.AddProcess(p_InNextBottom);
    
    DirectoryBottom::m_next.arbitrator.AddProcess(p_InPrevTop);    // Forward request from top onto on bottom ring
    DirectoryBottom::m_next.arbitrator.AddProcess(p_InPrevBottom); // Forward request on bottom ring
    DirectoryBottom::m_prev.arbitrator.AddProcess(p_InNextTop);    // Forward response from top onto bottom ring
    DirectoryTop   ::m_next.arbitrator.AddProcess(p_InPrevTop);    // Forward request on top ring
    DirectoryTop   ::m_next.arbitrator.AddProcess(p_InPrevBottom); // Forward request from bottom onto top ring
    DirectoryTop   ::m_prev.arbitrator.AddProcess(p_InNextTop);    // Forward response on top ring
    DirectoryTop   ::m_prev.arbitrator.AddProcess(p_InNextBottom); // Forward response from bottom onto top ring
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
        out << endl << "Interface with next top node:" << endl << endl;
        DirectoryTop::m_next.Print(out);

        out << endl << "Interface with previous top node:" << endl << endl;
        DirectoryTop::m_prev.Print(out);

        out << endl << "Interface with next bottom node:" << endl << endl;
        DirectoryBottom::m_next.Print(out);

        out << endl << "Interface with previous bottom node:" << endl << endl;
        DirectoryBottom::m_prev.Print(out);

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
    for (size_t i = 0; i < width; ++i) out << "       Address      |";
    out << endl << "----";
    std::string seperator = "+";
    for (size_t i = 0; i < width; ++i) seperator += "--------------------+";
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
                out << hex << "0x" << setw(16) << setfill('0') << (line.tag * m_sets + set) * m_lineSize;
            } else {
                out << "                  ";
            }
            out << " | ";
        }
        out << endl
            << ((index + width) % m_assoc == 0 ? "----" : "    ")
            << seperator << endl;
    }
}

}
