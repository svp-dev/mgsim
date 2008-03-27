#ifndef PORTS_H
#define PORTS_H

#include "kernel.h"
#include <map>
#include <set>

namespace Simulator
{

template <typename I> class Structure;
template <typename I> class ArbitratedWritePort;
class ArbitratedReadPort;

//
// ArbitratedPort
//
template <typename I>
class ArbitratedPort
{
    typedef std::map<const IComponent*,I>   RequestMap;
    typedef std::map<const IComponent*,int> PriorityMap;

	uint64_t m_busyCycles;
public:
	uint64_t getBusyCycles() const {
		return m_busyCycles;
	}

    void setPriority(const IComponent& component, int priority) {
        m_priorities[&component] = priority;
    }

    void notify(bool chosen) {
        m_chosen = chosen;
        m_requests.clear();
    }

    bool arbitrate(I* pIndex)
    {
        // Choose the request with the highest priority
        int highest = INT_MAX;
        for (typename RequestMap::const_iterator i = m_requests.begin(); i != m_requests.end(); i++)
        {
            typename PriorityMap::const_iterator priority = m_priorities.find(i->first);
            if (priority != m_priorities.end() && priority->second < highest)
            {
                highest     = priority->second;
                m_component = i->first;
                *pIndex     = i->second;
            }
        }
        m_chosen = false;
        if (highest != INT_MAX)
		{
			m_busyCycles++;
			return true;
		}
		return false;
    }
    
    bool chosen() const { return m_chosen; }
    const IComponent* component() const { return m_component; }

protected:
    bool acquiring() const {
        return m_kernel.getCyclePhase() == PHASE_ACQUIRE;
    }

    bool acquired(const IComponent& component) const {
        return m_chosen && &component == m_component;
    }

    void verify(const IComponent& component) const {
#ifndef NDEBUG
        if (m_priorities.find(&component) == m_priorities.end()) {
            throw IllegalPortAccess(component.getFQN());
        }
#endif
    }

    void addRequest(const IComponent& component, const I& index) {
        m_requests[&component] = index;
    }

    ArbitratedPort(const Kernel& kernel) : m_busyCycles(0), m_kernel(kernel) {}
    virtual ~ArbitratedPort() {}

private:
    PriorityMap m_priorities;
    RequestMap  m_requests;
    
    const Kernel&       m_kernel;
    const IComponent*   m_component;
    bool                m_chosen;
};

//
// Structure
//
template <typename I>
class Structure : public IStructure
{
    typedef std::map<ArbitratedWritePort<I>*,int> PriorityMap;
    typedef std::set<ArbitratedWritePort<I>*>     WritePortList;
    typedef std::set<ArbitratedReadPort*>         ReadPortList;

public:
    typedef std::vector<ArbitratedWritePort<I>*>  WritePortMap;
    typedef std::map<I,WritePortMap>              RequestPortMap;

    Structure(Object* parent, Kernel& kernel, const std::string& name) : IStructure(parent, kernel, name) {}

    void setPriority(ArbitratedWritePort<I>& port, int priority) { m_priorities[&port] = priority; }

    void registerReadPort(ArbitratedReadPort& port)         { m_readPorts.insert(&port);  }
    void registerWritePort(ArbitratedWritePort<I>& port)    { m_writePorts.insert(&port); }
    void unregisterReadPort(ArbitratedReadPort& port)       { m_readPorts.erase(&port);   }
    void unregisterWritePort(ArbitratedWritePort<I>& port)  { m_writePorts.erase(&port);  }

    void onArbitrateReadPhase();        // Body defined below

