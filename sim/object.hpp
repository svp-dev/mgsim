#ifndef SIM_OBJECT_HPP
#define SIM_OBJECT_HPP

#ifndef KERNEL_H
#error This file should be included in kernel.h
#endif

namespace Simulator
{

    inline
    bool Object::IsAcquiring() const
    {
        return GetKernel()->GetCyclePhase() == PHASE_ACQUIRE;
    }

    inline
    bool Object::IsChecking() const
    {
        return GetKernel()->GetCyclePhase() == PHASE_CHECK;
    }

    inline
    bool Object::IsCommitting() const
    {
        return GetKernel()->GetCyclePhase() == PHASE_COMMIT;
    }

#ifdef STATIC_KERNEL
    inline
    Kernel* Object::GetKernel() { return &Kernel::GetGlobalKernel(); }
#else
    inline
    Kernel* Object::GetKernel() const { return &m_kernel; }
#endif


}

#endif
