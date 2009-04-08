#include "ThreadTable.h"
#include "Processor.h"
#include <cassert>
using namespace Simulator;
using namespace std;

ThreadTable::ThreadTable(Processor& parent, const Config& config)
  : Structure<TID>(&parent, parent.GetKernel(), "threads"),
    m_parent(parent),
    m_threads(config.numThreads),
    m_numThreadsUsed(0)
{
    for (TID i = 0; i < config.numThreads; ++i)
    {
        m_threads[i].nextMember = i + 1;
        m_threads[i].state      = TST_EMPTY;
    }
    m_threads[config.numThreads - 1].nextMember = INVALID_TID;

    m_empty.head = 0;
    m_empty.tail = config.numThreads - 1;
}

TID ThreadTable::PopEmpty()
{
    TID tid = m_empty.head;
    if (tid != INVALID_TID)
    {
        assert(m_threads[tid].state == TST_EMPTY);
        COMMIT
        {
            m_empty.head = m_threads[tid].nextMember;
            m_threads[tid].state = TST_WAITING;
            m_numThreadsUsed++;
        }
    }
    return tid;
}

bool ThreadTable::PushEmpty(const ThreadQueue& q)
{
    COMMIT
    {
        if (m_empty.head == INVALID_TID) {
            m_empty.head = q.head;
        } else {
            m_threads[m_empty.tail].nextMember = q.head;
        }
        m_empty.tail = q.tail;
        m_threads[q.tail].nextMember = INVALID_TID;

        // Admin, set states to empty
        for (TID cur = q.head; cur != INVALID_TID; cur = m_threads[cur].nextMember)
        {
            m_threads[cur].state = TST_EMPTY;
            m_numThreadsUsed--;
        }
    }
    return true;
}
