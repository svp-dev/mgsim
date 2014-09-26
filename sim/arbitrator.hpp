#ifndef SIM_ARBITRATOR_HPP
#define SIM_ARBITRATOR_HPP

#ifndef KERNEL_H
#error This file should be included in kernel.h
#endif

namespace Simulator
{
    inline
    void Arbitrator::RequestArbitration()
    {
        if (!m_activated) {
            m_next = m_clock.ActivateArbitrator(*this);
            m_activated = true;
        }
    }


}

#endif
