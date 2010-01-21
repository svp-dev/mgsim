#ifndef _TOPOLOGY_S_H
#define _TOPOLOGY_S_H

#include "predef.h"
#include "busst.h"
#include "processortok.h"
#include "cachel2tokim.h"
#include "directoryrttok.h"
#include "directorytok.h"
#include "network.h"
#include "ddrmemorysys.h"
#include "busswitch.h"
#include "../../VirtualMemory.h"

namespace MemSim
{

class TopologyS : public Simulator::VirtualMemory
{
    // clocks
    sc_clock* m_pclk;
    sc_clock* m_pclkroot;
    sc_clock* m_pclkmem;

    std::vector<ProcessorTOK*>   m_ppProcessors;
    std::vector<BusST*>          m_ppBus;
    std::vector<CacheL2TOKIM*>   m_ppCacheL2;
    std::vector<Network*>        m_ppNetL0;
    std::vector<DirectoryRTTOK*> m_ppDirectoryRoot;
    std::vector<DirectoryTOK*>   m_ppDirectoryL0;
    std::vector<DDRMemorySys*>   m_ppMem;
    Network*                     m_pNet;
    BusSwitch*                   m_pBSMem;

public:
    TopologyS();
    ~TopologyS();
};

}

#endif

