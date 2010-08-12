#ifndef _TOPOLOGY_S_H
#define _TOPOLOGY_S_H

#include "processortok.h"
#include "cachel2tok.h"
#include "directoryrttok.h"
#include "directorytok.h"
#include "ddrmemorysys.h"
#include "../../VirtualMemory.h"

namespace MemSim
{

class TopologyS : public Simulator::VirtualMemory
{
    // clocks
    sc_clock* m_pclkroot;
    sc_clock* m_pclkmem;

    std::vector<ProcessorTOK*>   m_ppProcessors;
    std::vector<CacheL2TOK*>     m_ppCacheL2;
    std::vector<DirectoryTOK*>   m_ppDirectoryL0;
    DirectoryRTTOK*              m_pDirectoryRoot;
    DDRMemorySys*                m_pMem;

public:
    TopologyS();
    ~TopologyS();
};

}

#endif

