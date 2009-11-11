#ifndef _RING_NODE_H
#define _RING_NODE_H

#include "predef.h"
#include "simcontrol.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class RingNode: virtual public SimObj
{
public:
    // ports

    // creator
    RingNode()
    {
        SetRingNode(this);
    }

};

//////////////////////////////
//} memory simulator namespace
}

#endif

