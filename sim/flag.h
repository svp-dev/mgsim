// -*- c++ -*-
#ifndef SIM_FLAG_H
#define SIM_FLAG_H

#include "sim/storage.h"
#include "sim/sampling.h"

namespace Simulator
{
    /// A single-bit storage element
    class Flag : public SensitiveStorage
    {
    protected:

        DefineStateVariable(bool, set);
        bool m_updated;
        bool m_new;

        // Statistics
        DefineSampleVariable(uint64_t, stalls);      ///< Number of stalls so far
        DefineSampleVariable(CycleNo, lastcycle);    ///< Cycle no of last event
        DefineSampleVariable(uint64_t, totalsize); ///< Cumulated current size * cycle no

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
        static constexpr const char* NAME_PREFIX = "f_";
    };

}

#include "sim/flag.hpp"

#endif
