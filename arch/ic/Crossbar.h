// -*- c++ -*-
#ifndef IC_CROSSBAR_H
#define IC_CROSSBAR_H

#include "arch/Interconnect.h"
#include "arch/ic/WireNet.h"
#include "arch/ic/EndPointArbiter.h"
#include "arch/ic/EndPointRegistry.h"
#include "arch/ic/SourceBuffering.h"
#include "sim/ports.h"
#include "sim/kernel.h"

namespace Simulator
{

    namespace IC {

        template<typename Payload, typename Arbitrator = CyclicArbitratedPort>
        using UnbufferedCrossbar = EndPointRegistry<EndPointArbiter<WireNet<Payload>, Arbitrator>>;
        template<typename Payload, typename Arbitrator = CyclicArbitratedPort>
        using BufferedCrossbar = EndPointRegistry<DestinationBuffering<SourceBuffering<EndPointArbiter<WireNet<Payload>, Arbitrator>>>>;

    }
}

#endif
