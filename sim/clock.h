// -*- c++ -*-
#ifndef SIM_CLOCK_H
#define SIM_CLOCK_H

#ifndef KERNEL_H
#error This file should be included in kernel.h
#endif

#include <cstdint>

namespace Simulator
{
    /// Cycle Number
    //
    // We want a fairly large type here, for it will be used to track
    // progress of the simulation. For example with a 32-bit CycleNo
    // and and a master kernel frequency of 4GHz, we could only
    // simulate about 1 second of virtual time. With 64 bits, we can
    // simulate about 146 years of virtual time.
    typedef uint64_t CycleNo;

    /// Value representing forever (infinite cycles)
    static const CycleNo INFINITE_CYCLES = (CycleNo)-1;

    // Forward declarations
    class Process;
    class Storage;
    class Arbitrator;
    class Kernel;

    /*
     * A clock class to place processes in a frequency domain.
     * This is only an interface to pass around. Clocks are created
     * by the kernel.
     */
    class Clock
    {
        friend class Kernel;

    public:
        typedef unsigned long Frequency;
        typedef unsigned long Period;
    private:

#ifndef STATIC_KERNEL
        Kernel&       m_kernel;      ///< The kernel that controls this clock and all components based off it
#endif
        Frequency     m_frequency;   ///< Frequency of this clock, in MHz
        Period        m_period;      ///< No. master-cycles per tick of this clock.
        Clock*        m_next;        ///< Next clock to run
        CycleNo       m_cycle;       ///< Next cycle this clock needs to run

        Process*      m_activeProcesses;   ///< List of processes that need to be run.
        Storage*      m_activeStorages;    ///< List of storages that need to be updated.
        Arbitrator*   m_activeArbitrators; ///< List of arbitrators that need arbitration.

        bool          m_activated;   ///< Has this clock already been activated this cycle?

        Clock(const Clock& clock) = delete; // No copying

        // Constructor, called by Kernel::GetClock.
        //
        // the 2nd argument (frequency) is the frequency of this clock.
        // This can be seen as unit-less as it is only used in the Kernel
        // to compute the relative rate of different clocks. In practice,
        // some unit is assumed for concrete architecture models, eg. MHz.
        // The 3rd argument (period) is the number of master ticks per
        // tick of this clock.
        Clock(Kernel&, Frequency frequency, Period period);

    public:
#ifdef STATIC_KERNEL
	static Kernel& GetKernel();
#else
	Kernel& GetKernel() { return m_kernel; }
	const Kernel& GetKernel() const { return m_kernel; }
#endif

        /// Used for iterating through active clocks
        const Clock* GetNext() const { return m_next; }

        const Process* GetActiveProcesses() const { return m_activeProcesses; }
        const Storage* GetActiveStorages() const { return m_activeStorages; }
        const Arbitrator* GetActiveArbitrators() const { return m_activeArbitrators; }

        CycleNo GetNextTick() const { return m_cycle; }

        /// Returns the cycle counter for this clock
        CycleNo GetCycleNo() const;

        /// Returns the frequency of this clock
        Frequency GetFrequency() const { return m_frequency; }

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


}

#endif
