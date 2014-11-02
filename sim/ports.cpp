#include "ports.h"
#include "sampling.h"
#include <sstream>
#include <algorithm>

using namespace std;

namespace Simulator
{

    ArbitratedPort::ArbitratedPort(Kernel& k, const string& name)
        : m_name(name),
          m_selected(NULL),
          m_busyCycles(0)
    {
        k.GetVariableRegistry().RegisterVariable(m_busyCycles,
                                                 GetName() + ":busyCycles",
                                                 SVC_CUMULATIVE);
    }

    SimpleArbitratedPort::SimpleArbitratedPort(Kernel& k, const string& name)
        : ArbitratedPort(k, name),
          m_processes(),
          m_requests(),
          m_lastrequest((CycleNo)-1)
    {
        k.GetVariableRegistry().RegisterVariable(m_lastrequest,
                                                 GetName() + ":lastrequest",
                                                 SVC_LEVEL);
    }

    void SimpleArbitratedPort::AddProcess(const Process& process)
    {
        assert(find(m_processes.begin(),
                    m_processes.end(),
                    &process) == m_processes.end());
        m_processes.push_back(&process);
    }

    void SimpleArbitratedPort::AddRequest(const Process& process, CycleNo c)
    {
        if (find(m_requests.begin(),
                 m_requests.end(),
                 &process) != m_requests.end())
        {
            // A process can request more than once in an arbitrator cycle
            // if the requester is in a higher frequency domain than the
            // arbitrator.
            // However the same process cannot request more than once
            // in the same cycle.
            assert(c != m_lastrequest);
            return;
        }
        m_requests.push_back(&process);
        m_lastrequest = c;
    }

    PriorityArbitratedPort::PriorityArbitratedPort(Kernel& k,
                                                   const string& name)
        : SimpleArbitratedPort(k, name)
    {}

    // Select a process that acquires the port.
    // The process with lowest index gets access.
    void PriorityArbitratedPort::Arbitrate()
    {
        SetSelectedProcess(NULL);
        if (m_requests.empty()) return;

        if (m_requests.size() == 1)
        {
            // Optimization for common case
            SetSelectedProcess(*m_requests.begin());
        }
        else
        {
            // Choose the request with the highest priority
            auto min = m_processes.size();
            for (auto i : m_requests)
            {
                // The position in the vector is its priority
                size_t priority = find(m_processes.begin(), m_processes.end(), i) - m_processes.begin();
                if (priority < min)
                {
                    min = priority;
                    SetSelectedProcess(i);
                }
            }
        }
        assert(GetSelectedProcess() != NULL);
        m_requests.clear();
        MarkBusy();
    }

    CyclicArbitratedPort::CyclicArbitratedPort(Kernel& k, const string& name)
        : SimpleArbitratedPort(k, name),
          m_lastSelected(0)
    {
        k.GetVariableRegistry().RegisterVariable(m_lastSelected,
                                                 GetName() + ":lastSelected",
                                                 SVC_LEVEL);
    }

    // Select a process that acquires the port.
    // Every process gets a turn.
    void CyclicArbitratedPort::Arbitrate()
    {
        assert(m_lastSelected < m_processes.size());

        SetSelectedProcess(NULL);
        if (m_requests.empty()) return;

        if (m_requests.size() == 1)
        {
            SetSelectedProcess(*m_requests.begin());
            m_lastSelected = find(m_processes.begin(),
                                  m_processes.end(),
                                  GetSelectedProcess()) - m_processes.begin();
        }
        else
        {
            auto sz = m_processes.size();
            auto min = sz;
            for (auto i : m_requests)
            {
                // pos = position of the requesting process in the
                // list of all processes.
                auto pos = find(m_processes.begin(),
                                m_processes.end(),
                                i) - m_processes.begin();

                // Compute the distance between the requesting process
                // and the last selected.
                auto d = (pos + sz - m_lastSelected) % sz;
                if (d != 0 && d < min)
                {
                    // Different process, shortest distance, select
                    // this one.
                    min = d;
                    SetSelectedProcess(i);
                }
            }
            // Remember which one we selected
            m_lastSelected = (m_lastSelected + min) % sz;
        }
        assert(GetSelectedProcess() != NULL);
        m_requests.clear();
        MarkBusy();
    }

