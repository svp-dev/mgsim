#include "sim/flag.h"
#include "sim/sampling.h"

namespace Simulator
{
    void Flag::Update()
    {
        if (m_new && !m_set) {
            Notify();
        } else if (m_set && !m_new) {
            Unnotify();
        }

        m_set     = m_new;
        m_updated = false;

        // Update statistics
        CycleNo cycle = GetKernel()->GetCycleNo();
        CycleNo elapsed = cycle - m_lastcycle;
        m_lastcycle = cycle;
        m_totalsize += (uint64_t)m_set * elapsed;
    }

    Flag::Flag(const std::string& name, Object& parent, Clock& clock, bool set)
        : Object(name, parent),
          Storage(name, parent, clock),
          SensitiveStorage(name, parent, clock),
          InitStateVariable(set, false),
          InitStateVariable(updated, false),
          InitStateVariable(new, set),
          InitSampleVariable(stalls, SVC_CUMULATIVE),
          InitSampleVariable(lastcycle, SVC_CUMULATIVE),
          InitSampleVariable(totalsize, SVC_CUMULATIVE)
    {
        if (set) {
            RegisterUpdate();
        }
    }

}
