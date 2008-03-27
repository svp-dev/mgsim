#include "functions.h"
using namespace Simulator;
using namespace std;

void ArbitratedFunction::arbitrate()
{
    // Choose the request with the highest priority
    int highest = INT_MAX;
    m_component = NULL;
    for (RequestMap::const_iterator i = m_requests.begin(); i != m_requests.end(); i++)
    {
        PriorityMap::const_iterator priority = m_priorities.find(*i);
        if (priority != m_priorities.end() && priority->second < highest)
        {
            highest     = priority->second;
            m_component = *i;
        }
    }
    m_requests.clear();
}

#ifndef NDEBUG
void ArbitratedFunction::verify(const IComponent& component)
{
    if (m_priorities.find(&component) == m_priorities.end())
    {
        throw IllegalPortAccess(component.getFQN());
    }
}

void DedicatedFunction::verify(const IComponent& component)
{
    if (m_component != &component)
    {
        throw IllegalPortAccess(component.getFQN());
    }
}
#endif

