#include "FamilyTable.h"
#include "Processor.h"
#include "config.h"
#include "range.h"
#include "symtable.h"
#include <cassert>
#include <iomanip>
using namespace std;

namespace Simulator
{

FamilyTable::FamilyTable(const std::string& name, Processor& parent, const Config& config)
:   Object(name, parent),
    m_parent(parent),
    m_families(config.getInteger<size_t>("NumFamilies", 8))
{
    for (size_t i = 0; i < m_families.size(); ++i)
    {
        m_families[i].state = FST_EMPTY;
    }
    
    m_free[CONTEXT_EXCLUSIVE] = 1;
    m_free[CONTEXT_NORMAL]    = m_families.size() - 1;
    m_free[CONTEXT_RESERVED]  = 0;
}

bool FamilyTable::IsEmpty() const
{
    FSize free = 0;
    for (int i = 0; i < NUM_CONTEXT_TYPES; ++i)
    {
        free += m_free[i];
    }
    return free == m_families.size();
}

// Checks that all internal administration is sane
void FamilyTable::CheckStateSanity() const
{
#ifndef NDEBUG
    size_t used = 0;
    for (size_t i = 0; i < m_families.size(); ++i)
    {
        if (m_families[i].state != FST_EMPTY)
        {
            used++;
        }
    }
    
    // At most one exclusive context free
    assert(m_free[CONTEXT_EXCLUSIVE] <= 1);
    
    // Exclusive context is only free if the entry isn't used
    assert((m_free[CONTEXT_EXCLUSIVE] == 1) ^ (m_families.back().state != FST_EMPTY));
    
    // All counts must add up
    assert(m_free[CONTEXT_NORMAL] + m_free[CONTEXT_RESERVED] + m_free[CONTEXT_EXCLUSIVE] + used == m_families.size());
#endif
}

LFID FamilyTable::AllocateFamily(ContextType context)
{
    // Check that we're in a sane state
    CheckStateSanity();
    
    LFID fid = INVALID_LFID;
    if (m_free[context] > 0)
    {
        if (context == CONTEXT_EXCLUSIVE)
        {
            fid = m_families.size() - 1;
        }
        else
        {
            // There is at least one free entry for this context
            // Do an associative lookup; do not consider the exclusive family entry
            for (size_t i = 0; i < m_families.size() - 1; ++i)
            {
                if (m_families[i].state == FST_EMPTY)
                {
                    fid = i;
                    break;
                }
            }
        }
        
        // We've allocated a family entry
        assert(fid != INVALID_LFID);
        assert(m_families[fid].state == FST_EMPTY);
        COMMIT
        {
            Family& family = m_families[fid];
            family.state = FST_ALLOCATED;
            m_free[context]--;
        }
    }
    return fid;
}

// Reserves an entry for a group create that will come later
void FamilyTable::ReserveFamily()
{
    // Check that we're in a sane state
    CheckStateSanity();

    // Move a free entry from normal to reserved
    assert(m_free[CONTEXT_NORMAL] > 0);
    COMMIT{
        m_free[CONTEXT_NORMAL]--;
        m_free[CONTEXT_RESERVED]++;
    }
}

FSize FamilyTable::GetNumFreeFamilies() const
{
    // Check that we're in a sane state
    assert(m_free[CONTEXT_NORMAL] + m_free[CONTEXT_RESERVED] + m_free[CONTEXT_EXCLUSIVE] <= m_families.size());

    // We do not count reserved or exclusive entries to general free entries
    return m_free[CONTEXT_NORMAL];
}

void FamilyTable::FreeFamily(LFID fid, ContextType context)
{
    assert(fid != INVALID_LFID);
    
    // Check that we're in a sane state
    CheckStateSanity();

    COMMIT
    {
        m_families[fid].state = FST_EMPTY;
        m_free[context]++;
    }
}

void FamilyTable::Cmd_Help(ostream& out, const vector<string>& /* arguments */) const
{
    out <<
    "The Family Table is the storage area in a processor that stores all family's\n"
    "information. It contains information about the family's state, threads and \n"
    "much more.\n\n"
    "Supported operations:\n"
    "- read <component> [range]\n"
    "  Reads and displays the used family table entries. Note that not all\n"
    "  information is displayed; only the most important data.\n"
    "  An optional range argument can be given to only read those families. The\n"
    "  range is a comma-seperated list of family ranges. Example ranges:\n"
    "  \"1\", \"1-4,15,7-8\", \"all\"\n";
}

// Read the global and local family table
void FamilyTable::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    static const char* const FamilyStates[] = {
        "", "ALLOCATED", "CREATE QUEUED", "CREATING", "ACTIVE", "KILLED"
    };

