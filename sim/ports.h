#ifndef PORTS_H
#define PORTS_H

#include "kernel.h"
#include <cassert>
#include <algorithm>
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
class ArbitratedPort
{
public:
    uint64_t GetBusyCycles() const {
        return m_busyCycles;
    }

    std::string GetFQN() const { return m_object.GetFQN() + '.' + m_name; }

protected:
    bool HasAcquired(const Process& process) const {
        return m_selected == &process;
    }
    
    const Process* GetSelectedProcess() const {
        return m_selected;
    }

    ArbitratedPort(const Object& object, const std::string& name);
    virtual ~ArbitratedPort() {}

    const Process* m_selected;
    uint64_t       m_busyCycles;

    const Object&  m_object;
private:
    std::string    m_name;
};

class SimpleArbitratedPort : public ArbitratedPort
{
public:
    void AddProcess(const Process& process) {
        m_processes.push_back(&process);
    }

protected:
    bool CanAccess(const Process& process) const {
        return !(std::find(m_processes.begin(), m_processes.end(), &process) == m_processes.end());
    }

    void AddRequest(const Process& process);
    
SimpleArbitratedPort(const Object& object, const std::string& name)
    : ArbitratedPort(object, name)
    {}
      
    virtual ~SimpleArbitratedPort() {}

protected:
    typedef std::vector<const Process*> ProcessList;

    ProcessList    m_processes;
    ProcessList    m_requests;

};

class PriorityArbitratedPort : public SimpleArbitratedPort
{
public:
    void Arbitrate();

protected:
PriorityArbitratedPort(const Object& object, const std::string& name)
    : SimpleArbitratedPort(object, name) {}
};

class CyclicArbitratedPort : public SimpleArbitratedPort
{
public:
    void Arbitrate();
    
protected:
CyclicArbitratedPort(const Object& object, const std::string& name)
    : SimpleArbitratedPort(object, name), 
        m_lastSelected(0) {}

    
    size_t m_lastSelected;
    
};

class PriorityCyclicArbitratedPort : public CyclicArbitratedPort
{
public:

    void Arbitrate();

    void AddPriorityProcess(const Process& process) {
        SimpleArbitratedPort::AddProcess(process);
    }
    void AddCyclicProcess(const Process& process) {
        m_cyclicprocesses.push_back(&process);
    }

protected:
PriorityCyclicArbitratedPort(const Object& object, const std::string& name)
    : CyclicArbitratedPort(object, name)
        {}

    bool CanAccess(const Process& process) const {
        return SimpleArbitratedPort::CanAccess(process) 
            || (std::find(m_cyclicprocesses.begin(), m_cyclicprocesses.end(), &process) != m_cyclicprocesses.end());
    }

    ProcessList m_cyclicprocesses;

private:
    // hide AddProcess from base class to force use
    // of AddPriorityProcess above.
    void AddProcess(const Process& process);
};

//
// WritePort
//
// Write ports have an index, because if two ports write to the same index
// in a structure, only one can proceed. So the index needs to be captured
// for arbitration.
//
template <typename I>
    class WritePort
{
    bool    m_valid;    ///< Is there a request?
    bool    m_chosen;   ///< Have we been chosen?
    I       m_index;    ///< Index of the request
    
protected:
    void SetIndex(const I& index)
    {
        m_index  = index;
        m_valid  = true;
        m_chosen = false;
    }
    
WritePort()
    : m_valid(false), m_chosen(false)
    {
    }

public:
    bool     IsChosen() const { return m_chosen; }
    const I* GetIndex() const { return (m_valid) ? &m_index : NULL; }
    
    void Notify(bool chosen) {
        m_chosen = chosen;
        m_valid  = false;
    }
};

//
// Structure
//

/// Base class for all structures
class IStructure : public virtual Object, private Arbitrator
{
    typedef std::set<ArbitratedReadPort*> ReadPortList;

    ReadPortList m_readPorts;

protected:
    void ArbitrateReadPorts();

public:
    void RequestArbitration()
    {
        Arbitrator::RequestArbitration();
    }
    
