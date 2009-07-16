#include "FamilyTable.h"
#include "Processor.h"
#include "config.h"
#include "range.h"
#include <cassert>
#include <iomanip>
using namespace std;

namespace Simulator
{

FamilyTable::FamilyTable(Processor& parent, const Config& config)
:   Object(&parent, &parent.GetKernel(), "families"),
    m_parent(parent),
    m_families(config.getInteger<size_t>("NumFamilies", 8)),
    m_numFamiliesUsed(0)
{
    for (size_t i = 0; i < m_families.size(); ++i)
    {
		// Deny access to empty families
		m_families[i].created     = false;
		m_families[i].parent.lpid = INVALID_LPID;
		m_families[i].parent.gpid = INVALID_GPID;
		m_families[i].next        = INVALID_LFID;
        m_families[i].state       = FST_EMPTY;
    }
}

LFID FamilyTable::AllocateFamily()
{
    // Do an associative lookup
    for (size_t i = 0; i < m_families.size(); ++i)
    {
        if (m_families[i].state == FST_EMPTY)
        {
            assert(m_numFamiliesUsed < m_families.size());
            COMMIT
            {
        		Family& family = m_families[i];
                family.state = FST_ALLOCATED;
                m_numFamiliesUsed++;
            }
            return i;
        }
    }
    return INVALID_LFID;
}

void FamilyTable::FreeFamily(LFID fid)
{
	assert(fid != INVALID_LFID);
    assert(m_numFamiliesUsed > 0);
    
    COMMIT
    {
		m_families[fid].state = FST_EMPTY;
        m_numFamiliesUsed--;
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
        "", "ALLOCATED", "CREATE QUEUED", "CREATING", "DELEGATED", "IDLE", "ACTIVE", "KILLED"
    };

    // Read the range
    set<LFID> fids;
    if (!arguments.empty()) {
        fids = parse_range<LFID>(arguments[0], 0, m_families.size());
    } else {
        for (LFID i = 0; i < m_families.size(); ++i) {
            if (m_families[i].state != FST_EMPTY) {
                fids.insert(i);
            }
        }
    }

    if (fids.empty())
    {
        out << "No families selected" << endl;
        return;
    }

    out << "    |         PC         |   Allocated    | P/N/A/Rd/Sh |  Parent | Prev | Next | State" << endl;
    out << "----+--------------------+----------------+-------------+---------+------+------+-----------" << endl;
    for (set<LFID>::const_iterator p = fids.begin(); p != fids.end(); ++p)
    {
        const Family& family = m_families[*p];

        out << dec << right << setw(3) << setfill(' ') << *p << " | ";
        if (family.state == FST_EMPTY)
        {
            out << "                   |                |             |         |      |      |";
        }
        else
        {
            if (family.state == FST_ALLOCATED)
            {
                out << "        -          |       -        |      -      | ";
            }
            else
            {
                out << hex << setw(18) << showbase << family.pc << " | " << dec;
                if (family.state == FST_CREATING || family.state == FST_DELEGATED) {
                    out << "      -       ";
                } else {
                    out << setw(3) << family.dependencies.numThreadsAllocated << "/"
                         << setw(3) << family.physBlockSize << " ("
                         << setw(4) << family.virtBlockSize << ")";
                }
                out << " | ";
                if (family.state == FST_CREATING) {
                    out << "     -     ";
                } else {
                    out << noboolalpha
                         << !family.dependencies.prevSynchronized << "/"
                         << !family.dependencies.nextTerminated << "/"
                         << !family.dependencies.allocationDone << "/"
                         << setw(2) << family.dependencies.numPendingReads << "/"
                         << setw(2) << family.dependencies.numPendingShareds << right;
                }
                out << " | ";
            }

            // Print parent
            if (family.parent.gpid != INVALID_GPID) {
                // Delegated family
                out << setfill('0')
                     << "F"  << setw(2) << family.parent.fid
                     << "@P" << setw(2) << family.parent.gpid;
            } else if (family.type == Family::GROUP) {
                // Group family
                out << setfill('0')
                     << "T"  << setw(2) << family.parent.tid
                     << "@P" << setw(2) << family.parent.lpid;
            } else {
                // Local family
                out << "   -   ";
            }
            out << " | ";

            // Print prev and next FIDs
            if (family.state == FST_ALLOCATED) {
                out << "  -  |  -  ";
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
            out << " | " << FamilyStates[family.state];
        }
        out << endl;
    }
}

}
