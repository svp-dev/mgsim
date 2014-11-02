// -*- c++ -*-
#ifndef KERNEL_H
#define KERNEL_H

#include <vector>
#include <map>
#include <set>
#include <cassert>

// Dependencies of Kernel.
#include "sim/types.h"
#include "sim/except.h"
#include "sim/delegate.h"
#include "sim/storagetrace.h"
#include "sim/sampling.h"

// Other classes that users of Kernel expect to see defined too.
#include "sim/clock.h"
#include "sim/object.h"
#include "sim/process.h"
#include "sim/arbitrator.h"

class Config;

namespace Simulator
{
    /**
     * Enumeration for the phases inside a cycle
     */
    enum CyclePhase {
        PHASE_ACQUIRE,  ///< Acquire phase, all components indicate their wishes.
        PHASE_CHECK,    ///< Check phase, all components verify that they can continue.
        PHASE_COMMIT    ///< Commit phase, all components commit their cycle.
    };

    /**
     * @brief Component-manager class
     * The kernel class is the manager for all components in the simulation. It advances
     * time, calls the cycle callbacks on all components, initiates arbitration and more.
     */
    class Kernel
    {

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
            DEBUG_MEMNET   = 2048, ///< Debug memory network activity
        };
        static const int DEBUG_CPU_MASK = DEBUG_SIM | DEBUG_PROG | DEBUG_DEADLOCK | DEBUG_FLOW | DEBUG_MEM | DEBUG_IO | DEBUG_REG;

    private:
        CycleNo             m_lastsuspend;  ///< Avoid suspending twice on the same cycle.
        CycleNo             m_cycle;        ///< Current cycle of the simulation.
        Clock::Frequency    m_master_freq;  ///< Master frequency
        Clock*              m_clock;        ///< The currently active clock.
        Process*            m_process;      ///< The currently executing process.
        std::vector<Clock*> m_clocks;       ///< All clocks in the system.
        Clock*              m_activeClocks; ///< The clocks that have active components

        CyclePhase          m_phase;        ///< Current sub-cycle phase of the simulation.
        int                 m_debugMode;    ///< Bit mask of enabled debugging modes.
        bool                m_aborted;      ///< Should the run be aborted?
        bool                m_suspended;    ///< Should the run be suspended?

        Config*             m_config;       ///< Attached configuration object.
        VariableRegistry    m_var_registry; ///< Attached variable registry.
        std::set<Process*>  m_proc_registry; ///< Set of all processes instantiated.

        bool UpdateStorages();

#ifdef STATIC_KERNEL
        static Kernel* g_kernel;
    public:
        static inline void InitGlobalKernel() { assert(g_kernel == NULL); g_kernel = new Kernel(); }
        static inline Kernel& GetGlobalKernel() { return *g_kernel; }
    private:
#endif

    public:
        Kernel();
        ~Kernel();

        Kernel(const Kernel&) = delete; // No copy.
        Kernel& operator=(const Kernel&) = delete; // No assignment.

        void AttachConfig(Config& cfg) { m_config = &cfg; }
        Config* GetConfig() const { return m_config; }

        VariableRegistry& GetVariableRegistry() { return m_var_registry; }
        const VariableRegistry& GetVariableRegistry() const { return m_var_registry; }

        /**
         * @brief Register a process for introspection.
         */
        void RegisterProcess(Process&);

        /**
         * @brief Inspect all registered processes.
         */
        const std::set<Process*>& GetAllProcesses() const { return m_proc_registry; };

        /**
         * @brief Activate a clock to run in the next cycle.
         */
        void ActivateClock(Clock& clock);

        /**
         * @brief Creates a clock at the specified frequency (in MHz).
         */
        Clock& CreateClock(Clock::Frequency mhz);

        /**
         * @brief Returns the master frequency for the simulation, in MHz
         */
        Clock::Frequency GetMasterFrequency() const { return m_master_freq; }

        /**
         * @brief Get the currently active clock
         */
        inline Clock* GetActiveClock() const { return m_clock; }

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

    };

}

#include "sim/clock.hpp"
#include "sim/object.hpp"
#include "sim/process.hpp"
#include "sim/arbitrator.hpp"

#endif
