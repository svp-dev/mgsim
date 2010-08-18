#ifndef KERNEL_H
#define KERNEL_H

#include "except.h"
#include "delegate.h"

#include <vector>
#include <map>
#include <set>
#include <cassert>

class SymbolTable;
class BreakPoints;

namespace Simulator
{

#define COMMIT  if (IsCommitting())

class Object;
class Mutex;
class Kernel;
class Arbitrator;
class Storage;
class IRegister;
class Display;

/**
 * Enumeration for the run states of processes
 */
enum RunState
{
    STATE_ACTIVE,   ///< The component has been activated.
	STATE_RUNNING,  ///< The component is running.
	STATE_IDLE,     ///< The component has no work.
	STATE_DEADLOCK, ///< The component has work but cannot continue.
	STATE_ABORTED,  ///< The simulation has been aborted.
};

// Processes are member variables in components and represent the information
// about a single process in that component.
class Process
{
    friend class Kernel;
    
    const std::string m_name;          ///< The name of this process
    const delegate    m_delegate;      ///< The callback for the execution of the process
    RunState          m_state;         ///< Last run state of this process
    unsigned int      m_activations;   ///< Reference count of activations of this process
    Process*          m_next;          ///< Next pointer in the list of processes that require updates
    Process**         m_pPrev;         ///< Prev pointer in the list of processes that require updates

    // Processes are non-copyable and non-assignable
    Process(const Process&);
    void operator=(const Process&);
    
public:
    const Process* GetNext()   const { return m_next;  }
    RunState       GetState()  const { return m_state; }
    Object*        GetObject() const { return m_delegate.GetObject(); }
    std::string    GetName()   const;
    
    void Deactivate();
        
    Process(const std::string& name, const delegate& delegate)
        : m_name(name), m_delegate(delegate), m_state(STATE_IDLE), m_activations(0)
    {
    }
};

/**
 * Enumeration for the phases inside a cycle
 */
enum CyclePhase {
    PHASE_ACQUIRE,  ///< Acquire phase, all components indicate their wishes.
    PHASE_CHECK,    ///< Check phase, all components verify that they can continue.
    PHASE_COMMIT    ///< Commit phase, all components commit their cycle.
};

// Define these methods as macros to allow for optimizations
#define DebugSimWrite(msg, ...)  do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_SIM ) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugSimWrite_((msg), ##__VA_ARGS__); } while(false)
#define DebugProgWrite(msg, ...) do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_PROG) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugProgWrite_((msg), ##__VA_ARGS__); } while(false)
#define DebugFlowWrite(msg, ...) do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_FLOW) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugProgWrite_((msg), ##__VA_ARGS__); } while(false)
#define DeadlockWrite(msg, ...)  do { if (GetKernel()->GetDebugMode() & Kernel::DEBUG_DEADLOCK) DeadlockWrite_((msg), ##__VA_ARGS__); } while(false)
#define OutputWrite(msg, ...)    do { if (GetKernel()->GetCyclePhase() == PHASE_COMMIT) OutputWrite_((msg), ##__VA_ARGS__); } while(false)

/**
 * @brief Component-manager class
 * The kernel class is the manager for all components in the simulation. It advances
 * time, calls the cycle callbacks on all components, initiates arbitration and more.
 */
class Kernel
{
    friend class Object;
    
public:
    /// Modes of debugging
    enum DebugMode
    {
        DEBUG_SIM      = 1, ///< Debug the simulator 
        DEBUG_PROG     = 2, ///< Debug the program
        DEBUG_DEADLOCK = 4, ///< Debug deadlocks
        DEBUG_FLOW     = 8, ///< Debug control flow
    };
    
private:
    bool         m_aborted;           ///< Should the run be aborted?
    int		 m_debugMode;         ///< Bit mask of enabled debugging modes.
    CycleNo      m_cycle;             ///< Current cycle of the simulation.
    Display&     m_display;           ///< The display to manage.
    SymbolTable& m_symtable;          ///< The symbol table for debugging.
    BreakPoints& m_breakpoints;       ///< The breakpoint checker for debugging.
    CyclePhase   m_phase;             ///< Current sub-cycle phase of the simulation.
    Process*     m_process;           ///< The currently executing process.
    bool         m_debugging;         ///< Are we in a debug trace?

    Process*     m_activeProcesses;   ///< List of processes that need to be run.
    Storage*     m_activeStorages;    ///< List of storages that need to be updated.
    Arbitrator*  m_activeArbitrators; ///< List of arbitrators that need arbitration.

    bool UpdateStorages();
public:
    Kernel(Display& display, SymbolTable& symtable, BreakPoints& breakpoints);
    ~Kernel();
    
