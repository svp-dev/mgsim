#include "ThreadTable.h"
#include "Processor.h"
#include "config.h"
#include "range.h"
#include "symtable.h"
#include "sampling.h"
#include <cassert>
#include <iomanip>
using namespace std;

namespace Simulator
{

ThreadTable::ThreadTable(const std::string& name, Processor& parent, Clock& clock, const Config& config)
  : Object(name, parent, clock),
    m_threads(config.getValue<size_t>("NumThreads", 64)),
    m_totalalloc(0), m_maxalloc(0), m_lastcycle(0), m_curalloc(0)
{
    RegisterSampleVariableInObject(m_totalalloc, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_maxalloc, SVC_WATERMARK, m_threads.size());
    RegisterSampleVariableInObject(m_curalloc, SVC_LEVEL, m_threads.size());
    RegisterSampleVariableInObject(m_lastcycle, SVC_CUMULATIVE);

    for (TID i = 0; i < m_threads.size(); ++i)
    {
        m_threads[i].next  = i + 1;
        m_threads[i].state = TST_EMPTY;
    }
    m_threads.back().next = INVALID_TID;
    
    m_free[CONTEXT_NORMAL]    = m_threads.size() - 1;
    m_free[CONTEXT_RESERVED]  = 0;
    m_free[CONTEXT_EXCLUSIVE] = 1;

    m_empty = 0;
}

bool ThreadTable::IsEmpty() const
{
    TSize free = 0;
    for (int i = 0; i < NUM_CONTEXT_TYPES; ++i)
    {
        free += m_free[i];
    }
    return free == m_threads.size();
}

void ThreadTable::UpdateStats()
{
    CycleNo cycle = GetKernel()->GetCycleNo();
    CycleNo elapsed = cycle - m_lastcycle;
    m_lastcycle = cycle;
    
    m_curalloc = m_threads.size() - m_free[CONTEXT_RESERVED] - m_free[CONTEXT_EXCLUSIVE] - m_free[CONTEXT_NORMAL]; 
    
    m_totalalloc += m_curalloc * elapsed;
    m_maxalloc = std::max(m_maxalloc, m_curalloc);   
}

// Checks that all internal administration is sane
void ThreadTable::CheckStateSanity() const
{
#ifndef NDEBUG
    size_t used = 0;
    for (size_t i = 0; i < m_threads.size(); ++i)
    {
        if (m_threads[i].state != TST_EMPTY)
        {
            used++;
        }
    }

    // Check that each single counter is within limits
    assert(m_free[CONTEXT_NORMAL] <= m_threads.size());
    assert(m_free[CONTEXT_RESERVED] <= m_threads.size());
    assert(m_free[CONTEXT_EXCLUSIVE] <= 1);
    assert(used <= m_threads.size());

    // All counts must add up
    assert(m_free[CONTEXT_NORMAL] + m_free[CONTEXT_RESERVED] + m_free[CONTEXT_EXCLUSIVE] + used == m_threads.size());
#endif
}

TSize ThreadTable::GetNumFreeThreads(ContextType type) const
{
    // Check that we are in a sane state
    CheckStateSanity();
    
    return m_free[type];
}

void ThreadTable::ReserveThread()
{
    // Check that we are in a sane state
    CheckStateSanity();

    COMMIT{
        UpdateStats();

        // Move one free thread from normal to reserved
        m_free[CONTEXT_NORMAL]--;
        m_free[CONTEXT_RESERVED]++;
    }

    // Check that we leave a sane state
    CheckStateSanity();
}

void ThreadTable::UnreserveThread()
{
    // Check that we are in a sane state
    CheckStateSanity();

    COMMIT{
        UpdateStats();

        // Move one free thread from reserved back to normal
        m_free[CONTEXT_NORMAL]++;
        m_free[CONTEXT_RESERVED]--;
    }

    // Check that we leave a sane state
    CheckStateSanity();
}

TID ThreadTable::PopEmpty(ContextType context)
{
    // Check that we are in a sane state
    CheckStateSanity();

    TID tid = INVALID_TID;
    
    // See if we have a free entry
    if (m_free[context] > 0)
    {
        tid = m_empty;
        assert(tid != INVALID_TID);
        assert(m_threads[tid].state == TST_EMPTY);
        COMMIT
        {
            UpdateStats();

            m_empty = m_threads[tid].next;
            m_threads[tid].state = TST_WAITING;
            m_free[context]--;
        }
    }

    // Check that we leave a sane state
    CheckStateSanity();
    return tid;
}

void ThreadTable::PushEmpty(TID tid, ContextType context)
{
    // Check that we are in a sane state
    CheckStateSanity();

    assert(tid != INVALID_TID);
    
    COMMIT
    {
        UpdateStats();

        m_threads[tid].next = m_empty;
        m_empty = tid;

        // Admin, set state to empty
        m_threads[tid].state = TST_EMPTY;

        m_free[context]++;
    }

    // Check that we leave a sane state
    CheckStateSanity();
}

void ThreadTable::Cmd_Help(ostream& out, const vector<string>& /* arguments */) const
{
    out <<
    "The Thread Table is the storage area in a processor that stores all thread's\n"
    "information. It contains information about the thread's state, execution\n"
    "context, dependencies and much more.\n\n"
    "Supported operations:\n"
    "- read <component> [range]\n"
    "  Reads and displays the used thread table entries. Note that not all\n"
    "  information is displayed; only the most important data.\n"
    "  An optional range argument can be given to only read those threads. The\n"
    "  range is a comma-seperated list of thread ranges. Example ranges:\n"
    "  \"1\", \"1-4,15,7-8\", \"all\"\n";
}

void ThreadTable::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    static const char* const ThreadStates[] = {
        "", "WAITING", "READY", "ACTIVE", "RUNNING", "SUSPENDED", "UNUSED", "KILLED"
    };
    
