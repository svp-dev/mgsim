#ifndef SIM_FLAG_HPP
#define SIM_FLAG_HPP

#include "sim/flag.h"

namespace Simulator
{
    inline
    bool Flag::IsSet() const
    {
        return m_set;
    }

    inline
    bool Flag::Set() {
        MarkUpdate();
        if (!m_updated) {
            COMMIT {
                m_new     = true;
                m_updated = true;
                RegisterUpdate();
            }
            return true;
        }
        // Accumulate for statistics. We don't want
        // to register multiple stalls so only test during acquire.
        if (IsAcquiring())
        {
            ++m_stalls;
        }
        return false;
    }

    inline
    bool Flag::Clear() {
        MarkUpdate();
        if (!m_updated) {
            COMMIT {
                m_new     = false;
                m_updated = true;
                RegisterUpdate();
            }
            return true;
        }
        // Accumulate for statistics. We don't want
        // to register multiple stalls so only test during acquire.
        if (IsAcquiring())
        {
            ++m_stalls;
        }
        return false;
    }


}

#endif
