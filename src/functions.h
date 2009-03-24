#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "kernel.h"
#include <map>
#include <set>
#include <limits>

namespace Simulator
{

class ArbitratedFunction : public IFunction
{
    typedef std::map<int, const IComponent*> RequestMap;
    typedef std::map<const IComponent*,int>  PriorityMap;

public:
    ArbitratedFunction(const ArbitratedFunction& f)
      : IFunction(), m_priorities(f.m_priorities), m_requests(f.m_requests), m_kernel(f.m_kernel),
          m_priority(f.m_priority), m_component(f.m_component)
    {
        m_kernel.RegisterFunction(*this);
    }

    ArbitratedFunction(Kernel& kernel)
        : m_kernel(kernel), m_priority(std::numeric_limits<int>::max()), m_component(NULL)
    {
        m_kernel.RegisterFunction(*this);
    }

    ~ArbitratedFunction() {
        m_kernel.UnregisterFunction(*this);
    }

    void SetPriority(const IComponent& component, int priority);
    void Arbitrate();

protected:
    bool IsAcquiring() const {
        return m_kernel.GetCyclePhase() == PHASE_ACQUIRE;
    }

    bool HasAcquired(const IComponent& component) const {
        return &component == m_component;
    }

    bool HasAcquired(int priority) const {
        return m_priority == priority;
    }

#ifndef NDEBUG
    void Verify(const IComponent& component) const;
#else
    void Verify(const IComponent& /* component */) const {}
#endif

    void AddRequest(const IComponent& component);
    void AddRequest(int priority);

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
    DedicatedFunction() : m_component(NULL) {}
    virtual ~DedicatedFunction() {}

    void SetComponent(const IComponent& component) {
        m_component = &component;
    }

protected:
#ifndef NDEBUG
    void Verify(const IComponent& component) const;
#else
    void Verify(const IComponent& /* component */) const {}
#endif

private:
    const IComponent* m_component;
};

//
// Arbitrated Functions
//
class ArbitratedReadFunction : public ArbitratedFunction
{
public:
    ArbitratedReadFunction(Kernel& kernel) : ArbitratedFunction(kernel) {}
    void OnArbitrateReadPhase()               { Arbitrate(); }
    void OnArbitrateWritePhase()              {}
    bool Invoke(const IComponent& component)
    {
        Verify(component);
        if (IsAcquiring())
        {
            AddRequest(component);
        }
        else if (!HasAcquired(component))
        {
            return false;
        }
        return true;
    }

    bool Invoke(int priority)
    {
        if (IsAcquiring())
        {
            AddRequest(priority);
        }
        else if (!HasAcquired(priority))
        {
            return false;
        }
        return true;
    }
};

class ArbitratedWriteFunction : public ArbitratedFunction
{
public:
    ArbitratedWriteFunction(Kernel& kernel) : ArbitratedFunction(kernel) {}
    void OnArbitrateReadPhase()               {}
    void OnArbitrateWritePhase()              { Arbitrate(); }
    bool Invoke(const IComponent& component)
    {
        Verify(component);
        if (IsAcquiring())
        {
            AddRequest(component);
        }
        else if (!HasAcquired(component))
        {
            return false;
        }
        return true;
    }
    
    bool Invoke(int priority)
    {
        if (IsAcquiring())
        {
            AddRequest(priority);
        }
        else if (!HasAcquired(priority))
        {
            return false;
        }
        return true;
    }
};

//
// Dedicated Functions
//
class DedicatedReadFunction : public DedicatedFunction
{
public:
    bool Invoke(const IComponent& component) {
        Verify(component);
        return true;
    }
};

class DedicatedWriteFunction : public DedicatedFunction
{
public:
    bool Invoke(const IComponent& component) {
        Verify(component);
        return true;
    }
};

}

#endif