    // Read the range
    bool show_counts = false;
    set<LFID> fids;
    if (!arguments.empty()) {
        fids = parse_range<LFID>(arguments[0], 0, m_families.size());
    } else {
        show_counts = true;
        for (LFID i = 0; i < m_families.size(); ++i) {
            if (m_families[i].state != FST_EMPTY) {
                fids.insert(i);
            }
        }
    }

    if (fids.empty())
    {
        out << "No families selected" << endl;
    }
    else
    {
        out << "    |     Initial PC     |   Allocated    | P/A/D/Rd | Prev | Next |    Sync    |     Capability     | State         | Symbol" << endl;
        out << "----+--------------------+----------------+----------+------+------+------------+--------------------+---------------+--------" << endl;
        for (set<LFID>::const_iterator p = fids.begin(); p != fids.end(); ++p)
        {
            const Family& family = m_families[*p];

            out << dec << right << setw(3) << setfill(' ') << *p << " | ";
            if (family.state == FST_EMPTY)
            {
                out << "                   |                |          |      |      |            |                    |               |";
            }
            else
            {
                if (family.state == FST_ALLOCATED)
                {
                    out << "        -          |       -        |     -    | ";
                }
                else
                {
                    out << hex << setw(18) << showbase << family.pc << " | " << dec;
                    if (family.state == FST_CREATE_QUEUED || family.state == FST_CREATING) {
                        out << "      -        |    -    ";
                    } else {
                        out << setw(3) << family.dependencies.numThreadsAllocated << "/"
                            << setw(3) << family.physBlockSize << " ("
                            << setw(4) << family.virtBlockSize << ")"
                            << " | "
                            << noboolalpha
                            << !family.dependencies.prevSynchronized << "/"
                            << !family.dependencies.allocationDone << "/"
                            << !family.dependencies.detached << "/"
                            << setw(2) << family.dependencies.numPendingReads
                            << right;
                    }
                    out << " | ";
                }

                // Print prev and next FIDs
                if (family.state == FST_ALLOCATED || family.state == FST_CREATE_QUEUED) {
                    out << "  -  |   - ";
                } else {
                    out << setfill('0') << right;
                    if (family.link_prev != INVALID_LFID) {
                        out << " F" << setw(2) << family.link_prev;
                    } else {
                        out << "  - ";
                    }
                    out << " | ";
                    if (family.link_next != INVALID_LFID) {
                        out << " F" << setw(2) << family.link_next;
                    } else {
                        out << "  - ";
                    }
                }
            
                // Print sync reg
                if (family.sync.pid != INVALID_GPID) {
                    out << right << setfill('0') << noshowbase
                        << " | R" << setw(4) << hex << family.sync.reg
                        << "@P" << setw(3) << dec << family.sync.pid;
                } else {
                    out << " |      -    ";
                }
            
                out << " | 0x" << right << setw(16) << setfill('0') << hex << noshowbase << family.capability
                    << " | " << left  << setw(13) << setfill(' ') << FamilyStates[family.state]
                    << " | ";
            
                if (family.state != FST_ALLOCATED)
                {
                    out << GetKernel()->GetSymbolTable()[family.pc];
                }
            }
            out << endl;
        }
    }
    
    if (show_counts)
    {
        out << endl
            << "Free families: " << dec
            << m_free[CONTEXT_NORMAL] << " normal, "
            << m_free[CONTEXT_RESERVED] << " reserved, "
            << m_free[CONTEXT_EXCLUSIVE] << " exclusive"
            << endl;
    }
}

}
