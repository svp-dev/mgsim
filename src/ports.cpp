#include "ports.h"

namespace Simulator
{

void StructureBase::RegisterReadPort(ArbitratedReadPort& port)
{
    m_readPorts.insert(&port);
}

void StructureBase::UnregisterReadPort(ArbitratedReadPort& port)
{
    m_readPorts.erase(&port);
}

void StructureBase::OnArbitrateReadPhase()
{
    // Arbitrate between all incoming requests
    for (ReadPortList::iterator i = m_readPorts.begin(); i != m_readPorts.end(); ++i)
    {
        (*i)->Arbitrate();
    }
}

void ArbitratedPort::Arbitrate()
{
    // Choose the request with the highest priority
    m_source = ArbitrationSource();

    int highest = std::numeric_limits<int>::max();
    for (RequestMap::const_iterator i = m_requests.begin(); i != m_requests.end(); ++i)
    {
        PriorityMap::const_iterator priority = m_priorities.find(*i);
        if (priority != m_priorities.end() && priority->second < highest)
        {
            highest  = priority->second;
            m_source = *i;
        }
    }
    m_requests.clear();
    
    if (m_source != ArbitrationSource())
    {
        m_busyCycles++;
    }
}

void ArbitratedService::OnArbitrateReadPhase()
{
    Arbitrate();
}

void ArbitratedService::OnArbitrateWritePhase()
{
    Arbitrate();
}


}
