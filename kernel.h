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

#define COMMIT  if (IsCommitting())

class Object;
class Mutex;
class Kernel;
class IComponent;
class IFunction;
class IStructure;
class IRegister;

/**
 * Enumeration for the phases inside a cycle
 */
enum CyclePhase {
    PHASE_ACQUIRE,  ///< Acquire phase, all components indicate their wishes.
    PHASE_CHECK,    ///< Check phase, all components verify that they can continue.
    PHASE_COMMIT    ///< Commit phase, all components commit their cycle.
};

class IComponent;
class IStructure;
class IFunction;
class IRegister;

/**
 * Enumeration for the run states of components
 */
enum RunState
{
	STATE_RUNNING,  ///< The component is running.
	STATE_IDLE,     ///< The component has no work.
	STATE_DEADLOCK, ///< The component has work but cannot continue.
	STATE_ABORTED,  ///< The simulation has been aborted.
};

/**
 * @brief Component-manager class
 * The kernel class is the manager for all components in the simulation. It advances
 * time, calls the cycle callbacks on all components, initiates arbitration and more.
 */
class Kernel
{
public:
    struct ComponentState
    {
        std::string name;   ///< Name of this state
        RunState    state;  ///< Last run state of this state
    };
    
    /// Holds information about a component's callbacks struct C
	struct CallbackInfo
	{
	    std::vector<ComponentState> states;   ///< The component's states
	};

	typedef std::map<IComponent*, CallbackInfo> CallbackList;   ///< List of callbacks, indexed by component.
    typedef std::set<IComponent*>               ComponentList;  ///< List of unique components.
    typedef std::set<IStructure*>               StructureList;  ///< List of unique structures.
    typedef std::set<IFunction*>                FunctionList;   ///< List of unique functions.
    typedef std::set<IRegister*>                RegisterList;   ///< List of unique registers.

    /// Modes of debugging
    enum DebugMode
    {
        DEBUG_SIM      = 1, ///< Debug the simulator 
        DEBUG_PROG     = 2, ///< Debug the program
        DEBUG_DEADLOCK = 4, ///< Debug deadlocks
    };
    
private:
    bool         m_aborted;         ///< Should the run be aborted?
    int			 m_debugMode;       ///< Bit mask of enabled debugging modes.
    CycleNo      m_cycle;           ///< Current cycle of the simulation.
    CyclePhase   m_phase;           ///< Current sub-cycle phase of the simulation.

    StructureList m_structures;     ///< List of all structures in the simulation.
    CallbackList  m_callbacks;      ///< List of all callbacks in the simulation.
    ComponentList m_components;     ///< List of all components in the simulation.
    FunctionList  m_functions;      ///< List of all functions in the simulation.
    RegisterList  m_registers;      ///< List of all registers in the simulation.

public:
    Kernel();
    ~Kernel();

    /// Registers a function to the kernel. @param function the function to register.
    void RegisterFunction (IFunction&  function );
    /// Registers a structure to the kernel. @param structure the structure to register.
    void RegisterStructure(IStructure& structure);
    /**
     * Registers a component to the kernel.
     * @param component the component to register.
     * @param states '|'-delimited list of state names
     */
    void RegisterComponent(IComponent& component, const std::string& states);
    /// Registers a register to the kernel. @param reg the register to register.
    void RegisterRegister (IRegister&  reg );

    /// Unregisters a function from the kernel. @param function the function to unregister.
    void UnregisterFunction (IFunction&  function );
    /// Unregisters a structure from the kernel. @param structure the structure to unregister.
    void UnregisterStructure(IStructure& structure);
    /// Unregisters a component from the kernel. @param component the component to unregister.
    void UnregisterComponent(IComponent& component);
    /// Unregisters a register from the kernel. @param reg the register to unregister.
    void UnregisterRegister (IRegister&  reg );

    /**
     * @brief Get the cycle counter.
     * Gets the current cycle counter of the simulation.
     * @return the current cycle counter.
     */
    CycleNo GetCycleNo() const;
    
    /**
     * @brief Get the cycle phase.
     * Gets the current sub-cycle phase of the simulation.
     * @return the current sub-cycle phase.
     */
    CyclePhase GetCyclePhase() const;
    
    /**
     * Sets the debug mode.
     * @param mode the debug mode to set.
     */
    void SetDebugMode(int mode);
    
    /**
     * Gets the current debug mode.
     * @return the current debug mode.
     */
    int GetDebugMode() const;

    /**
     * @brief Advances the simulation.
     * Advances the simulation by the specified number of cycles. It will abort early if
     * the simulation reaches deadlock.
     * @param cycles The number of cycles to advance the simulation with.
     * @return the state of simulation afterwards.
     */
    RunState Step(CycleNo cycles = 1);
    
    /**
     * @brief Aborts the simulation
     * Aborts the current simulation, in Step(). This is best called asynchronously,
     * from a signal handler. Step() will return STATE_ABORTED.
     */
    void Abort();

    /**
     * @brief Get all callbacks.
     * Gets the list of all callbacks in the simulation.
     * @return a constant reference to the list of all callbacks.
     */
	const CallbackList& GetCallbacks() const { return m_callbacks; }
};

/**
 * @brief Base class for simulator components.
 * The Object class is the base object for all simulated components, offering
 * interaction with the system. Objects are linked in a hierarchy and each
 * is managed by a kernel, to which it must register. It will unregister from
 * its kernel when destroyed.
 */