    PriorityCyclicArbitratedPort::PriorityCyclicArbitratedPort(Kernel& k,
                                                               const string& name)
        : CyclicArbitratedPort(k, name),
          m_cyclicprocesses()
    {}

    void PriorityCyclicArbitratedPort::Arbitrate()
    {
        SetSelectedProcess(NULL);
        if (m_requests.empty()) return;

        if (m_requests.size() == 1)
        {
            // Optimization for common case
            SetSelectedProcess(*m_requests.begin());

            // If we just selected a cyclic process, remember it for
            // the next round of cyclic arbitration.
            size_t maybe_cyclic_last = find(m_cyclicprocesses.begin(),
                                            m_cyclicprocesses.end(),
                                            GetSelectedProcess()) - m_cyclicprocesses.begin();
            if (maybe_cyclic_last < m_cyclicprocesses.size())
                m_lastSelected = maybe_cyclic_last;
        }
        else
        {
            // First try to select using priorities.
            auto sz = m_processes.size();
            auto min = sz;
            for (auto i : m_requests)
            {
                // The position in the vector is its priority
                size_t priority = find(m_processes.begin(), m_processes.end(), i) - m_processes.begin();
                if (priority == sz)
                {
                    // this request is for a cyclic process, don't use
                    // it as a candidate.
                    continue;
                }
                if (priority < min)
                {
                    min = priority;
                    SetSelectedProcess(i);
                }
            }

            if (min == sz)
            {
                // no priority process found, the request is for a
                // cyclic process.
                min = sz = m_cyclicprocesses.size();
                for (auto i : m_requests)
                {
                    // pos = position of the requesting process in the
                    // list of all processes.
                    auto pos = find(m_cyclicprocesses.begin(),
                                    m_cyclicprocesses.end(),
                                    i) - m_cyclicprocesses.begin();

                    // Find the distance between the requesting process
                    // and the last selected
                    auto d = (pos + sz - m_lastSelected) % sz;
                    if (d != 0 && d < min)
                    {
                        // Different process, shortest distance,
                        // select this one.
                        min = d;
                        SetSelectedProcess(i);
                    }
                }
                // Remember which one we selected
                m_lastSelected = (m_lastSelected + min) % sz;
            }
        }
        assert(GetSelectedProcess() != NULL);
        m_requests.clear();
        MarkBusy();
    }

    ReadOnlyStructure::ReadOnlyStructure(const string& name, Object& parent, Clock& clock)
        : Object(name, parent),
          Arbitrator(clock),
          m_readPorts()
    { }

    const string& ReadOnlyStructure::GetName() const
    {
        return Object::GetName();
    }

    void ReadOnlyStructure::RegisterReadPort(ArbitratedReadPort& port)
    {
        assert(find(m_readPorts.begin(),
                    m_readPorts.end(),
                    &port) == m_readPorts.end());
        m_readPorts.push_back(&port);
    }

    // Arbitrate between all incoming requests
    void ReadOnlyStructure::ArbitrateReadPorts()
    {
        for (auto p : m_readPorts)
            p->Arbitrate();
    }

    void ReadOnlyStructure::OnArbitrate()
    {
        ArbitrateReadPorts();
    }

    ArbitratedReadPort::ArbitratedReadPort(ReadOnlyStructure& structure,
                                           const string& name)
        : PriorityArbitratedPort(*structure.GetKernel(), name),
          m_structure(structure)
    {
        m_structure.RegisterReadPort(*this);
    }

    DedicatedPort::DedicatedPort()
        : m_process(0)
    {}

    DedicatedReadPort::DedicatedReadPort(ReadOnlyStructure& structure)
        : m_structure(structure)
    {}

}
