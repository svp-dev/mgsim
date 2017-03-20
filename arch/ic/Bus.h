// -*- c++ -*-
#ifndef IC_BUS_H
#define IC_BUS_H

#include "arch/Interconnect.h"
#include "arch/ic/SharedMedium.h"
#include "arch/ic/EndPointRegistry.h"
#include "arch/ic/SourceBuffering.h"
#include "arch/ic/DestinationBuffering.h"
#include "arch/ic/WireNet.h"

namespace Simulator
{

    namespace IC {

        template<typename Payload, typename Arbitrator = CyclicArbitratedPort>
        using UnbufferedBus = EndPointRegistry<SharedMedium<WireNet<Payload>, Arbitrator>>;

        template<typename Payload, typename Arbitrator = CyclicArbitratedPort>
        using BufferedBus = EndPointRegistry<DestinationBuffering<SourceBuffering<SharedMedium<WireNet<Payload>, Arbitrator>>>>;

    }
}

#endif