    /**
     * @brief Constructs the structure
     * @param parent parent object.
     * @param name name of the object.
     */
    IStructure(const std::string& name, Object& parent, Clock& clock);

    void RegisterReadPort(ArbitratedReadPort& port);
    void UnregisterReadPort(ArbitratedReadPort& port);

    std::string GetFQN() const { return Object::GetFQN(); }
};

template <typename I>
    class Structure : public IStructure
{
    typedef std::map<WritePort<I>*, int>      PriorityMap;
    typedef std::set<ArbitratedWritePort<I>*> ArbitratedWritePortList;
    typedef std::set<WritePort<I>*>           WritePortList;

    void OnArbitrate()
    {
        typedef std::vector<WritePort<I>*> WritePortMap;
        typedef std::map<I,WritePortMap>   RequestPortMap;
        
        // Tell each port to arbitrate
        ArbitrateReadPorts();
        for (typename ArbitratedWritePortList::iterator i = m_arbitratedWritePorts.begin(); i != m_arbitratedWritePorts.end(); ++i)
        {
            (*i)->Arbitrate();
        }

        // Get the final requests from all ports
        RequestPortMap requests;
        for (typename WritePortList::iterator i = m_writePorts.begin(); i != m_writePorts.end(); ++i)
        {
            const I* index = (*i)->GetIndex();
            if (index != NULL)
            {
                requests[*index].push_back(*i);
            }
        }

        // Arbitrate between the ports for each request
        for (typename RequestPortMap::iterator i = requests.begin(); i != requests.end(); ++i)
        {
            WritePort<I>* port = NULL;
            int highest = std::numeric_limits<int>::max();

            for (typename WritePortMap::iterator j = i->second.begin(); j != i->second.end(); ++j)
            {
                typename PriorityMap::const_iterator priority = m_priorities.find(*j);
                if (priority != m_priorities.end() && priority->second < highest)
                {
                    highest   = priority->second;
                    port      = *j;
                }
            }

            for (typename WritePortMap::iterator j = i->second.begin(); j != i->second.end(); ++j)
            {
                (*j)->Notify( port == *j );
            }
        }
    }

    WritePortList           m_writePorts;
    ArbitratedWritePortList m_arbitratedWritePorts;
    PriorityMap             m_priorities;

public:
Structure(const std::string& name, Object& parent, Clock& clock) 
    : Object(name, parent, clock), IStructure(name, parent, clock) {}

    void AddPort(WritePort<I>& port)
    {
        m_priorities.insert(typename PriorityMap::value_type(&port, m_priorities.size()));
    }

    void RegisterWritePort(WritePort<I>& port)                        { m_writePorts.insert(&port); }
    void UnregisterWritePort(WritePort<I>& port)                      { m_writePorts.erase(&port);  }
    void RegisterArbitratedWritePort(ArbitratedWritePort<I>& port)    { RegisterWritePort(port);   m_arbitratedWritePorts.insert(&port); }
    void UnregisterArbitratedWritePort(ArbitratedWritePort<I>& port)  { UnregisterWritePort(port); m_arbitratedWritePorts.erase (&port); }
};

//
// ArbitratedReadPort
//
class ArbitratedReadPort : public PriorityArbitratedPort
{
    IStructure& m_structure;
public:
ArbitratedReadPort(IStructure& structure, const std::string& name)
    : PriorityArbitratedPort(structure, name), m_structure(structure)
    {
        m_structure.RegisterReadPort(*this);
    }
    
    ~ArbitratedReadPort() {
        m_structure.UnregisterReadPort(*this);
    }

