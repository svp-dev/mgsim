#ifndef KERNEL_H
#define KERNEL_H

#include "except.h"
#include "delegate.h"
#include "types.h"
#include "storagetrace.h"

#include <vector>
#include <map>
#include <set>
#include <cassert>

namespace Simulator
{

#define COMMIT  if (IsCommitting())

class Object;
class Mutex;
class Kernel;
class Arbitrator;
class IRegister;
class Process;
class BreakPointManager;

/// Cycle Number
typedef uint64_t CycleNo;

/// Value representing forever (infinite cycles)
static const CycleNo INFINITE_CYCLES = (CycleNo)-1;

/*
 * A clock class to place processes in a frequency domain.
 * This is only an interface to pass around. Clocks are created
 * by the kernel.
 */
class Clock
{
    friend class Kernel;

    Kernel&            m_kernel;      ///< The kernel that controls this clock and all components based off it
    unsigned long long m_frequency;   ///< Frequency of this clock, in MHz
    unsigned long long m_period;      ///< No. master-cycles per tick of this clock.
    Clock*             m_next;        ///< Next clock to run
    CycleNo            m_cycle;       ///< Next cycle this clock needs to run

    Process*     m_activeProcesses;   ///< List of processes that need to be run.
    Storage*     m_activeStorages;    ///< List of storages that need to be updated.
    Arbitrator*  m_activeArbitrators; ///< List of arbitrators that need arbitration.

    bool               m_activated;   ///< Has this clock already been activated this cycle?

    Clock(const Clock& clock) = delete; // No copying

    Clock(Kernel& kernel, unsigned long long frequency, unsigned long long period)
      : m_kernel(kernel),
        m_frequency(frequency), m_period(period), m_next(NULL), m_cycle(0),
        m_activeProcesses(NULL), m_activeStorages(NULL), m_activeArbitrators(NULL),
        m_activated(false)
    {}

public:
    Kernel& GetKernel() { return m_kernel; }

    /// Used for iterating through active clocks
    const Clock* GetNext() const { return m_next; }

    const Process* GetActiveProcesses() const { return m_activeProcesses; }
    const Storage* GetActiveStorages() const { return m_activeStorages; }
    const Arbitrator* GetActiveArbitrators() const { return m_activeArbitrators; }

    CycleNo GetNextTick() const { return m_cycle; }

    /// Returns the cycle counter for this clock
    CycleNo GetCycleNo() const;

    /// Returns the frequency of this clock
    unsigned long long GetFrequency() const { return m_frequency; }

    /**
     * @brief Register an update request for the specified storage at the end of the cycle.
     * @param storage The storage to update
     * @return the next storage that requires updating
     */
    Storage* ActivateStorage(Storage& storage);

    /**
     * @brief Register an update request for the specified arbitrator at the end of the cycle.
     * @param arbitrator The arbitrator to update
     * @return the next arbitrator that requires updating
     */
    Arbitrator* ActivateArbitrator(Arbitrator& arbitrator);

    /**
     * @brief Schedule the specified process on the run queue.
     * @param process The process to schedule
     */
    void ActivateProcess(Process& process);
};

/**
 * Enumeration for the run states of processes
 */
enum RunState
{
    STATE_IDLE,     ///< The component has no work.
    STATE_ACTIVE,   ///< The component has been activated.
    STATE_RUNNING,  ///< The component is running.
    STATE_DEADLOCK, ///< The component has work but cannot continue.
    STATE_ABORTED,  ///< The simulation has been aborted.
};


// Processes are member variables in components and represent the information
// about a single process in that component.
class Process
{
    friend class Kernel;
    friend class Clock;

    const std::string m_name;          ///< The name of this process
    const delegate    m_delegate;      ///< The callback for the execution of the process
    RunState          m_state;         ///< Last run state of this process
    unsigned int      m_activations;   ///< Reference count of activations of this process
    Process*          m_next;          ///< Next pointer in the list of processes that require updates
    Process**         m_pPrev;         ///< Prev pointer in the list of processes that require updates

#if !defined(NDEBUG) && !defined(DISABLE_TRACE_CHECKS)
    StorageTraceSet m_storages;         ///< Set of storage traces this process can have
    StorageTrace    m_currentStorages;  ///< Storage trace for this cycle
#endif
    uint64_t          m_stalls;        ///< Number of times the process stalled (failed).

    // Processes are non-copyable and non-assignable
    Process(const Process&);
    void operator=(const Process&);

public:
    const Process* GetNext()   const { return m_next;  }
    RunState       GetState()  const { return m_state; }
    Object*        GetObject() const { return m_delegate.GetObject(); }
    std::string    GetName()   const;

    void Deactivate();

    // The following functions are for verification of storage accesses.
    // They check that the process does not violate its contract for
    // accessing storages. The contract is set up when the system is created.
#if !defined(NDEBUG) && !defined(DISABLE_TRACE_CHECKS)
    void OnBeginCycle() {
        m_currentStorages = StorageTrace();
    }

