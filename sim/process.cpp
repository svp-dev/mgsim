#include "sim/sampling.h"
#include "sim/kernel.h"

namespace Simulator
{

    Process::Process(Object& parent, const std::string& name, const delegate& d)
        : m_name(parent.GetName() + ":" + name),
          m_delegate(d),
          m_state(STATE_IDLE),
          m_activations(0),
          m_next(0),
          m_pPrev(0),
          m_stalls(0)
#if !defined(NDEBUG) && !defined(DISABLE_TRACE_CHECKS)
        , m_storages(),
          m_currentStorages()
#endif
    {
        parent.GetKernel()->RegisterProcess(*this);
        RegisterSampleVariable(m_stalls, m_name + ":stalls", SVC_CUMULATIVE);
        RegisterSampleVariable(m_state, m_name + ":state", SVC_LEVEL);
    }


}
