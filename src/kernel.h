#ifndef KERNEL_H
#define KERNEL_H

#include "simtypes.h"
#include "except.h"
#include <vector>
#include <map>
#include <set>
#include <cassert>

namespace Simulator
{

#define COMMIT  if (IsCommitting())

class Storage;
class Object;
class Mutex;
class Kernel;
class Arbitrator;
class IComponent;
class IRegister;

/**
 * Enumeration for the phases inside a cycle
 */
enum CyclePhase {
    PHASE_ACQUIRE,  ///< Acquire phase, all components indicate their wishes.
    PHASE_CHECK,    ///< Check phase, all components verify that they can continue.
    PHASE_COMMIT    ///< Commit phase, all components commit their cycle.
};

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
    friend class Object;
    
public:
    struct ComponentInfo;

    /// Holds information a component's process
    struct ProcessInfo
    {
        ComponentInfo* info;          ///< The component it's part of
        std::string    name;          ///< Name of this process
        RunState       state;         ///< Last run state of this process
        unsigned int   activations;   ///< Reference count of activations of this process
        ProcessInfo*   next;          ///< Next pointer in the list of processes that require updates
        ProcessInfo**  pPrev;         ///< Prev pointer in the list of processes that require updates
    };
    
    /// Holds information about a component
	struct ComponentInfo
	{
	    IComponent*              component; ///< The component
	    std::vector<ProcessInfo> processes; ///< The component's processes
	};
	
	/// Holds information about an arbitrator
	struct ArbitratorInfo
	{
	    Arbitrator*     arbitrator; ///< The arbitrator object
	    bool            activated;  ///< Has the arbitrator already been activated this cycle?
	    ArbitratorInfo* next;       ///< Next pointer in the list of arbitrators that require arbitration
	};
	
	/// Holds information about a storage object
	struct StorageInfo
	{
	    Storage*     storage;   ///< The storage object
	    bool         activated; ///< Has the storage already been activated this cycle?
	    StorageInfo* next;      ///< Next pointer in the list of storages that require updates
	};

	typedef std::vector<ComponentInfo>     ComponentList;  ///< List of components.
    typedef std::vector<ArbitratorInfo>    ArbitratorList; ///< List of unique arbitrators.
    typedef std::vector<StorageInfo>       StorageList;    ///< List of storages.
    typedef std::pair<ComponentInfo*, int> Process;        ///< Type of a process

    /// Modes of debugging
    enum DebugMode
    {
        DEBUG_SIM      = 1, ///< Debug the simulator 
        DEBUG_PROG     = 2, ///< Debug the program
        DEBUG_DEADLOCK = 4, ///< Debug deadlocks
    };
    
private:
    bool            m_aborted;           ///< Should the run be aborted?
    int		        m_debugMode;         ///< Bit mask of enabled debugging modes.
    CycleNo         m_cycle;             ///< Current cycle of the simulation.
    CyclePhase      m_phase;             ///< Current sub-cycle phase of the simulation.
    ProcessInfo*    m_process;           ///< The currently executing process.
    bool            m_debugging;         ///< Are we in a debug trace?

    ComponentList   m_components;        ///< List of all components.
    ArbitratorList  m_arbitrators;       ///< List of all arbitrators.
    StorageList     m_storages;          ///< List of all storages.
    
    ProcessInfo*    m_activeProcesses;   ///< List of processes that need to be run.
    StorageInfo*    m_activeStorages;    ///< List of storages that need to be updated.
    ArbitratorInfo* m_activeArbitrators; ///< List of arbitrators that need arbitration.

    void UpdateStorages();
public:
    Kernel();
    ~Kernel();
    
    void Initialize();
    
    ProcessInfo* GetProcessInfo(IComponent* component, int state);
    
    /**
     * @brief Register an update request for the specified storage at the end of the cycle.
     * @param storage The storage to update
     */
    void ActivateStorage(StorageInfo* storage)
    {
        if (!storage->activated) {
            storage->next = m_activeStorages;
            storage->activated = true;
            m_activeStorages = storage;
        }
    }
    
    void ActivateArbitrator(ArbitratorInfo* arbitrator)
    {
        if (!arbitrator->activated) {
            arbitrator->next = m_activeArbitrators;
            arbitrator->activated = true;
            m_activeArbitrators = arbitrator;
        }
    }
    
    /**
     * @brief Schedule the specified process on the run queue.
     * @param process The process to schedule
     */
    void ActivateProcess(ProcessInfo* process);

