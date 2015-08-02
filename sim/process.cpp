#include "sim/sampling.h"
#include "sim/kernel.h"
#include <cctype>

namespace Simulator
{
    static std::string&& renameProcess(std::string cname,
                                       const std::string& pname)
    {
        assert(pname.size() > 0);
        size_t i = 0;
        if (pname.size() > 2 && pname[0] == 'p' && pname[1] == '_')
            i = 2;

        cname += ':';
        cname += (char)::tolower(pname[i]);
        for (++i; i < pname.size(); ++i)
        {
            if (::isupper(pname[i]))
            {
                cname += '-';
                cname += (char)::tolower(pname[i]);
            }
            else
                cname += pname[i];
        }
        return std::move(cname);
    }


    Process::Process(Object& parent, const std::string& name, delegate&& d)
        : m_name(renameProcess(parent.GetName(), name)),
          m_delegate(std::move(d)),
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
        auto& kernel = *parent.GetKernel();
        kernel.RegisterProcess(*this);
        kernel.GetVariableRegistry().RegisterVariable(m_stalls, m_name + ":stalls", SVC_CUMULATIVE);
        kernel.GetVariableRegistry().RegisterVariable(m_state, m_name + ":state", SVC_LEVEL);
    }


}