    // Read the range
    bool show_counts = false;
    set<TID> tids;
    if (!arguments.empty()) {
        tids = parse_range<TID>(arguments[0], 0, m_threads.size());
    } else {
        show_counts = true;
        for (TID i = 0; i < m_threads.size(); ++i) {
            if (m_threads[i].state != TST_EMPTY) {
                tids.insert(i);
            }
        }
    }
    
    if (tids.empty())
    {
        out << "No threads selected" << endl;
    }
    else
    {
        out << "    |         PC         | Fam | Index | Next | Flags | WR | State     | Symbol" << endl;
        out << "----+--------------------+-----+-------+------+-------+----+-----------+--------" << endl;
        for (set<TID>::const_iterator p = tids.begin(); p != tids.end(); ++p)
        {
            out << right << dec << setw(3) << setfill(' ') << *p << " | ";
            const Thread& thread = m_threads[*p];

            if (thread.state != TST_EMPTY)
            {
                out << setw(18) << setfill(' ') << hex << showbase << thread.pc << " | ";
                out << "F" << setfill('0') << dec << noshowbase << setw(2) << thread.family << " | ";
                out << setw(5) << dec << setfill(' ') << thread.index << " | ";
                if (thread.nextInBlock != INVALID_TID) out << dec << setw(4) << setfill(' ') << thread.nextInBlock; else out << "   -";
                out << " | ";
                out << dec;
                out << " "
                    << (thread.dependencies.prevCleanedUp ? 'P' : '.')
                    << (thread.dependencies.killed        ? 'K' : '.')
                    << "   | "
                    << setw(2) << setfill(' ') << thread.dependencies.numPendingWrites
                    << " | ";

                out << left << setfill(' ') << setw(9) <<  ThreadStates[thread.state]
                    << " | " << GetKernel()->GetSymbolTable()[thread.pc];
            }
            else
            {
                out << "                   |     |       |      |       |    |           |";
            }
            out << endl;
        }
    }
    
    if (show_counts)
    {
        out << endl
            << "Free threads: " << dec
            << m_free[CONTEXT_NORMAL] << " normal, "
            << m_free[CONTEXT_RESERVED] << " reserved, "
            << m_free[CONTEXT_EXCLUSIVE] << " exclusive"
            << endl;
    }
}

}
