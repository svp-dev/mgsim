#include "ports.h"
#include "sampling.h"
#include <sstream>
#include <algorithm>

namespace Simulator
{

void SimpleArbitratedPort::AddRequest(const Process& process)
{
    if (std::find(m_requests.begin(), m_requests.end(), &process) != m_requests.end())
    {
        // A process can request more than once in a cycle if the requester is in a higher frequency
        // domain than the arbitrator.
        
        // But obviously the clocks should differ, or else it's a bug.
        assert(&process.GetObject()->GetClock() != &m_object.GetClock());
        return;
    }
    m_requests.push_back(&process);
}

ArbitratedPort::ArbitratedPort(const Object& object, const std::string& name) 
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
            
            size_t maybe_cyclic_last = std::find(m_cyclicprocesses.begin(), m_cyclicprocesses.end(), m_selected) - m_cyclicprocesses.begin();
            if (maybe_cyclic_last < m_cyclicprocesses.size())
                m_lastSelected = maybe_cyclic_last;
        }
        else
        {
            // Choose the request with the highest priority
            unsigned int highest = std::numeric_limits<unsigned int>::max();
            for (ProcessList::const_iterator i = m_requests.begin(); i != m_requests.end(); ++i)
            {
                // The position in the vector is its priority
                size_t priority = std::find(m_processes.begin(), m_processes.end(), *i) - m_processes.begin();
                if (priority == m_processes.size())
                {
                    // this request is for a cyclic process, don't use
                    // it as a candidate.
                    continue;
                }
                else if (priority < highest)
                {
                    highest    = priority;
                    m_selected = *i;
                }
            }

            if (m_selected == NULL)
            {
                // no priority process found, the request is for a cyclic process.
                size_t lowest = std::numeric_limits<size_t>::max();
                for (ProcessList::const_iterator i = m_requests.begin(); i != m_requests.end(); ++i)
                {
                    size_t pos = std::find(m_cyclicprocesses.begin(), m_cyclicprocesses.end(), *i) - m_cyclicprocesses.begin();
                    assert(pos < m_cyclicprocesses.size());
                    
                    // Find the distance to the last selected
                    pos = (pos + m_cyclicprocesses.size() - m_lastSelected) % m_cyclicprocesses.size();
                    if (pos != 0 && pos < lowest)
                    {
                        lowest     = pos;
                        m_selected = *i;
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
            size_t highest = std::numeric_limits<size_t>::max();
            for (ProcessList::const_iterator i = m_requests.begin(); i != m_requests.end(); ++i)
            {
                // The position in the vector is its priority
                size_t priority = std::find(m_processes.begin(), m_processes.end(), *i) - m_processes.begin();
                assert(priority < m_processes.size());
                if (priority < highest)
                {
                    highest    = priority;
                    m_selected = *i;
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
            m_lastSelected = std::find(m_processes.begin(), m_processes.end(), m_selected) - m_processes.begin();
            assert(m_lastSelected < m_processes.size());
        }
        else
        {
            size_t lowest = std::numeric_limits<size_t>::max();
            for (ProcessList::const_iterator i = m_requests.begin(); i != m_requests.end(); ++i)
            {
                size_t pos = std::find(m_processes.begin(), m_processes.end(), *i) - m_processes.begin();
                assert(pos < m_processes.size());
        
                // Find the distance to the last selected
                pos = (pos + m_processes.size() - m_lastSelected) % m_processes.size();
                if (pos != 0 && pos < lowest)
                {
                    lowest     = pos;
                    m_selected = *i;
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
IStructure::IStructure(const std::string& name, Object& parent, Clock& clock)
    : Object(name, parent, clock), Arbitrator(clock)
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
    for (ReadPortList::iterator i = m_readPorts.begin(); i != m_readPorts.end(); ++i)
    {
        (*i)->Arbitrate();
    }
}

}
