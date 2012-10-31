#include "ports.h"
#include "sampling.h"
#include <sstream>
#include <algorithm>

using namespace std;

namespace Simulator
{

void SimpleArbitratedPort::AddRequest(const Process& process)
{
    if (find(m_requests.begin(), m_requests.end(), &process) != m_requests.end())
    {
        // A process can request more than once in a cycle if the requester is in a higher frequency
        // domain than the arbitrator.

        // But obviously the clocks should differ, or else it's a bug.
        assert(&process.GetObject()->GetClock() != &m_object.GetClock());
        return;
    }
    m_requests.push_back(&process);
}

ArbitratedPort::ArbitratedPort(const Object& object, const string& name)
  : m_selected(NULL),
    m_busyCycles(0),
    m_object(object),
    m_name(name)
{
    RegisterSampleVariable(m_busyCycles, object.GetFQN() + '.' + name + ".busyCycles", SVC_CUMULATIVE);
}

void PriorityCyclicArbitratedPort::Arbitrate()
{
    m_selected = NULL;
    if (!m_requests.empty())
    {
        if (m_requests.size() == 1)
        {
            // Optimization for common case
            m_selected = *m_requests.begin();

            size_t maybe_cyclic_last = find(m_cyclicprocesses.begin(), m_cyclicprocesses.end(), m_selected) - m_cyclicprocesses.begin();
            if (maybe_cyclic_last < m_cyclicprocesses.size())
                m_lastSelected = maybe_cyclic_last;
        }
        else
        {
            // Choose the request with the highest priority
            unsigned int highest = numeric_limits<unsigned int>::max();
            for (auto i : m_requests)
            {
                // The position in the vector is its priority
                size_t priority = find(m_processes.begin(), m_processes.end(), i) - m_processes.begin();
                if (priority == m_processes.size())
                {
                    // this request is for a cyclic process, don't use
                    // it as a candidate.
                    continue;
                }
                else if (priority < highest)
                {
                    highest    = priority;
                    m_selected = i;
                }
            }

            if (m_selected == NULL)
            {
                // no priority process found, the request is for a cyclic process.
                size_t lowest = numeric_limits<size_t>::max();
                for (auto i : m_requests)
                {
                    size_t pos = find(m_cyclicprocesses.begin(), m_cyclicprocesses.end(), i) - m_cyclicprocesses.begin();
                    assert(pos < m_cyclicprocesses.size());

                    // Find the distance to the last selected
                    pos = (pos + m_cyclicprocesses.size() - m_lastSelected) % m_cyclicprocesses.size();
                    if (pos != 0 && pos < lowest)
                    {
                        lowest     = pos;
                        m_selected = i;
                    }
                }

                // Remember which one we selected
                m_lastSelected = (m_lastSelected + lowest) % m_cyclicprocesses.size();

            }
        }

        assert(m_selected != NULL);
        m_requests.clear();
        m_busyCycles++;
    }
}


void PriorityArbitratedPort::Arbitrate()
{
    m_selected = NULL;
    if (!m_requests.empty())
    {
        if (m_requests.size() == 1)
        {
            // Optimization for common case
            m_selected = *m_requests.begin();
        }
        else
        {
            // Choose the request with the highest priority
            size_t highest = numeric_limits<size_t>::max();
            for (auto i : m_requests)
            {
                // The position in the vector is its priority
                size_t priority = find(m_processes.begin(), m_processes.end(), i) - m_processes.begin();
                assert(priority < m_processes.size());
                if (priority < highest)
                {
                    highest    = priority;
                    m_selected = i;
                }
            }
        }
        assert(m_selected != NULL);
        m_requests.clear();
        m_busyCycles++;
    }
}

void CyclicArbitratedPort::Arbitrate()
{
    assert(m_lastSelected <= m_processes.size());

    m_selected = NULL;
    if (!m_requests.empty())
    {
        if (m_requests.size() == 1)
        {
            m_selected     = *m_requests.begin();
            m_lastSelected = find(m_processes.begin(), m_processes.end(), m_selected) - m_processes.begin();
            assert(m_lastSelected < m_processes.size());
        }
        else
        {
            size_t lowest = numeric_limits<size_t>::max();
            for (auto i : m_requests)
            {
                size_t pos = find(m_processes.begin(), m_processes.end(), i) - m_processes.begin();
                assert(pos < m_processes.size());

                // Find the distance to the last selected
                pos = (pos + m_processes.size() - m_lastSelected) % m_processes.size();
                if (pos != 0 && pos < lowest)
                {
                    lowest     = pos;
                    m_selected = i;
                }
            }

            // Remember which one we selected
            m_lastSelected = (m_lastSelected + lowest) % m_processes.size();
        }
        assert(m_selected != NULL);
        m_requests.clear();
        m_busyCycles++;
    }
}

//
// IStructure class
//
IStructure::IStructure(const string& name, Object& parent, Clock& clock)
    : Object(name, parent, clock), Arbitrator(clock),
      m_readPorts()
{
}

void IStructure::RegisterReadPort(ArbitratedReadPort& port)
{
    m_readPorts.insert(&port);
}

void IStructure::UnregisterReadPort(ArbitratedReadPort& port)
{
    m_readPorts.erase(&port);
}

void IStructure::ArbitrateReadPorts()
{
    // Arbitrate between all incoming requests
    for (auto p : m_readPorts)
        p->Arbitrate();
}

}