    void OnEndCycle() const {
        // Check if the process accessed storages in a way that isn't allowed
        if (!m_storages.Contains(m_currentStorages))
        {
            std::cerr << std::endl
                      << "Invalid access by " << GetName() << ": " << m_currentStorages << std::endl;
#ifdef VERBOSE_TRACE_CHECKS
            std::cerr << "Allowed traces:" << std::endl
                      << m_storages;
#endif
#ifdef ABORT_ON_TRACE_FAILURE
            assert(false);
#endif
        };
    }

    void OnStorageAccess(const Storage& s) {
        m_currentStorages.Append(s);
    }

    void SetStorageTraces(const StorageTraceSet& sl) {
        m_storages = sl;
    }
#else
    void OnBeginCycle() {}
    void OnEndCycle() const {}
    void OnStorageAccess(const Storage& ) {}
    void SetStorageTraces(const StorageTraceSet& ) {}
#endif

    Process(Object& parent, const std::string& name, const delegate& delegate);
    ~Process();

    // for introspection
private:
    static std::set<const Process*> m_registry;

public:
    static const std::set<const Process*>& GetAllProcesses() { return m_registry; }
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
#define DebugSimWrite(msg, ...)  do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_SIM ) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugSimWrite_(("s " msg), ##__VA_ARGS__); } while(false)
#define DebugProgWrite(msg, ...) do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_PROG) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugSimWrite_(("- " msg), ##__VA_ARGS__); } while(false)
#define DebugFlowWrite(msg, ...) do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_FLOW) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugSimWrite_(("f " msg), ##__VA_ARGS__); } while(false)
#define DebugMemWrite(msg, ...)  do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_MEM ) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugSimWrite_(("m " msg), ##__VA_ARGS__); } while(false)
#define DebugIOWrite(msg, ...)   do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_IO  ) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugSimWrite_(("i " msg), ##__VA_ARGS__); } while(false)
#define DebugRegWrite(msg, ...)  do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_REG ) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugSimWrite_(("r " msg), ##__VA_ARGS__); } while(false)
#define DebugNetWrite(msg, ...)  do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_NET ) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugSimWrite_(("n " msg), ##__VA_ARGS__); } while(false)
#define DebugIONetWrite(msg, ...)  do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_IONET ) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugSimWrite_(("b " msg), ##__VA_ARGS__); } while(false)
#define DebugFPUWrite(msg, ...)  do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_FPU ) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugSimWrite_(("f " msg), ##__VA_ARGS__); } while(false)
#define DebugPipeWrite(msg, ...)  do { if ((GetKernel()->GetDebugMode() & Kernel::DEBUG_PIPE ) && GetKernel()->GetCyclePhase() == PHASE_COMMIT) DebugSimWrite_(("p " msg), ##__VA_ARGS__); } while(false)

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
        DEBUG_MEM      = 16, ///< Debug memory stores
        DEBUG_IO       = 32, ///< Debug I/O requests
        DEBUG_REG      = 64, ///< Debug register accesses
        DEBUG_NET      = 128, ///< Debug network message (delegation/link)
        DEBUG_IONET    = 256, ///< Debug I/O network message (interrupts/requests)
        DEBUG_FPU      = 512, ///< Debug FPU activity
        DEBUG_PIPE     = 1024, ///< Debug pipeline activity
    };
    static const int DEBUG_CPU_MASK = DEBUG_SIM | DEBUG_PROG | DEBUG_DEADLOCK | DEBUG_FLOW | DEBUG_MEM | DEBUG_IO | DEBUG_REG;

private:
    CycleNo             m_lastsuspend;  ///< Avoid suspending twice on the same cycle.
    CycleNo             m_cycle;        ///< Current cycle of the simulation.
    BreakPointManager&  m_bp_manager;   ///< The breakpoint checker for debugging.
    unsigned long long  m_master_freq;  ///< Master frequency
    Process*            m_process;      ///< The currently executing process.
    std::vector<Clock*> m_clocks;       ///< All clocks in the system.
    Clock*              m_activeClocks; ///< The clocks that have active components

    CyclePhase          m_phase;        ///< Current sub-cycle phase of the simulation.
    int                 m_debugMode;    ///< Bit mask of enabled debugging modes.
    bool                m_aborted;      ///< Should the run be aborted?
    bool                m_suspended;    ///< Should the run be suspended?

    bool UpdateStorages();

public:
    Kernel(BreakPointManager& breakpoints);
    ~Kernel();

    Kernel(const Kernel&) = delete; // No copy.
    Kernel& operator=(const Kernel&) = delete; // No assignment.

    void ActivateClock(Clock& clock);

    /**
     * @brief Creates a clock at the specified frequency (in MHz).
     */
    Simulator::Clock& CreateClock(unsigned long mhz);

    /**
     * @brief Returns the master frequency for the simulation, in MHz
     */
    unsigned long long GetMasterFrequency() const { return m_master_freq; }

    /**
     * @brief Get the currently executing process
     */
    inline Process* GetActiveProcess() const { return m_process; }

