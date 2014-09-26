// -*- c++ -*-
#ifndef SIM_ARBITRATOR_H
#define SIM_ARBITRATOR_H

#ifndef KERNEL_H
#error This file should be included in kernel.h
#endif

namespace Simulator
{
    /// Base class for all objects that arbitrate
    class Arbitrator
    {
        ///< Next pointer in the list of arbitrators that require arbitration
        Arbitrator* m_next;

        ///< The clock that controls this arbitrator
        Clock&      m_clock;

        ///< Has the arbitrator already been activated this cycle?
        bool        m_activated;

    protected:
        // Request arbitration: register this arbitrator to its clock, so
        // that it is woken up during the cycle arbitration phase (calling
        // OnArbitrate).
        // Arbitration is requested each time a request to a shared
        // arbitrator comes in (see derived classes).
        void RequestArbitration();

    protected:
        // Retrieve the associated clock.
        Clock& GetClock() { return m_clock; }

    public:
        // Callback for arbitration, called by Kernel
        virtual void OnArbitrate() = 0;

        // Name of this arbitrator.
        virtual const std::string& GetName() const = 0;

        // Iterate through the list of arbitrators waiting arbitration.
        Arbitrator* GetNext() { return m_next; }
        const Arbitrator* GetNext() const { return m_next; }

        // Deactivate this arbitrator (called by Kernel after OnArbitrate)
        void Deactivate() { m_activated = false; }

        // Constructors, destructors etc.
        Arbitrator(Clock& clock);
        virtual ~Arbitrator();

        Arbitrator(const Arbitrator&) = delete;
        Arbitrator& operator=(const Arbitrator&) = delete;
    };
}

#endif
