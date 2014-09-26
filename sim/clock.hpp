#ifndef SIM_CLOCK_HPP
#define SIM_CLOCK_HPP

#ifndef KERNEL_H
#error This file should be included in kernel.h
#endif

namespace Simulator
{

#ifdef STATIC_KERNEL
    inline Kernel& Clock::GetKernel()
    {
        return Kernel::GetGlobalKernel();
    }
#endif

    inline
    CycleNo Clock::GetCycleNo() const
    {
        return GetKernel().GetCycleNo() / m_period;
    }

    inline
    Storage* Clock::ActivateStorage(Storage& storage)
    {
        Storage* next = m_activeStorages;
        m_activeStorages = &storage;
        GetKernel().ActivateClock(*this);
        return next;
    }

    inline
    Arbitrator* Clock::ActivateArbitrator(Arbitrator& arbitrator)
    {
        Arbitrator* next = m_activeArbitrators;
        m_activeArbitrators = &arbitrator;
        GetKernel().ActivateClock(*this);
        return next;
    }

    inline
    void Clock::ActivateProcess(Process& process)
    {
        if (++process.m_activations == 1)
        {
            // First time this process has been activated, queue it
            process.m_next  = m_activeProcesses;
            process.m_pPrev = &m_activeProcesses;
            if (process.m_next != NULL) {
                process.m_next->m_pPrev = &process.m_next;
            }
            m_activeProcesses = &process;
            process.m_state = STATE_ACTIVE;

            GetKernel().ActivateClock(*this);
        }
    }

}

#endif