    /**
     * @brief Get the currently scheduled processes
     */
    inline const Clock* GetActiveClocks() const { return m_activeClocks; }

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
     * Stops the current simulation, in Step(). This is best called asynchronously,
     * from a signal handler. Step() will return STATE_ABORTED. The simulation cannot be resumed.
     */
    void Abort();

    /**
     * @brief Suspends the simulation
     * Stops the current simulation, in Step(). This is best called asynchronously,
     * from a signal handler. Step() will return STATE_ABORTED. Next call to Step()
     * will resume the simulation.
     */
    void Stop();

    /**
     * @brief Get all components.
     * Gets the list of all components in the simulation.
     * @return a constant reference to the list of all components.
     */
        //const ComponentList& GetComponents() const { return m_components; }

    inline BreakPointManager& GetBreakPointManager() const { return m_bp_manager; }
};

inline CycleNo Clock::GetCycleNo() const
{
    return m_kernel.GetCycleNo() / m_period;
}

inline Storage* Clock::ActivateStorage(Storage& storage)
{
    Storage* next = m_activeStorages;
    m_activeStorages = &storage;
    m_kernel.ActivateClock(*this);
    return next;
}

inline Arbitrator* Clock::ActivateArbitrator(Arbitrator& arbitrator)
{
    Arbitrator* next = m_activeArbitrators;
    m_activeArbitrators = &arbitrator;
    m_kernel.ActivateClock(*this);
    return next;
}

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
    std::string          m_fqn;         ///< Full object name.
    Clock&               m_clock;       ///< Clock that drives this object.
    Kernel&              m_kernel;      ///< The kernel that manages this object.
    std::vector<Object*> m_children;    ///< Children of this object

public:
    /**
     * Constructs a root object.
     * @param name the name of this object.
     */
    Object(const std::string& name, Clock& clock);

    /**
     * Constructs a child object, using the same kernel as the parent
     * @param parent the parent object.
     * @param name the name of this object.
     */
    Object(const std::string& name, Object& parent);

    /**
     * Constructs a child object.
     * @param parent the parent object.
     * @param kernel the clock that drives this object.
     * @param name the name of this object.
     */
    Object(const std::string& name, Object& parent, Clock& clock);

    Object(const Object&) = delete; // No copy.
    Object& operator=(const Object&) = delete; // No assignment.

    virtual ~Object();

    /// Check if the simulation is in the acquiring phase. @return true if the simulation is in the acquiring phase.
    bool IsAcquiring()  const { return m_kernel.GetCyclePhase() == PHASE_ACQUIRE; }
    /// Check if the simulation is in the check phase. @return true if the simulation is in the check phase.
    bool IsChecking()   const { return m_kernel.GetCyclePhase() == PHASE_CHECK;   }
    /// Check if the simulation is in the commit phase. @return true if the simulation is in the commit phase.
    bool IsCommitting() const { return m_kernel.GetCyclePhase() == PHASE_COMMIT;  }

    /// Get the kernel managing this object. @return the kernel managing this object.
    Kernel*            GetKernel() const { return &m_kernel; }
    /// Get the clock that controls this object
    Clock&             GetClock() const { return m_clock; }
    /// Get the parent object. @return the parent object.
    Object*            GetParent() const { return m_parent; }
    /// Get the number of children of the object. @return the number of children.
    unsigned int       GetNumChildren() const { return (unsigned int)m_children.size(); }
    /// Get a child of the object. @param i the index of the child. @return the child at index i.
    Object*            GetChild(int i)  const { return m_children[i]; }
    /// Get the object name. @return the object name.
    const std::string& GetName()   const { return m_name; }
    /// Get the current cycle counter of this object's clock
    CycleNo            GetCycleNo() const { return m_clock.GetCycleNo(); }

    /**
     * @brief Get the object's Fully Qualified Name.
     * The objectś Fully Qualified Name, or FQN, is the name of the object and
     * all its parent up to the root, with periods seperating the names.
     * @return the FQN of the object.
     */
    const std::string& GetFQN()    const { return m_fqn; }

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

private:
    Arbitrator* m_next;       ///< Next pointer in the list of arbitrators that require arbitration

protected:
    Clock&      m_clock;      ///< The clock that controls this arbitrator

private:
    bool        m_activated;  ///< Has the arbitrator already been activated this cycle?

protected:
    void RequestArbitration()
    {
        if (!m_activated) {
            m_next = m_clock.ActivateArbitrator(*this);
            m_activated = true;
        }
    }

public:
    ///< Callback for arbitration
    virtual void OnArbitrate() = 0;
    virtual std::string GetFQN() const = 0;

    const Arbitrator* GetNext() const { return m_next; }

    Arbitrator(Clock& clock)
        : m_next(NULL), m_clock(clock), m_activated(false)
    {}

    Arbitrator(const Arbitrator&) = delete; // No copy.
    Arbitrator& operator=(const Arbitrator&) = delete; // No assignment.

    virtual ~Arbitrator() {}
};

}
#endif
