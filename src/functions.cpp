#include "functions.h"
#include <cassert>
using namespace Simulator;
using namespace std;

void ArbitratedFunction::Arbitrate()
{
    // Choose the request with the highest priority (lowest numerical value)
    m_priority = std::numeric_limits<int>::max();
    for (RequestMap::const_iterator i = m_requests.begin(); i != m_requests.end(); ++i)
    {
        if (i->first < m_priority)
        {
            m_priority  = i->first;
            m_component = i->second;
        }
    }
    m_requests.clear();
}

void ArbitratedFunction::SetPriority(const IComponent& component, int priority)
{
    m_priorities[&component] = priority;
}

void ArbitratedFunction::AddRequest(const IComponent& component)
{
    assert(!m_priorities.empty());
    
    PriorityMap::const_iterator p = m_priorities.find(&component);
    assert(p != m_priorities.end());
    m_requests.insert(make_pair(p->second, &component));
}

void ArbitratedFunction::AddRequest(int priority)
{
    assert(m_priorities.empty());
    
    m_requests.insert(make_pair(priority, (const IComponent*)NULL));
}

                                
#ifndef NDEBUG
void ArbitratedFunction::Verify(const IComponent& component) const
{
    if (m_priorities.find(&component) == m_priorities.end())
    {
        throw IllegalPortAccess(component);
    }
}

void DedicatedFunction::Verify(const IComponent& component) const
{
    if (m_component != &component)
    {
        throw IllegalPortAccess(component);
    }
}
#endif

