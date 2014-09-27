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
          m_set(false),
          m_updated(false),
          m_new(set),
          m_stalls(0),
          m_lastcycle(0),
          m_totalsize(0)
    {
        RegisterSampleVariableInObject(m_totalsize, SVC_CUMULATIVE);
        RegisterSampleVariableInObject(m_set, SVC_LEVEL);
        RegisterSampleVariableInObject(m_stalls, SVC_CUMULATIVE);
        if (set) {
            RegisterUpdate();
        }
    }

}
