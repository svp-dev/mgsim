#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "kernel.h"
#include <map>
#include <set>

namespace Simulator
{

class ArbitratedFunction : public IFunction
{
    typedef std::map<int, const IComponent*> RequestMap;
    typedef std::map<const IComponent*,int>  PriorityMap;

public:
    ArbitratedFunction(const ArbitratedFunction& f)
        : m_priorities(f.m_priorities), m_requests(f.m_requests), m_kernel(f.m_kernel),
          m_priority(f.m_priority), m_component(f.m_component)
    {
        m_kernel.registerFunction(*this);
    }

    ArbitratedFunction(Kernel& kernel)
        : m_kernel(kernel), m_priority(INT_MAX), m_component(NULL)
    {
        m_kernel.registerFunction(*this);
    }

    virtual ~ArbitratedFunction() {
        m_kernel.unregisterFunction(*this);
    }

    void setPriority(const IComponent& component, int priority);
    void arbitrate();

protected:
    bool acquiring() const {
        return m_kernel.getCyclePhase() == PHASE_ACQUIRE;
    }

    bool acquired(const IComponent& component) const {
        return &component == m_component;
    }

    bool acquired(int priority) const {
        return m_priority == priority;
    }

#ifndef NDEBUG
    void verify(const IComponent& component) const;
#else
    void verify(const IComponent& component) const {}
#endif

    void addRequest(const IComponent& component);
    void addRequest(int priority);

private:
    PriorityMap m_priorities;
    RequestMap  m_requests;
    Kernel&     m_kernel;
    
    int               m_priority;
    const IComponent* m_component;
};

class DedicatedFunction
{
public:
    DedicatedFunction() { m_component = NULL; }
    virtual ~DedicatedFunction() {}

    virtual void setComponent(const IComponent& component) {
        m_component = &component;
    }

protected:
#ifndef NDEBUG
    void verify(const IComponent& component) const;
#else
    void verify(const IComponent& component) const {}
#endif

private:
    const IComponent* m_component;
};

//
// Read and Write function
//
class ReadFunction
{
public:
    virtual bool invoke(const IComponent& component) = 0;
    virtual bool invoke(int priority) = 0;

    virtual ~ReadFunction() {}
};

class WriteFunction
{
public:
    virtual bool invoke(const IComponent& component) = 0;
    virtual bool invoke(int priority) = 0;

    virtual ~WriteFunction() {}
};

//
// Arbitrated Functions
//
class ArbitratedReadFunction : public ReadFunction, public ArbitratedFunction
{
public:
    ArbitratedReadFunction(Kernel& kernel) : ArbitratedFunction(kernel) {}
    void onArbitrateReadPhase()               { arbitrate(); }
    void onArbitrateWritePhase()              {}
    bool invoke(const IComponent& component)
    {
        verify(component);
        if (acquiring())
        {
            addRequest(component);
        }
        else if (!acquired(component))
        {
            return false;
        }
        return true;
    }

    bool invoke(int priority)
    {
        if (acquiring())
        {
            addRequest(priority);
        }
        else if (!acquired(priority))
        {
            return false;
        }
        return true;
    }
};

class ArbitratedWriteFunction : public WriteFunction, public ArbitratedFunction
{
public:
    ArbitratedWriteFunction(Kernel& kernel) : ArbitratedFunction(kernel) {}
    void onArbitrateReadPhase()               {}
    void onArbitrateWritePhase()              { arbitrate(); }
    bool invoke(const IComponent& component)
    {
        verify(component);
        if (acquiring())
        {
            addRequest(component);
        }
        else if (!acquired(component))
        {
            return false;
        }
        return true;
    }
    
    bool invoke(int priority)
    {
        if (acquiring())
        {
            addRequest(priority);
        }
        else if (!acquired(priority))
        {
            return false;
        }
        return true;
    }
};

//
// Dedicated Functions
//
class DedicatedReadFunction : public ReadFunction, public DedicatedFunction
{
public:
    bool invoke(const IComponent& component) {
        verify(component);
        return true;
    }
};

class DedicatedWriteFunction : public WriteFunction, public DedicatedFunction
{
public:
    bool invoke(const IComponent& component) {
        verify(component);
        return true;
    }
};

}

#endif