    void onArbitrateWritePhase()
    {
        // Arbitrate between the ports      
        m_requests.clear();
        for (typename WritePortList::iterator i = m_writePorts.begin(); i != m_writePorts.end(); i++)
        {
            I index;
            if ((*i)->arbitrate(&index))
            {
                // This port has requests
                m_requests[index].push_back(*i);
            }
        }

        for (typename RequestPortMap::iterator i = m_requests.begin(); i != m_requests.end(); i++)
        {
            ArbitratedWritePort<I>* port = NULL;
            int highest = INT_MAX;

            for (typename WritePortMap::iterator j = i->second.begin(); j != i->second.end(); j++)
            {
                int priority = getPriority(*j);
                if (priority < highest)
                {
                    highest   = priority;
                    port      = *j;
                }
            }

            for (typename WritePortMap::iterator j = i->second.begin(); j != i->second.end(); j++)
            {
                (*j)->notify( port == *j );
            }
        }
    }

protected:
    const RequestPortMap& getArbitratedRequests() const {
        return m_requests;
    }

    int getPriority(ArbitratedWritePort<I>* &port) const {
        typename PriorityMap::const_iterator priority = m_priorities.find(port);
        return (priority != m_priorities.end()) ? priority->second : INT_MAX;
    }

private:
    ReadPortList    m_readPorts;
    WritePortList   m_writePorts;
    PriorityMap m_priorities;
    RequestPortMap  m_requests;
};

//
// DedicatedPort
//
class DedicatedPort
{
public:
    DedicatedPort() { m_component = NULL; }
    virtual ~DedicatedPort() {}

    virtual void setComponent(const IComponent& component) {
        m_component = &component;
    }
protected:
    void verify(const IComponent& component) {
#ifndef NDEBUG
        if (m_component != &component) {
            throw IllegalPortAccess(component.getFQN());
        }
#endif
    }
private:
    const IComponent* m_component;
};

class ReadPort
{
public:
    virtual bool read(const IComponent& component) = 0;
    virtual ~ReadPort() {}
};

template <typename I>
class WritePort
{
public:
    virtual bool write(const IComponent& component, const I& index) = 0;
    virtual ~WritePort() {}
};

//
// ArbitratedReadPort
//
class ArbitratedReadPort : public ArbitratedPort<bool>, public ReadPort
{
public:
    template <typename I>
    ArbitratedReadPort(Structure<I>& structure) : ArbitratedPort<bool>(*structure.getKernel()) {
        structure.registerReadPort(*this);
    }

    bool read(const IComponent& component)
    {
        this->verify(component);
        if (acquiring())
        {
            addRequest(component, false);
        }
        else if (!acquired(component))
        {
            return false;
        }
        return true;
    }
};

//
// ArbitratedWritePort
//
template <typename I>
class ArbitratedWritePort : public ArbitratedPort<I>, public WritePort<I>
{
public:
    ArbitratedWritePort(Structure<I>& structure) : ArbitratedPort<I>(*structure.getKernel()) {
        structure.registerWritePort(*this);
    }

    bool write(const IComponent& component, const I& index)
    {
        this->verify(component);
        if (this->acquiring())
        {
            this->addRequest(component, index);
        }
        else if (!this->acquired(component))
        {
            return false;
        }
        return true;
    }
};

//
// DedicatedReadPort
//
class DedicatedReadPort : public DedicatedPort, public ReadPort
{
public:
    template <typename I>
    DedicatedReadPort(Structure<I>& structure) {}
    bool read(const IComponent& component) {
        verify(component);
        return true;
    }
};

//
// DedicatedWritePort
//
template <typename I>
class DedicatedWritePort : public ArbitratedWritePort<I>, public DedicatedPort
{
public:
    DedicatedWritePort(Structure<I>& structure) : ArbitratedWritePort<I>(structure) {}

    void setComponent(const IComponent& component) {
        this->setPriority(component, 0);
        DedicatedPort::setComponent(component);
    }

    bool write(const IComponent& component, const I& index) {
        DedicatedPort::verify(component);
        return ArbitratedWritePort<I>::write(component, index);
    }
};


template <typename I>
void Structure<I>::onArbitrateReadPhase()
{
    // Arbitrate between all incoming requests
    for (typename ReadPortList::iterator i = m_readPorts.begin(); i != m_readPorts.end(); i++)
    {
        bool index;
        if ((*i)->arbitrate(&index))
        {
            // This port has a request, handle it
            (*i)->notify(true);
        }
    }
}


}
#endif

