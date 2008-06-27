#ifndef KERNEL_H
#define KERNEL_H

#include "simtypes.h"
#include "except.h"
#include "buffer.h"
#include <vector>
#include <map>
#include <set>

namespace Simulator
{

#define COMMIT  if (committing())

class Object;
class Mutex;
class Kernel;
class IComponent;
class IFunction;
class IStructure;
class IRegister;

enum CyclePhase {
    PHASE_ACQUIRE,
    PHASE_CHECK,
    PHASE_COMMIT
};

class IComponent;
class IStructure;
class IFunction;
class IRegister;

enum RunState
{
	STATE_RUNNING,
	STATE_IDLE,
	STATE_DEADLOCK,
};

class Kernel
{
public:
	struct CallbackInfo
	{
		unsigned int nStates;
		RunState     state;
	};

	typedef std::map<IComponent*, CallbackInfo> CallbackList;
    typedef std::set<IComponent*>               ComponentList;
    typedef std::set<IStructure*>               StructureList;
    typedef std::set<IFunction*>                FunctionList;
    typedef std::set<IRegister*>                RegisterList;

private:
    int			 m_debugMode;
    CycleNo      m_cycle;
    CyclePhase   m_phase;

    StructureList m_structures;
    CallbackList  m_callbacks;
    ComponentList m_components;
    FunctionList  m_functions;
    RegisterList  m_registers;

public:
	// Debug modes
    static const int DEBUG_SIM  = 1;
    static const int DEBUG_PROG = 2;

    Kernel();
    ~Kernel();

    void registerFunction (IFunction&  _function );
    void registerStructure(IStructure& _structure);
    void registerComponent(IComponent& _component, unsigned int numStates);
    void registerRegister (IRegister&  _register );
    void unregisterFunction (IFunction&  _function );
    void unregisterStructure(IStructure& _structure);
    void unregisterComponent(IComponent& _component);
    void unregisterRegister (IRegister&  _register );

    CycleNo             getCycleNo()    const;
    CyclePhase          getCyclePhase() const;
	const CallbackList& GetCallbacks()  const { return m_callbacks; }

	// Set and get the debug mode
    void debug(int mode);
    int  debug() const;

    RunState step(CycleNo cycles = 1);
};

// Common properties for simulator components
class Object
{
    Object*     m_parent;
    std::string m_name;
    Kernel*     m_kernel;
    std::vector<Object*> m_children;
public:
    Object(Object* parent, Kernel* kernel, const std::string& name);
    virtual ~Object();

    // Are we in phase X?
    bool acquiring()  const { return m_kernel->getCyclePhase() == PHASE_ACQUIRE; }
    bool checking()   const { return m_kernel->getCyclePhase() == PHASE_CHECK;   }
    bool committing() const { return m_kernel->getCyclePhase() == PHASE_COMMIT;  }

    Kernel*            getKernel() const { return m_kernel; }
    Object*            getParent() const { return m_parent; }
    const std::string& getName()   const { return m_name; }
    const std::string  getFQN()    const;
    unsigned int       getNumChildren() const { return (unsigned int)m_children.size(); }
    Object*            getChild(int i)  const { return m_children[i]; }

    void DebugSimWrite(const char* msg, ...) const;
    void DebugProgWrite(const char* msg, ...) const;
    void OutputWrite(const char* msg, ...) const;
};

class IFunction
{
public:
    virtual void onArbitrateReadPhase()  = 0;
    virtual void onArbitrateWritePhase() = 0;

    virtual ~IFunction() {}
};

class IRegister
{
    const Kernel& m_kernel;

protected:
    bool committing() const
    {
        return m_kernel.getCyclePhase() == PHASE_COMMIT;
    }

public:
    virtual void onUpdate() = 0;

	IRegister(Kernel& kernel) : m_kernel(kernel)
	{
        kernel.registerRegister(*this);
	}

    virtual ~IRegister() {}
};

class IStructure : public Object
{
public:
    virtual void onArbitrateReadPhase()  {}
    virtual void onArbitrateWritePhase() {}
    
    virtual bool empty() const { return true; }

    IStructure(Object* parent, Kernel& kernel, const std::string& name);
    ~IStructure();
};

class IComponent : public Object
{
public:
	// The result is used to detect idle and deadlocked componenents.
	// DELAYED: There's nothing to do
	// FAILED:  There's something to do but I can't do it
	// SUCCESS: There's something to do and I have done it
    virtual Result onCycleReadPhase(unsigned int stateIndex)  { return DELAYED; }
    virtual Result onCycleWritePhase(unsigned int stateIndex) { return DELAYED; }

    IComponent(Object* parent, Kernel& kernel, const std::string& name, unsigned int numStates = 1);
    ~IComponent();
};

}
#endif