    /**
     * @brief Registers an arbitrator to the kernel.
     * @param structure the structure to register.
     */
    void RegisterArbitrator(Arbitrator& arbitrator);
    
    /**
     * @brief Registers a component to the kernel.
     * @param component the component to register.
     * @param states '|'-delimited list of state names
     */
    void RegisterComponent(IComponent& component, const std::string& states);

    /**
     * @brief Registers a storage to the kernel.
     * @param storage the storage to register.
     */
    void RegisterStorage(Storage& storage);
    
    /**
     * @brief Get the currently executing process
     */
    inline const ProcessInfo* GetActiveProcess() const { return m_process; }

    /**
     * @brief Get the cycle counter.
     * Gets the current cycle counter of the simulation.
     * @return the current cycle counter.
     */
    inline CycleNo GetCycleNo() const { return m_cycle; }
    
    /**
     * @brief Get the cycle phase.
     * Gets the current sub-cycle phase of the simulation.
     * @return the current sub-cycle phase.
     */
    inline CyclePhase GetCyclePhase() const { return m_phase; }
    
    /**
     * Sets the debug mode.
     * @param mode the debug mode to set.
     */
    void SetDebugMode(int mode);
    
    /**
     * Gets the current debug mode.
     * @return the current debug mode.
     */
    inline int GetDebugMode() const { return m_debugMode; }

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
     * @brief Get all components.
     * Gets the list of all components in the simulation.
     * @return a constant reference to the list of all components.
     */
	const ComponentList& GetComponents() const { return m_components; }
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
    void DebugSimWrite(const char* msg, ...) const FORMAT_PRINTF(2,3);

    /**
     * @brief Writes program debug output.
     * Writes debug output if and only if the object's kernel's debug more contains at least DEBUG_PROG.
     * @param msg the printf-style format string.
     */
    void DebugProgWrite(const char* msg, ...) const FORMAT_PRINTF(2,3);

    /**
     * @brief Writes deadlock debug output.
     * Writes debug output if and only if the object's kernel's debug more contains at least DEBUG_DEADLOCK.
     * @param msg the printf-style format string.
     */
    void DeadlockWrite(const char* msg, ...) const FORMAT_PRINTF(2,3);

    /// Writes output. @param msg the printf-style format string.
    void OutputWrite(const char* msg, ...) const FORMAT_PRINTF(2,3);
};

/// Base class for all objects that arbitrate
class Arbitrator
{
    Kernel::ArbitratorInfo* m_handle;

protected:
    Kernel& m_kernel;
    
    void RequestArbitration()
    {
        m_kernel.ActivateArbitrator(m_handle);
    }
    
public:
    ///< Callback for arbitration
    virtual void OnArbitrate() = 0;
    
    void Initialize(Kernel::ArbitratorInfo* handle)
    {
        assert(m_handle == NULL);
        assert(handle != NULL);
        m_handle = handle;
    }
    
    Arbitrator(Kernel& kernel) : m_handle(NULL), m_kernel(kernel)
    {
        kernel.RegisterArbitrator(*this);
    }
    virtual ~Arbitrator() {}
};

/// Base class for all components in the simulation.
class IComponent : public Object
{
public:
	/**
	 * @brief Per-cycle callback handler.
	 * @details Called every cycle. The state index is a number between
	 * 0 and the number of states in the component as indicated at the constructor.
	 * Should a state not be able to proceed, other states will still be tried
	 * independently. Equivalent to a 'process'.
	 * A process will only be called when its at least of the structures it's
	 * sensitive is full, so a process should rarely have nothing to do when called.
	 * @param stateIndex the current index of the state that should be handled.
	 * @return a value used to detect idle and deadlocked components:
	 * - DELAYED: There's nothing to do
	 * - FAILED:  There's something to do but I can't do it
 	 * - SUCCESS: There's something to do and I have done it
	 */
    virtual Result OnCycle(unsigned int /*stateIndex*/) { return DELAYED; }
    
    virtual void UpdateStatistics() {}

    /**
     * @brief Constructs the component
     * @param parent the parent object.
     * @param kernel the kernel that will manage this component.
     * @param name the name of tehe compnent.
     * @param states '|'-delimited list of state names
     */
    IComponent(Object* parent, Kernel& kernel, const std::string& name, const std::string& states = "default");
    
    /// Destroys the component
    ~IComponent();
};

}
#endif