    /**
     * @brief Register an update request for the specified storage at the end of the cycle.
     * @param storage The storage to update
     * @return the next storage that require update
     */
    Storage* ActivateStorage(Storage& storage)
    {
        Storage* next = m_activeStorages;
        m_activeStorages = &storage;
        return next;
    }
    
    Arbitrator* ActivateArbitrator(Arbitrator& arbitrator)
    {
        Arbitrator* next = m_activeArbitrators;
        m_activeArbitrators = &arbitrator;
        return next;
    }
    
    /**
     * @brief Schedule the specified process on the run queue.
     * @param process The process to schedule
     */
    void ActivateProcess(Process& process);

    /**
     * @brief Get the currently executing process
     */
    inline const Process* GetActiveProcess() const { return m_process; }

    /**
     * @brief Get the currently scheduled processes
     */
    inline const Process* GetActiveProcesses() const { return m_activeProcesses; }

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
     * Sets the debug flags.
     * @param mode the debug flags to set (from enum DebugMode).
     */
    void SetDebugMode(int flags);

    /**
     * Toggle the debug flags.
     * @param mode the debug flags to toggle (from enum DebugMode).
     */
    void ToggleDebugMode(int flags);
    
    /**
     * Gets the current debug flags.
     * @return the current debug flags.
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
	//const ComponentList& GetComponents() const { return m_components; }

    inline Display& GetDisplay() const { return m_display; }
    inline SymbolTable& GetSymbolTable() const { return m_symtable; }
    inline BreakPoints& GetBreakPoints() const { return m_breakpoints; }
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
    Kernel&              m_kernel;      ///< Kernel managing this object.
    std::vector<Object*> m_children;    ///< Children of this object
    
public:
    /**
     * Constructs a root object.
     * @param name the name of this object.
     */
    Object(const std::string& name, Kernel& kernel);
    
    /**
     * Constructs a child object, using the same kernel as the parent
     * @param parent the parent object.
     * @param name the name of this object.
     */
    Object(const std::string& name, Object& parent);

    /**
     * Constructs a child object.
     * @param parent the parent object.
     * @param kernel the kernel which will manage this object.
     * @param name the name of this object.
     */
    Object(const std::string& name, Object& parent, Kernel& kernel);

    virtual ~Object();

    /// Check if the simulation is in the acquiring phase. @return true if the simulation is in the acquiring phase.
    bool IsAcquiring()  const { return m_kernel.GetCyclePhase() == PHASE_ACQUIRE; }
    /// Check if the simulation is in the check phase. @return true if the simulation is in the check phase.
    bool IsChecking()   const { return m_kernel.GetCyclePhase() == PHASE_CHECK;   }
    /// Check if the simulation is in the commit phase. @return true if the simulation is in the commit phase.
    bool IsCommitting() const { return m_kernel.GetCyclePhase() == PHASE_COMMIT;  }

    /// Get the kernel managing this object. @return the kernel managing this object.
    Kernel*            GetKernel() const { return &m_kernel; }
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
    void DebugSimWrite_(const char* msg, ...) const FORMAT_PRINTF(2,3);

    /**
     * @brief Writes program debug output.
     * Writes debug output if and only if the object's kernel's debug more contains at least DEBUG_PROG.
     * @param msg the printf-style format string.
     */
    void DebugProgWrite_(const char* msg, ...) const FORMAT_PRINTF(2,3);

    /**
     * @brief Writes deadlock debug output.
     * Writes debug output if and only if the object's kernel's debug more contains at least DEBUG_DEADLOCK.
     * @param msg the printf-style format string.
     */
    void DeadlockWrite_(const char* msg, ...) const FORMAT_PRINTF(2,3);

    /// Writes output. @param msg the printf-style format string.
    void OutputWrite_(const char* msg, ...) const FORMAT_PRINTF(2,3);
};

/// Base class for all objects that arbitrate
class Arbitrator
{
    friend class Kernel;

    bool        m_activated;  ///< Has the arbitrator already been activated this cycle?
    Arbitrator* m_next;       ///< Next pointer in the list of arbitrators that require arbitration

protected:
    Kernel& m_kernel;
    
    void RequestArbitration()
    {
        if (!m_activated) {
            m_next = m_kernel.ActivateArbitrator(*this);
            m_activated = true;
        }
    }

public:
    ///< Callback for arbitration
    virtual void OnArbitrate() = 0;
    
    Arbitrator(Kernel& kernel) : m_activated(false), m_kernel(kernel)
    {}
    
    virtual ~Arbitrator() {}
};

}
#endif

