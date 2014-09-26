#ifndef SIM_PROCESS_HPP
#define SIM_PROCESS_HPP

#ifndef KERNEL_H
#error This file should be included in kernel.h
#endif

#include <cassert>

namespace Simulator
{
    inline
    void Process::Deactivate()
    {
        // A process can be sensitive to multiple objects, so we only
        // remove it from the list if the count becomes zero.
        if (--m_activations == 0)
        {
            // Remove the handle node from the list
            *m_pPrev = m_next;
            if (m_next != NULL) {
                m_next->m_pPrev = m_pPrev;
            }
            m_state = STATE_IDLE;
        }
    }

    inline
    void Process::OnBeginCycle() {
#if !defined(NDEBUG) && !defined(DISABLE_TRACE_CHECKS)
        m_currentStorages.clear();
#endif
    }

    inline
    void Process::OnEndCycle() const {
#if !defined(NDEBUG) && !defined(DISABLE_TRACE_CHECKS)
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
#endif
    }

    inline
    void Process::OnStorageAccess(const Storage& s) {
#if !defined(NDEBUG) && !defined(DISABLE_TRACE_CHECKS)
        m_currentStorages.Append(s);
#else
        (void)s;
#endif
    }

    inline
    void Process::SetStorageTraces(const StorageTraceSet& sl) {
#if !defined(NDEBUG) && !defined(DISABLE_TRACE_CHECKS)
        m_storages = sl;
#else
        (void)sl;
#endif
    }

}

#endif
