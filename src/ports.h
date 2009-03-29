#ifndef PORTS_H
#define PORTS_H

#include "kernel.h"
#include <map>
#include <set>
#include <limits>

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
	uint64_t GetBusyCycles() const {
		return m_busyCycles;
	}

    void SetPriority(const IComponent& component, int priority) {
        m_priorities[&component] = priority;
    }

    void Notify(bool chosen) {
        m_chosen = chosen;
        m_requests.clear();
    }

    bool Arbitrate(I* pIndex)
    {
        // Choose the request with the highest priority
        int highest = std::numeric_limits<int>::max();
        for (typename RequestMap::const_iterator i = m_requests.begin(); i != m_requests.end(); ++i)
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
        if (highest != std::numeric_limits<int>::max())
		{
			m_busyCycles++;
			return true;
		}
		return false;
    }
    
    bool IsChosen() const { return m_chosen; }
    const IComponent* GetComponent() const { return m_component; }

protected:
    bool IsAcquiring() const {
        return m_kernel.GetCyclePhase() == PHASE_ACQUIRE;
    }

    bool HasAcquired(const IComponent& component) const {
        return m_chosen && &component == m_component;
    }

#ifndef NDEBUG
    void Verify(const IComponent& component) const {
        if (m_priorities.find(&component) == m_priorities.end()) {
            throw IllegalPortAccess(component);
        }
#else
    void Verify(const IComponent& /* component */) const {
#endif
    }

    void AddRequest(const IComponent& component, const I& index) {
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

    void SetPriority(ArbitratedWritePort<I>& port, int priority) { m_priorities[&port] = priority; }

    void RegisterReadPort(ArbitratedReadPort& port)         { m_readPorts.insert(&port);  }
    void RegisterWritePort(ArbitratedWritePort<I>& port)    { m_writePorts.insert(&port); }
    void UnregisterReadPort(ArbitratedReadPort& port)       { m_readPorts.erase(&port);   }
    void UnregisterWritePort(ArbitratedWritePort<I>& port)  { m_writePorts.erase(&port);  }

    void OnArbitrateReadPhase();        // Body defined below

    void OnArbitrateWritePhase()
    {
        // Arbitrate between the ports      
        m_requests.clear();
        for (typename WritePortList::iterator i = m_writePorts.begin(); i != m_writePorts.end(); ++i)
        {
            I index = I();
            if ((*i)->Arbitrate(&index))
            {
                // This port has requests
                m_requests[index].push_back(*i);
            }
        }

        for (typename RequestPortMap::iterator i = m_requests.begin(); i != m_requests.end(); ++i)
        {
            ArbitratedWritePort<I>* port = NULL;
            int highest = std::numeric_limits<int>::max();

            for (typename WritePortMap::iterator j = i->second.begin(); j != i->second.end(); ++j)
            {
                int priority = GetPriority(*j);
                if (priority < highest)
                {
                    highest   = priority;
                    port      = *j;
                }
            }

            for (typename WritePortMap::iterator j = i->second.begin(); j != i->second.end(); ++j)
            {
                (*j)->Notify( port == *j );
            }
        }
    }

protected:
    const RequestPortMap& GetArbitratedRequests() const {
        return m_requests;
    }

    int GetPriority(ArbitratedWritePort<I>* &port) const {
        typename PriorityMap::const_iterator priority = m_priorities.find(port);
        return (priority != m_priorities.end()) ? priority->second : std::numeric_limits<int>::max();
    }

private:
    ReadPortList    m_readPorts;
    WritePortList   m_writePorts;
    PriorityMap     m_priorities;
    RequestPortMap  m_requests;
};

//
// DedicatedPort
//
class DedicatedPort
{
public:
    DedicatedPort() : m_component(NULL) { }
    virtual ~DedicatedPort() {}

    virtual void SetComponent(const IComponent& component) {
        m_component = &component;
    }
protected:
#ifndef NDEBUG
    void Verify(const IComponent& component) {
        if (m_component != &component) {
            throw IllegalPortAccess(component);
        }
#else
    void Verify(const IComponent& /* component */) {
#endif
    }
private:
    const IComponent* m_component;
};

class ReadPort
{
public:
    virtual bool Read(const IComponent& component) = 0;
    virtual ~ReadPort() {}
};

template <typename I>
class WritePort
{
public:
    virtual bool Write(const IComponent& component, const I& index) = 0;
    virtual ~WritePort() {}
};

//
// ArbitratedReadPort
//
class ArbitratedReadPort : public ArbitratedPort<bool>, public ReadPort
{
public:
    template <typename I>
    ArbitratedReadPort(Structure<I>& structure) : ArbitratedPort<bool>(*structure.GetKernel()) {
        structure.RegisterReadPort(*this);
    }

    bool Read(const IComponent& component)
    {
        this->Verify(component);
        if (IsAcquiring())
        {
            AddRequest(component, false);
        }
        else if (!HasAcquired(component))
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
    ArbitratedWritePort(Structure<I>& structure) : ArbitratedPort<I>(*structure.GetKernel()) {
        structure.RegisterWritePort(*this);
    }

    bool Write(const IComponent& component, const I& index)
    {
        this->Verify(component);
        if (this->IsAcquiring())
        {
            this->AddRequest(component, index);
        }
        else if (!this->HasAcquired(component))
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
    DedicatedReadPort(Structure<I>& /* structure */) {}
    bool Read(const IComponent& component) {
        Verify(component);
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

    void SetComponent(const IComponent& component) {
        this->SetPriority(component, 0);
        DedicatedPort::SetComponent(component);
    }

    bool Write(const IComponent& component, const I& index) {
        DedicatedPort::Verify(component);
        return ArbitratedWritePort<I>::Write(component, index);
    }
};


template <typename I>
void Structure<I>::OnArbitrateReadPhase()
{
    // Arbitrate between all incoming requests
    for (typename ReadPortList::iterator i = m_readPorts.begin(); i != m_readPorts.end(); ++i)
    {
        bool index;
        if ((*i)->Arbitrate(&index))
        {
            // This port has a request, handle it
            (*i)->Notify(true);
        }
    }
}


}
#endif

