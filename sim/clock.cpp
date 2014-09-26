#include "sim/kernel.h"

namespace Simulator
{
    Clock::Clock(Kernel&
#ifndef STATIC_KERNEL
          kernel
#endif
          , unsigned long frequency, unsigned long period)
        :
#ifndef STATIC_KERNEL
        m_kernel(kernel),
#endif
        m_frequency(frequency),
        m_period(period),
        m_next(NULL),
        m_cycle(0),
        m_activeProcesses(NULL),
        m_activeStorages(NULL),
        m_activeArbitrators(NULL),
        m_activated(false)
    {}

}
