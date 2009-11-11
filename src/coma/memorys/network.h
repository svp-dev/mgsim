#ifndef _NETWORK_H
#define _NETWORK_H

#include "predef.h"

#include "network_port.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class Network : public sc_module, public SimObj
{

public:
    // ports
    sc_in_clk port_clk;
    Network_port port_net;

public:
    SC_HAS_PROCESS(Network);
    Network(sc_module_name nm) : sc_module(nm)
    {
        port_net.SetClockPort(&port_clk);
    }

    void ConnectNetwork(){port_net.ConnectNodes();};

    virtual void InitializeLog(const char* logName, LOG_LOCATION ll, VERBOSE_LEVEL verbose = VERBOSE_DEFAULT)
    {
        SimObj::InitializeLog(logName, ll, verbose);

        port_net.InitializeLog(logName, ll, verbose);
    }
};

//////////////////////////////
//} memory simulator namespace
}

#endif
