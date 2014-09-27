#ifndef SIM_STORAGE_HPP
#define SIM_STORAGE_HPP

#include "sim/storage.h"

namespace Simulator
{
    inline
    void Storage::CheckClocks() const
    {
#ifndef NDEBUG
        assert(GetKernel()->GetActiveClock() == &GetClock());
#endif
    }

    inline
    void Storage::MarkUpdate()
    {
#if !defined(NDEBUG) && !defined(DISABLE_TRACE_CHECKS)
        if (IsAcquiring()) {
            auto p = GetKernel()->GetActiveProcess();
            p->OnStorageAccess(*this);
        }
#endif
    }

    inline
    void Storage::RegisterUpdate()
    {
        if (!m_activated) {
            m_next = GetClock().ActivateStorage(*this);
            m_activated = true;
        }
    }
}


#endif
