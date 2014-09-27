// -*- c++ -*-
#ifndef SIM_FLAG_H
#define SIM_FLAG_H

#include "sim/storage.h"

namespace Simulator
{
    /// A single-bit storage element
    class Flag : public SensitiveStorage
    {
    protected:
        bool m_set;
        bool m_updated;
        bool m_new;

        // Statistics
        uint64_t      m_stalls;         ///< Number of stalls so far
        CycleNo       m_lastcycle;      ///< Cycle no of last event
        uint64_t      m_totalsize;      ///< Cumulated current size * cycle no

        // Update: commit this flag's changes between master cycles.
        void Update() override;

    public:
        // IsSet: return true iff the flag is set.
        bool IsSet() const;

        // Set: set the flag to true.
        bool Set();

        // Clear: reset the flag to false.
        bool Clear();

        Flag(const std::string& name, Object& parent, Clock& clock, bool set);
        Flag(const Flag&) = delete;
        Flag& operator=(const Flag&) = delete;
    };

}

#include "sim/flag.hpp"

#endif
