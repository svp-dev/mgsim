// -*- c++ -*-
#ifndef SIM_PROCESS_H
#define SIM_PROCESS_H

#ifndef KERNEL_H
#error This file should be included in kernel.h
#endif

#include <string>
#include <type_traits>
#include "sim/storagetrace.h"

namespace Simulator
{
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

    /**
     * Enumeration for the result type of a cycle handler.
     *  Values:
     *  - FAILED: The operation could not make progress, must be retried.
     *    For example a producer encountered a full FIFO.
     *  - DELAYED: The operation did make progress but must be retried.
     *  - SUCCESS: The operation made progress and need not be retried.
     */
    enum Result
    {
        FAILED,
        DELAYED,
        SUCCESS
    };

    // The most common delegate form in MGSim is cycle handlers, of
    // type Result (*)(). So alias this to "delegate" for convenience.
    typedef delegate_gen<Result> delegate;

    // Forward declaration
    class Object;

    // Processes are member variables in components and represent the information
    // about a single process in that component.
    class Process
    {
        friend class Kernel;
        friend class Clock;

        const std::string m_name;          ///< The fully qualified name of this process
        const delegate    m_delegate;      ///< The callback for the execution of the process
        RunState          m_state;         ///< Last run state of this process
        unsigned int      m_activations;   ///< Reference count of activations of this process
        Process*          m_next;          ///< Next pointer in the list of processes that require updates
        Process**         m_pPrev;         ///< Prev pointer in the list of processes that require updates

        uint64_t          m_stalls;        ///< Number of times the process stalled (failed).

#if !defined(NDEBUG) && !defined(DISABLE_TRACE_CHECKS)
        StorageTraceSet   m_storages;         ///< Set of storage traces this process can have
        StorageTrace      m_currentStorages;  ///< Storage trace for this cycle
#endif

        // Processes are non-copyable and non-assignable
        Process(const Process&) = delete;
        Process& operator=(const Process&) = delete;

    public:
        // Constructor
        Process(Object& parent, const std::string& name, const delegate& delegate);

        // Accessors
        const Process* GetNext() const { return m_next;  }
        RunState GetState() const { return m_state; }
        const std::string& GetName() const { return m_name; }

        // Deactivate a process from receiving invocations from its
        // clock. Used by storages (cf storage.h) when they become empty.
        void Deactivate();

        // The following functions are for verification of storage accesses.
        // They check that the process does not violate its contract for
        // accessing storages. The contract is set up when the system is created.
        void OnBeginCycle();
        void OnEndCycle() const;
        void OnStorageAccess(const Storage&);
        void SetStorageTraces(const StorageTraceSet& );
    };

#define INIT_PROCESS(Member, Name, DelegateFunc) \
    Member(GetName() + ":" #Member, \
           delegate::create<std::remove_reference<decltype(*this)>::type, \
           &std::remove_reference<decltype(*this)>::type::DelegateFunc>(*this))

}


#endif