    bool Read()
    {
        const Process& process = *m_structure.GetKernel()->GetActiveProcess();
        assert(CanAccess(process));
        if (m_structure.GetKernel()->GetCyclePhase() == PHASE_ACQUIRE)
        {
            AddRequest(process);
            m_structure.RequestArbitration();
        }
        else if (!HasAcquired(process))
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
    class ArbitratedWritePort : public PriorityArbitratedPort, public WritePort<I>
{
    typedef std::map<const Process*, I> IndexMap;
   
    Structure<I>& m_structure;
    IndexMap      m_indices;

    void AddRequest(const Process& process, const I& index)
    {
        PriorityArbitratedPort::AddRequest(process);
        m_indices[&process] = index;
    }
    
public:
    void Arbitrate()
    {
        PriorityArbitratedPort::Arbitrate();
        const Process* process = GetSelectedProcess();
        if (process != NULL)
        {
            // A process was selected; make its index active for
            // write port arbitration
            typename IndexMap::const_iterator p = m_indices.find(process);
            assert(p != m_indices.end());
            this->SetIndex(p->second);
        }
    }

ArbitratedWritePort(Structure<I>& structure, const std::string& name)
    : PriorityArbitratedPort(structure, name), m_structure(structure)
    {
        m_structure.RegisterArbitratedWritePort(*this);
    }
    
    ~ArbitratedWritePort() {
        m_structure.UnregisterArbitratedWritePort(*this);
    }

    bool Write(const I& index)
    {
        const Process& process = *m_structure.GetKernel()->GetActiveProcess();
        assert(CanAccess(process));
        if (m_structure.GetKernel()->GetCyclePhase() == PHASE_ACQUIRE)
        {
            AddRequest(process, index);
            m_structure.RequestArbitration();
        }
        else if (!this->IsChosen() || !HasAcquired(process))
        {
            return false;
        }
        return true;
    }
};

//
// DedicatedPort
//
class DedicatedPort
{
public:
DedicatedPort(const Object& /*unused for now*/, const std::string& /* unused for now */) {}
    virtual ~DedicatedPort() {}

    void SetProcess(const Process& process) {
        m_process = &process;
    }
protected:
    bool CanAccess(const Process& process) {
        return (m_process == &process);
    }

private:
    const Process* m_process;
};

//
// DedicatedReadPort
//
class DedicatedReadPort : public DedicatedPort
{
    IStructure& m_structure;
public:
DedicatedReadPort(IStructure& structure, const std::string& name)
    : DedicatedPort(structure, name), m_structure(structure) {}
        
    bool Read() {
        // Dedicated Read ports always succeed -- they don't require arbitration
        assert(CanAccess( *m_structure.GetKernel()->GetActiveProcess() ));
        return true;
    }
};

//
// DedicatedWritePort
//
template <typename I>
    class DedicatedWritePort : public DedicatedPort, public WritePort<I>
{
    Structure<I>& m_structure;
public:
DedicatedWritePort(Structure<I>& structure, const std::string& name)
    : DedicatedPort(structure, name), m_structure(structure)
    {
        m_structure.RegisterWritePort(*this);
    }
    
    ~DedicatedWritePort() {
        m_structure.UnregisterWritePort(*this);
    }

    bool Write(const I& index) {
        assert(CanAccess( *m_structure.GetKernel()->GetActiveProcess() ));
        if (m_structure.GetKernel()->GetCyclePhase() == PHASE_ACQUIRE) {
            this->SetIndex(index);
            m_structure.RequestArbitration();
        } else if (!this->IsChosen()) {
            return false;
        }
        return true;
    }
};

//
// ArbitratedService
//
// An arbitrated service is like an arbitrated port, only there's no
// associated service. It's purpose is to arbitrate access to a single
// feature of a component.
//
template <typename Base = class PriorityArbitratedPort>
    class ArbitratedService : public Base, public Arbitrator
{
    void OnArbitrate()
    {
        this->Arbitrate();
    }
    
public:
    bool Invoke()
    {
        Kernel& kernel = m_clock.GetKernel();
        const Process& process = *kernel.GetActiveProcess();
        assert(this->CanAccess(process));
        if (kernel.GetCyclePhase() == PHASE_ACQUIRE) {
            this->AddRequest(process);
            RequestArbitration();
        } else if (!this->HasAcquired(process)) {
            return false;
        }
        return true;
    }
    
ArbitratedService(const Object& object, Clock& clock, const std::string& name)
    : Base(object, name), Arbitrator(clock)
    {
    }
    
    ~ArbitratedService() {
    }

    std::string GetFQN() const { return Base::GetFQN(); }


};

}
#endif

