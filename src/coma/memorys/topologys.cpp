#include "topologys.h"
#include "../../config.h"
using namespace MemSim;

static const int MAX_NAME_SIZE = 0x30;

extern Config* g_Config;

std::vector<MemSim::ProcessorTOK*> g_Links;

TopologyS::TopologyS()
{
    // parsed config
    LinkConfig &cf = LinkMGS::s_oLinkConfig;
    const Config& config = *g_Config;

    g_nCacheLineSize       = cf.m_nLineSize;
    g_pMemoryDataContainer = this;

    assert(cf.m_nProcessorsPerCache > 0);
    assert(cf.m_nCachesPerDirectory > 0);
    
    // temporary names
    char tempname[MAX_NAME_SIZE];

    m_pclkroot = new sc_clock("clkroot", cf.m_nCycleTimeCore, SC_PS);
    m_pclkmem  = new sc_clock("clkmem",  cf.m_nCycleTimeMemory, SC_PS);

    // Create L2 caches
    m_ppCacheL2.resize( std::max(2U, (cf.m_nProcs + cf.m_nProcessorsPerCache - 1) / cf.m_nProcessorsPerCache) );
    for (unsigned int i = 0; i < m_ppCacheL2.size(); i++)
    {
        sprintf(tempname, "cache%d", i);
        m_ppCacheL2[i] = new CacheL2TOK(tempname, *m_pclkroot, i,
            cf.m_nCacheSet,
            cf.m_nCacheAssociativity,
            cf.m_nLineSize,
            cf.m_nInject,
            cf.m_nCacheAccessTime );
    }

    // set total token number
    CacheState::SetTotalTokenNum( m_ppCacheL2.size() );

    // Create processor links
    g_Links.resize(cf.m_nProcs);
    m_ppProcessors.resize(cf.m_nProcs);
    for (unsigned int i = 0; i < cf.m_nProcs; i++)
    {
        g_Links[i] = m_ppProcessors[i] = new ProcessorTOK(*m_ppCacheL2[i / cf.m_nProcessorsPerCache]);
    }

    // create level0 network
    m_ppDirectoryL0.resize((m_ppCacheL2.size() + cf.m_nCachesPerDirectory - 1) / cf.m_nCachesPerDirectory);
    for (unsigned int i = 0; i < m_ppDirectoryL0.size(); i++)
    {
        unsigned int first = i * cf.m_nCachesPerDirectory;
        unsigned int last  = std::min(first + cf.m_nCachesPerDirectory, (unsigned)m_ppCacheL2.size()) - 1;
        
        std::stringstream name;
        name << "dir" << i;
        m_ppDirectoryL0[i] = new DirectoryTOK(name.str().c_str(), *m_pclkroot,
           first, last,
           cf.m_nCacheSet,
           cf.m_nCacheAssociativity * cf.m_nCachesPerDirectory,
           cf.m_nLineSize,
           cf.m_nCacheAccessTime );

        // Connect this ring
        static_cast<NetworkBelow_Node*>(m_ppDirectoryL0[i])->SetNext(m_ppCacheL2[first]);
        for (unsigned int j = first; j < last; j++)
        {
            m_ppCacheL2[j]->SetNext(m_ppCacheL2[j+1]);
        }
        m_ppCacheL2[last]->SetNext(static_cast<NetworkBelow_Node*>(m_ppDirectoryL0[i]));

    }
    
    // Create root directory
    m_pMem = new DDRMemorySys("ddr", *m_pclkmem, *this, config);
    m_pDirectoryRoot = new DirectoryRTTOK("dir-root", *m_pclkroot, *m_pMem,
        cf.m_nCacheSet,
        cf.m_nCacheAssociativity * m_ppCacheL2.size(),
        cf.m_nLineSize );

    // Connect top-level ring
    m_pDirectoryRoot->SetNext(static_cast<NetworkAbove_Node*>(m_ppDirectoryL0[0]));
    for (unsigned int i = 0; i < m_ppDirectoryL0.size() - 1; i++)
    {
        static_cast<NetworkAbove_Node*>(m_ppDirectoryL0[i])->SetNext(static_cast<NetworkAbove_Node*>(m_ppDirectoryL0[i+1]));
    }
    static_cast<NetworkAbove_Node*>(m_ppDirectoryL0.back())->SetNext(m_pDirectoryRoot);
}


TopologyS::~TopologyS()
{
    for (size_t i = 0; i < m_ppProcessors.size(); ++i)
        delete m_ppProcessors[i];

    for (size_t i = 0; i < m_ppCacheL2.size(); ++i)
        delete m_ppCacheL2[i];

    delete m_pDirectoryRoot;
    delete m_pMem;
    
    delete m_pclkroot;
    delete m_pclkmem;
}