class Object
{
    Object*              m_parent;      ///< Parent object.
    std::string          m_name;        ///< Object name.
    Kernel*              m_kernel;      ///< Kernel managing this object.
    std::vector<Object*> m_children;    ///< Children of this object
    
public:
    /**
     * Constructs the object.
     * @param parent the parent object.
     * @param kernel the kernel which will manage this object.
     * @param name the name of this object.
     */
    Object(Object* parent, Kernel* kernel, const std::string& name);
    virtual ~Object();

    /// Check if the simulation is in the acquiring phase. @return true if the simulation is in the acquiring phase.
    bool IsAcquiring()  const { return m_kernel->GetCyclePhase() == PHASE_ACQUIRE; }
    /// Check if the simulation is in the check phase. @return true if the simulation is in the check phase.
    bool IsChecking()   const { return m_kernel->GetCyclePhase() == PHASE_CHECK;   }
    /// Check if the simulation is in the commit phase. @return true if the simulation is in the commit phase.
    bool IsCommitting() const { return m_kernel->GetCyclePhase() == PHASE_COMMIT;  }

    /// Get the kernel managing this object. @return the kernel managing this object.
    Kernel*            GetKernel() const { return m_kernel; }
    /// Get the parent object. @return the parent object.
    Object*            GetParent() const { return m_parent; }
    /// Get the number of children of the object. @return the number of children.
    unsigned int       GetNumChildren() const { return (unsigned int)m_children.size(); }
    /// Get a child of the object. @param i the index of the child. @return the child at index i.
    Object*            GetChild(int i)  const { return m_children[i]; }
    /// Get the object name. @return the object name.
    const std::string& GetName()   const { return m_name; }

    /**
     * @brief Get the object's Fully Qualified Name.
     * The objectś Fully Qualified Name, or FQN, is the name of the object and
     * all its parent up to the root, with periods seperating the names.
     * @return the FQN of the object.
     */
    const std::string  GetFQN()    const;

    /**
     * @brief Writes simulator debug output.
     * Writes debug output if and only if the object's kernel's debug more contains at least DEBUG_SIM.
     * @param msg the printf-style format string.
     */
    void DebugSimWrite(const char* msg, ...) const;

    /**
     * @brief Writes program debug output.
     * Writes debug output if and only if the object's kernel's debug more contains at least DEBUG_PROG.
     * @param msg the printf-style format string.
     */
    void DebugProgWrite(const char* msg, ...) const;

    /**
     * @brief Writes deadlock debug output.
     * Writes debug output if and only if the object's kernel's debug more contains at least DEBUG_DEADLOCK.
     * @param msg the printf-style format string.
     */
    void DeadlockWrite(const char* msg, ...) const;

    /// Writes output. @param msg the printf-style format string.
    void OutputWrite(const char* msg, ...) const;
};

/// Base class for all functions in the simulation.
class IFunction
{
public:
    virtual void OnArbitrateReadPhase()  = 0; ///< Callback for arbitration in the read phase.
    virtual void OnArbitrateWritePhase() = 0; ///< Callback for arbitration in the write phase.

    virtual ~IFunction() {}
};

/// Base class for all registers in the simulation.
class IRegister
{
    const Kernel& m_kernel; ///< Kernel that manages this register

protected:
    /// Check if the simulation is in the commit phase. @return true if the simulation is in the commit phase.
    bool IsCommitting() const
    {
        return m_kernel.GetCyclePhase() == PHASE_COMMIT;
    }

public:
    virtual void OnUpdate() = 0;    ///< Callback to affect any pending writes to the register.

    /**
     * Constructs the register
     * @param kernel the kernel which will manage this register.
     */
	IRegister(Kernel& kernel) : m_kernel(kernel)
	{
        kernel.RegisterRegister(*this);
	}

    virtual ~IRegister() {}
};

/// Base class for all structures in the simulation.
class IStructure : public Object
{
public:
    virtual void OnArbitrateReadPhase()  {} ///< Callback for arbitration in the read phase.
    virtual void OnArbitrateWritePhase() {} ///< Callback for arbitration in the write phase.
    
    /**
     * Constructs the structure.
     * @param parent parent object.
     * @param kernel the kernel which will manage this structure.
     * @param name name of the object.
     */
    IStructure(Object* parent, Kernel& kernel, const std::string& name);
    ~IStructure();
};

/// Base class for all components in the simulation.
class IComponent : public Object
{
public:
	/**
	 * @brief Per-cycle read-phase callback handler.
	 * Called every cycle in the read phase. The state index is a number between
	 * 0 and the number of states in the component as indicated at the constructor.
	 * Should a state not be able to proceed, other states will still be tried
	 * independently.
	 * @param stateIndex the current index of the state that should be handled.
	 * @return a value used to detect idle and deadlocked components:
	 * - DELAYED: There's nothing to do
	 * - FAILED:  There's something to do but I can't do it
 	 * - SUCCESS: There's something to do and I have done it
	 */
    virtual Result OnCycleReadPhase(unsigned int stateIndex)  { return DELAYED; }
    virtual Result OnCycleWritePhase(unsigned int stateIndex) { return DELAYED; }
    virtual void UpdateStatistics() {}

    /**
     * Constructs the component.
     * @param parent the parent object.
     * @param kernel the kernel that will manage this component.
     * @param name the name of tehe compnent.
     * @param states '|'-delimited list of state names
     */
    IComponent(Object* parent, Kernel& kernel, const std::string& name, const std::string& states = "default");
    ~IComponent();
};

}
#endif

