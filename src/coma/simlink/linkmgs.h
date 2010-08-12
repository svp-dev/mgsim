#ifndef LINKMGS_H
#define LINKMGS_H
#include "../../VirtualMemory.h"
#include <cstdlib>
#include <stdint.h>
#include <vector>
#include <iostream>

using namespace Simulator;

class LinkConfig
{
public:
    unsigned int m_nLineSize;
    unsigned int m_nProcs;
    unsigned int m_nProcessorsPerCache;
    unsigned int m_nCachesPerDirectory;
    unsigned int m_nNumRootDirs;
    unsigned int m_nMemoryChannels;
    unsigned int m_nCacheSet;
    unsigned int m_nCacheAssociativity;
    unsigned int m_nCacheAccessTime;
    unsigned int m_nMemoryAccessTime;
    unsigned int m_nInject;

    unsigned int m_nCycleTimeCore;
    unsigned int m_nCycleTimeMemory;

    LinkConfig()
    {
        m_nMemoryAccessTime = 100;
    }
    
    void dumpConfiguration(std::ostream& os) const
    {
#define PC(N) "# " #N << '\t' << m_ ## N << std::endl 
        os << "### begin COMA configuration" << std::endl
           << PC(nProcs)
           << PC(nProcessorsPerCache)
           << PC(nCachesPerDirectory)
           << PC(nNumRootDirs)
           << PC(nMemoryChannels)
           << PC(nCycleTimeCore)
           << PC(nCycleTimeMemory)
           << PC(nCacheAccessTime)
           << PC(nMemoryAccessTime)
           << PC(nCacheSet)
           << PC(nCacheAssociativity)
           << PC(nLineSize)
           << PC(nInject)
           << "### end COMA configuration" << std::endl;
#undef PC
    };
};

class LinkMGS
{
public:
    static LinkConfig s_oLinkConfig;
};

extern Simulator::VirtualMemory* g_pMemoryDataContainer;

#endif

