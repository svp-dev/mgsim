#include "sim/kernel.h"

namespace Simulator
{
    Arbitrator::Arbitrator(Clock& clock)
        : m_next(0),
          m_clock(clock),
          m_activated(false)
    {}

    Arbitrator::~Arbitrator()
    {}
}
