#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "kernel.h"
#include <map>
#include <set>

namespace Simulator
{

class ArbitratedFunction : public IFunction
{
    typedef std::set<const IComponent*>     RequestMap;
    typedef std::map<const IComponent*,int> PriorityMap;

public:
    ArbitratedFunction(Kernel& kernel) : m_kernel(kernel) {
        kernel.registerFunction(*this);
    }

    virtual ~ArbitratedFunction() {
        m_kernel.unregisterFunction(*this);
    }

    void setPriority(const IComponent& component, int priority) {
        m_priorities[&component] = priority;
    }

    void arbitrate();

protected:
    bool acquiring() const {
        return m_kernel.getCyclePhase() == PHASE_ACQUIRE;
    }

    bool acquired(const IComponent& component) {
        return &component == m_component;
    }

#ifndef NDEBUG
    void verify(const IComponent& component);
#else
    void verify(const IComponent& component) {}
#endif

    void addRequest(const IComponent& component) {
        m_requests.insert(&component);
    }

private:
    PriorityMap m_priorities;
    RequestMap  m_requests;
    Kernel&     m_kernel;
    
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
    void verify(const IComponent& component);
#else
    void verify(const IComponent& component) {}
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

    virtual ~ReadFunction() {}
};

class WriteFunction
{
public:
    virtual bool invoke(const IComponent& component) = 0;

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

