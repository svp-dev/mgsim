#include "topologys.h"
#include "ddrxml.h"
using namespace MemSim;

#define MAX_NAME_SIZE   0x30

TopologyS::TopologyS()
{
    // check something here
    //assert((1<<g_nCacheLineWidth) == CACHE_BIT_MASK_WIDTH*CACHE_REQUEST_ALIGNMENT);

    // parsed config
    LinkConfig &cf = LinkMGS::s_oLinkConfig;

    // make sure the number of caches is checked
    assert(cf.m_nCache <= cf.m_nProcLink);

    clog << "Simulation on " << cf.m_nProcMGS << "(" << cf.m_nProcLink << ")" << " Processors and " << cf.m_nCache << " L2 Caches." << endl;

    // set total token number
    CacheState::SetTotalTokenNum(cf.m_nCache);

    bool bddrdefault = true;

    // parse DDR Memory interfaces configurations
    map<int, ddr_interface*>* pddrinterfaces = NULL;
    if (cf.m_sDDRXML != NULL)
        pddrinterfaces = loadxmlddrinterfaces(cf.m_sDDRXML);
    map<int, vector<int> >* pddrconfigurations = NULL;
    vector<int>* pddrconfig=NULL;
    unsigned int ddrinterfacecount=0;

    if (pddrinterfaces != NULL)
    {
        pddrconfigurations = loadxmlddrconfigurations(pddrinterfaces, cf.m_sDDRXML);
        if (pddrconfigurations != NULL)
        {
            assert(!pddrinterfaces->empty());
            assert(!pddrconfigurations->empty());

            map<int, vector<int> >::iterator iter = pddrconfigurations->find(cf.m_nDDRConfigID);

            if (iter != pddrconfigurations->end())
            {
                pddrconfig = &(*iter).second;
                ddrinterfacecount = pddrconfig->size();

                bddrdefault = false;
            }
        }
    }

    // temporary names
    char tempname[MAX_NAME_SIZE];

    // processors are essential links 
    // the are defined first
    // SimpleMemoryDataContainer memDataSim;
    // m_pmemDataSim = new SimpleMemoryDataContainer(cf.m_nMemorySize);
    m_pmemDataSim = new VMemoryDataContainer();
    g_pMemoryDataContainer = m_pmemDataSim;

    // create processors
    m_ppProcessors = (PROCESSOR_DEF**)malloc(sizeof(PROCESSOR_DEF*)*cf.m_nProcLink);
    for (unsigned int i=0;i<cf.m_nProcLink;i++)
    {
        sprintf(tempname, "p%d", i);
        m_ppProcessors[i] = new PROCESSOR_DEF(tempname, i, m_pmemDataSim);
    }

    /////////////////////////////////////////////////////
    // create the links
    g_pLinks = (LinkMGS**)malloc(cf.m_nProcLink*sizeof(LinkMGS*));
    for (unsigned int i=0;i<cf.m_nProcLink;i++)
    {
        g_pLinks[i] = m_ppProcessors[i];
    }

    //////////////////////////////////////////////////////////////////////////
    // topology new full
    m_pclk = new sc_clock("clk", cf.m_nCycleTimeCore, SC_PS);
    m_pclkmem = new sc_clock("clkmem", cf.m_nCycleTimeMemory, SC_PS);

    // create buses and caches
    m_ppBus = (BusST**)malloc(sizeof(BusST*)*cf.m_nCache);
    m_ppCacheL2 = (CACHEL2IM_DEF**)malloc(sizeof(CACHEL2IM_DEF*)*cf.m_nCache);

    CacheState::INJECTION_POLICY injpol = (CacheState::INJECTION_POLICY)cf.m_nInject;

    for (unsigned int i=0;i<cf.m_nCache;i++)
    {
        sprintf(tempname, "bus%d", i);
        m_ppBus[i] = new BusST(tempname);

        sprintf(tempname, "cache%d", i);


        m_ppCacheL2[i] = new CACHEL2IM_DEF(tempname, cf.m_nCacheSet, cf.m_nCacheAssociativity, cf.m_nLineSize, injpol, CACHEL2IM_DEF::RP_LRU, (CACHEL2IM_DEF::WP_WRITE_THROUGH|CACHEL2IM_DEF::WP_WRITE_AROUND), 0, cf.m_nMemorySize-1, cf.m_nCacheAccessTime,
#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
                        2,
#endif
        cf.m_bEager?CacheState::BBI_EAGER:CacheState::BBI_LAZY);
    }

    // create directory later if any specified

    m_pNet = new Network("net");

    //m_pMem = new MemoryST("mem", m_pmemDataSim, 0, cf.m_nMemorySize-1,cf.m_nMemoryAccessTime);
#ifndef MEMBUS_CHANNELSWITCH
    clog << "Root and Channel are associated in this configuration." << endl;
    if (cf.m_nSplitRootNumber != cf.m_nMemoryChannelNumber)
    {
        clog << "warning: the configuration does not allow nChannel to be different than nRoot. Configuration is overwritten." << endl;
        cf.m_nMemoryChannelNumber = cf.m_nSplitRootNumber;
    }
#endif

    // split directory memory bus
#ifdef MEMBUS_CHANNELSWITCH
    uint64_t chselmode = 0;
    if (cf.m_nChannelInterleavingScheme == SC_INTERLEAVE_DIRECT)
    {
        chselmode = (int)ceil(log2(cf.m_nLineSize)) | ((int)(ceil(log2(cf.m_nMemoryChannelNumber)))<<8);
        // interleaving directly on set bits
    }
    else if (cf.m_nChannelInterleavingScheme == SC_INTERLEAVE_KSKEW)
    {
        int k = 1;
        chselmode = (int)ceil(log2(cf.m_nLineSize)) | (0x0f<<8) | (1 << 12) | (k << 16);
        // k-skew 
    }
    m_pBSMem = new BusSwitch("membusswitch", cf.m_nSplitRootNumber, cf.m_nMemoryChannelNumber, chselmode);
#else
    m_ppBusMem = (BusST**)malloc(cf.m_nSplitRootNumber*sizeof(BusST*));
#endif

    if (!bddrdefault)
    {
        int totalchannels = 0;
        for (unsigned int i=0;i<cf.m_nMemoryChannelNumber;i++)
        {
            int idinterface = (*pddrconfig)[i%ddrinterfacecount];
            map<int, ddr_interface*>::iterator iter = pddrinterfaces->find(idinterface);

            totalchannels += iter->second->nChannel;
        }

        clog << "total number of ddr-channels : " << totalchannels << "." << endl;
    }


    m_ppMem = (DDRMemorySys**)malloc(cf.m_nMemoryChannelNumber*sizeof(DDRMemorySys*));


    // split directories are created while connecting components
    m_ppDirectoryRoot = (DIRRTIM_DEF**)malloc(sizeof(DIRRTIM_DEF*)*cf.m_nSplitRootNumber);
//    m_pDirectoryRoot = new DIRRTIM_DEF("DirRoot", cf.m_nCacheSet, cf.m_nCacheAssociativity*cf.m_nCache, cf.m_nLineSize, 0, cf.m_nMemorySize-1, cf.m_nCacheAccessTime);



//    m_pMem = new DDRMemorySys("ddrmem", m_pmemDataSim);

    //////////////////////////////////////////////////////////////////////////
    // connecting parts

    unsigned int nfixed = cf.m_nProcLink / cf.m_nCache;
    unsigned int nextra = cf.m_nProcLink % cf.m_nCache;
    unsigned int npid = 0;
    unsigned int nsplitrd = 0;

    for (unsigned int i=0;i<cf.m_nCache;i++)
    {
        clog << "cache " << i << ": ";

        // bus 
        m_ppBus[i]->clk(*m_pclk);
        m_ppBus[i]->BindSlave(*m_ppCacheL2[i]);

        // cache clock
        m_ppCacheL2[i]->port_clk(*m_pclk);

        #ifdef MEM_CACHE_LEVEL_ONE_SNOOP
        // prepare peers
        vector<LinkMGS*> vecpeers;
        unsigned int assocachenum = 0;
        #endif

        for (unsigned int j=0;j<nfixed;j++)
        {
            // processor
            m_ppProcessors[npid]->port_clk(*m_pclk);
            m_ppBus[i]->BindMaster(*m_ppProcessors[npid]);

            // cache
            m_ppCacheL2[i]->BindProcTopology(*m_ppProcessors[npid]);

            #ifdef MEM_CACHE_LEVEL_ONE_SNOOP
            // manage peers
            vecpeers.push_back(g_pLinks[npid]);

            assocachenum++;
            #endif

            clog << "cpu " << npid << ", ";
            npid++;
        }

        // extra processor if any
        if (nextra > 0)
        {
            // processor
            m_ppProcessors[npid]->port_clk(*m_pclk);
            m_ppBus[i]->BindMaster(*m_ppProcessors[npid]);

            // cache
            m_ppCacheL2[i]->BindProcTopology(*m_ppProcessors[npid]);

            #ifdef MEM_CACHE_LEVEL_ONE_SNOOP
            // manage peers
            vecpeers.push_back(g_pLinks[npid]);

            assocachenum++;
            #endif

            clog << "cpu " << npid << ", ";
            npid++;
            nextra--;
        }

        #ifdef MEM_CACHE_LEVEL_ONE_SNOOP
        // binding peers
        for (unsigned int k=0;k<assocachenum;k++)
        {
            unsigned int cind = npid - 1 - k;

            g_pLinks[cind]->BindPeers(vecpeers);
        }
        #endif


        clog << endl;
    }

    // bind directories if any specified
    if (cf.m_nDirectory > 0)
    {
        nfixed = cf.m_nCache / cf.m_nDirectory;
        nextra = cf.m_nCache % cf.m_nDirectory;
        unsigned int ncid = 0;

        // create level0 network
        m_ppNetL0 = (Network**)malloc(sizeof(Network*)*cf.m_nDirectory);

        // create directories array container
        m_ppDirectoryL0 = (DirectoryTOKIM**)malloc(sizeof(DirectoryTOKIM*)*cf.m_nDirectory);


        for (unsigned int i=0;i<cf.m_nDirectory;i++)
        {
            // creating lower level network
            sprintf(tempname, "net-l0-%d", i);
            m_ppNetL0[i] = new Network(tempname);

            bool bextra = false;
            clog << m_ppNetL0[i]->name() << " : ";

            // bind clock
            m_ppNetL0[i]->port_clk(*m_pclk);

            // bind cache
            for (unsigned int j=0;j<nfixed;j++)
            {
                // bind cache to the network
                m_ppNetL0[i]->port_net(*(m_ppCacheL2[ncid]));

                clog << m_ppCacheL2[ncid]->name() << " joined the network;" << endl;
                ncid ++;

            }

            if (nextra > 0)
            {
                m_ppNetL0[i]->port_net(*(m_ppCacheL2[ncid]));
                clog << m_ppCacheL2[ncid]->name() << " joined the network;" << endl;

                ncid ++;
                nextra--;
                bextra = true;
            }

            // create directory
            sprintf(tempname, "dir-l0-%d", i);
            m_ppDirectoryL0[i] = new DirectoryTOKIM(tempname, cf.m_nCacheSet, cf.m_nCacheAssociativity*(nfixed+(bextra?1:0)), cf.m_nLineSize, injpol, 0, cf.m_nMemorySize-1, cf.m_nCacheAccessTime);

            // bind clock
            m_ppDirectoryL0[i]->port_clk(*m_pclk);

             // bind directory to network
            m_ppNetL0[i]->port_net(m_ppDirectoryL0[i]->GetBelowIF());
            clog << m_ppDirectoryL0[i]->name() << ".below joined the network." << endl;

            // connect network
            m_ppNetL0[i]->ConnectNetwork();
        }

        // connect directories to the root-network
        clog << m_pNet->name() << " : ";

        // clock for root-network
        m_pNet->port_clk(*m_pclk);

        static int lastid = 0;
        int modsp = cf.m_nDirectory%cf.m_nSplitRootNumber;
        int nocsp = cf.m_nDirectory/cf.m_nSplitRootNumber;

        for (unsigned int i=0;i<cf.m_nDirectory;i++)
        {
            // connect with root-level network
            m_pNet->port_net(m_ppDirectoryL0[i]->GetAboveIF());
            clog << m_ppDirectoryL0[i]->name() << ".above joined the root-level network;" << endl;

//            if ((i+1) >= (unsigned int)(floor((double)cf.m_nDirectory/(double)cf.m_nSplitRootNumber)*(nsplitrd+1)))
            if ((int)i+1  >= lastid + nocsp+((modsp>0)?1:0))
            {
                if (modsp-- == 0 )
                    modsp = 0;

                sprintf(tempname, "split-root-%d", nsplitrd);
                unsigned int nspmode = (cf.m_nSplitRootNumber << 8)|nsplitrd;
//                cout << "nspmode " << nspmode << endl;
                m_ppDirectoryRoot[nsplitrd] = new DIRRTIM_DEF(tempname, cf.m_nCacheSet/cf.m_nSplitRootNumber, cf.m_nCacheAssociativity*cf.m_nCache, cf.m_nLineSize, injpol, nspmode, 0, cf.m_nMemorySize-1, cf.m_nCacheAccessTime);
            	m_pNet->port_net(*m_ppDirectoryRoot[nsplitrd]);
	            clog << m_ppDirectoryRoot[nsplitrd]->name() << " joined the network." << endl;
   
#ifndef MEMBUS_CHANNELSWITCH
                sprintf(tempname, "ddrmem-sp-ch-%d", nsplitrd);
                if (bddrdefault)
                    m_ppMem[nsplitrd] = new DDRMemorySys(tempname, m_pmemDataSim);
                else
                {
                    int idinterface = (*pddrconfig)[nsplitrd%ddrinterfacecount];
                    map<int, ddr_interface*>::iterator iter = pddrinterfaces->find(idinterface);
                    assert(iter != pddrinterfaces->end());
                    ddr_interface* pinter = (*iter).second;

                    m_ppMem[nsplitrd] = new DDRMemorySys(tempname, m_pmemDataSim,  pinter->tAL, pinter->tCL, pinter->tCWL, pinter->tRCD, pinter->tRP, pinter->tRAS, pinter->nRankBits, pinter->nBankBits, pinter->nRowBits, pinter->nColumnBits, pinter->nCellSizeBits, pinter->nChannel, pinter->nModeChannel, pinter->nDevicePerRank, pinter->nBurstLength);

                }

                sprintf(tempname, "membus-sp-%d", nsplitrd);
                m_ppBusMem[nsplitrd] = new BusST(tempname);
#endif
                nsplitrd ++;

                lastid = i+1;
            }

        }

//    	m_pNet->port_net(*m_pDirectoryRoot);
//	    clog << m_pDirectoryRoot->name() << " joined the roo-level network." << endl;

        // connect network
        if (nsplitrd >= cf.m_nSplitRootNumber)
            m_pNet->ConnectNetwork();
    }
    else    // no low level directories
    {
        // clock for root-network
        m_pNet->port_clk(*m_pclk);

        int modsp = cf.m_nCache%cf.m_nSplitRootNumber;
        int nocsp = cf.m_nCache/cf.m_nSplitRootNumber;
        // bind every cache together with the root directory on the single level ring
        for (unsigned int i=0;i<cf.m_nCache;i++)
        {
            static int lastid = 0;
            m_pNet->port_net(*m_ppCacheL2[i]);
            clog << m_ppCacheL2[i]->name() << " is connected with single level ring network;" << endl;

//            cout << i+1  << " " <<  ((nsplitrd+1)*nocsp+((modsp>0)?1:0)) << endl;
//            if ((i+1) >= (unsigned int)(floor((double)cf.m_nCache/(double)cf.m_nSplitRootNumber)*(nsplitrd+1)))
            if ((int)i+1  >= lastid + nocsp+((modsp>0)?1:0))
            {
                if (modsp-- == 0 )
                    modsp = 0;

                sprintf(tempname, "split-root-%d", nsplitrd);
                unsigned int nspmode = (cf.m_nSplitRootNumber << 8)|nsplitrd;
                m_ppDirectoryRoot[nsplitrd] = new DIRRTIM_DEF(tempname, cf.m_nCacheSet/cf.m_nSplitRootNumber, cf.m_nCacheAssociativity*cf.m_nCache, cf.m_nLineSize, injpol, nspmode, 0, cf.m_nMemorySize-1, cf.m_nCacheAccessTime);
            	m_pNet->port_net(*m_ppDirectoryRoot[nsplitrd]);
	            clog << m_ppDirectoryRoot[nsplitrd]->name() << " joined the network." << endl;

#ifndef MEMBUS_CHANNELSWITCH
                sprintf(tempname, "ddrmem-sp-ch-%d", nsplitrd); 
                if (bddrdefault)
                    m_ppMem[nsplitrd] = new DDRMemorySys(tempname, m_pmemDataSim);
                else
                {
                    int idinterface = (*pddrconfig)[nsplitrd%ddrinterfacecount];
                    map<int, ddr_interface*>::iterator iter = pddrinterfaces->find(idinterface);
                    assert(iter != pddrinterfaces->end());
                    ddr_interface* pinter = (*iter).second;

                    m_ppMem[nsplitrd] = new DDRMemorySys(tempname, m_pmemDataSim,  pinter->tAL, pinter->tCL, pinter->tCWL, pinter->tRCD, pinter->tRP, pinter->tRAS, pinter->nRankBits, pinter->nBankBits, pinter->nRowBits, pinter->nColumnBits, pinter->nCellSizeBits, pinter->nChannel, pinter->nModeChannel, pinter->nDevicePerRank, pinter->nBurstLength);

                }
//                m_ppMem[nsplitrd] = new DDRMemorySys(tempname, m_pmemDataSim);

                sprintf(tempname, "membus-sp-%d", nsplitrd);
                m_ppBusMem[nsplitrd] = new BusST(tempname);
#endif
                nsplitrd ++;

                lastid = i+1;
            }

        }

//    	m_pNet->port_net(*m_pDirectoryRoot);
//	    clog << m_pDirectoryRoot->name() << " joined the network." << endl;

        // connect network
        if (nsplitrd >= cf.m_nSplitRootNumber)
            m_pNet->ConnectNetwork();
    }

    if ( ((cf.m_nDirectory > 0) &&(cf.m_nSplitRootNumber > cf.m_nDirectory) ) || ((cf.m_nDirectory <= 0) && (cf.m_nSplitRootNumber > cf.m_nCache)) )
    {
        int nscurrent;
        if (cf.m_nDirectory>0)
            nscurrent = cf.m_nDirectory;
        else
            nscurrent = cf.m_nCache;

        assert(nsplitrd == (unsigned int)nscurrent);

        for (int i=nscurrent;i<(int)cf.m_nSplitRootNumber;i++)
        {
            sprintf(tempname, "split-root-%d", nsplitrd);
            unsigned int nspmode = (cf.m_nSplitRootNumber << 8)|nsplitrd;
            m_ppDirectoryRoot[nsplitrd] = new DIRRTIM_DEF(tempname, cf.m_nCacheSet/cf.m_nSplitRootNumber, cf.m_nCacheAssociativity*cf.m_nCache, cf.m_nLineSize, injpol, nspmode, 0, cf.m_nMemorySize-1, cf.m_nCacheAccessTime);
            m_pNet->port_net(*m_ppDirectoryRoot[nsplitrd]);
            clog << m_ppDirectoryRoot[nsplitrd]->name() << " joined the network." << endl;

#ifndef MEMBUS_CHANNELSWITCH
            sprintf(tempname, "ddrmem-sp-ch-%d", nsplitrd); 
            if (bddrdefault)
                m_ppMem[nsplitrd] = new DDRMemorySys(tempname, m_pmemDataSim);
            else
            {
                int idinterface = (*pddrconfig)[nsplitrd%ddrinterfacecount];
                map<int, ddr_interface*>::iterator iter = pddrinterfaces->find(idinterface);
                assert(iter != pddrinterfaces->end());
                ddr_interface* pinter = (*iter).second;

                m_ppMem[nsplitrd] = new DDRMemorySys(tempname, m_pmemDataSim,  pinter->tAL, pinter->tCL, pinter->tCWL, pinter->tRCD, pinter->tRP, pinter->tRAS, pinter->nRankBits, pinter->nBankBits, pinter->nRowBits, pinter->nColumnBits, pinter->nCellSizeBits, pinter->nChannel, pinter->nModeChannel, pinter->nDevicePerRank, pinter->nBurstLength);

            }
//                m_ppMem[nsplitrd] = new DDRMemorySys(tempname, m_pmemDataSim);
            sprintf(tempname, "membus-sp-%d", nsplitrd);
            m_ppBusMem[nsplitrd] = new BusST(tempname);
#endif
            nsplitrd ++;
        }

        if (nsplitrd >= cf.m_nSplitRootNumber)
            m_pNet->ConnectNetwork();
    }

#ifdef MEMBUS_CHANNELSWITCH
    m_pBSMem->clk(*m_pclk);
#endif

    for (unsigned int i=0;i<cf.m_nSplitRootNumber;i++)
    {
        // directory root
        m_ppDirectoryRoot[i]->port_clk(*m_pclk);

        // busmem
#ifdef MEMBUS_CHANNELSWITCH
        m_pBSMem->BindMaster(*m_ppDirectoryRoot[i]);
#else
        m_ppBusMem[i]->BindMaster(*m_ppDirectoryRoot[i]);

        m_ppBusMem[i]->clk(*m_pclk);
        m_ppBusMem[i]->port_slave(*m_ppMem[i]);
        m_ppBusMem[i]->port_fifo_slave_in(m_ppMem[i]->channel_fifo_slave);

        // memory
        m_ppMem[i]->port_clk(*m_pclkmem);
#endif
    }

#ifdef MEMBUS_CHANNELSWITCH
    for (unsigned int i=0;i<cf.m_nMemoryChannelNumber;i++)
    {
        sprintf(tempname, "ddrmem-ch-%d", i); 
        if (bddrdefault)
            m_ppMem[i] = new DDRMemorySys(tempname, m_pmemDataSim);
        else
        {
            int idinterface = (*pddrconfig)[i%ddrinterfacecount];
            map<int, ddr_interface*>::iterator iter = pddrinterfaces->find(idinterface);
            assert(iter != pddrinterfaces->end());
            ddr_interface* pinter = (*iter).second;

            m_ppMem[i] = new DDRMemorySys(tempname, m_pmemDataSim,  pinter->tAL, pinter->tCL, pinter->tCWL, pinter->tRCD, pinter->tRP, pinter->tRAS, pinter->nRankBits, pinter->nBankBits, pinter->nRowBits, pinter->nColumnBits, pinter->nCellSizeBits, pinter->nChannel, pinter->nModeChannel, pinter->nDevicePerRank, pinter->nBurstLength);

        }

        m_pBSMem->BindSlave(*m_ppMem[i]);

        // memory
        m_ppMem[i]->port_clk(*m_pclkmem);
    }
#endif


//    // directory root
//    m_pDirectoryRoot->port_clk(*m_pclk);
//    m_pBusMem->BindMaster(*m_pDirectoryRoot);
//
//    // busmem
//    m_pBusMem->clk(*m_pclk);
//    m_pBusMem->port_slave(*m_pMem);
//    m_pBusMem->port_fifo_slave_in(m_pMem->channel_fifo_slave);
//
//    // memory
//    m_pMem->port_clk(*m_pclk);
//

    // initialize the log
#if defined(MEM_ENABLE_DEBUG) && ( MEM_ENABLE_DEBUG >= MEM_DEBUG_LEVEL_LOG )
    if (cf.m_pGlobalLogFile != NULL)
        SimObj::SetGlobalLog(cf.m_pGlobalLogFile);

    for (unsigned int i=0;i<cf.m_nProcLink;i++)
    {
        m_ppProcessors[i]->InitializeLog(NULL, SimObj::LOG_GLOBAL);
    }

    for (unsigned int i=0;i<cf.m_nCache;i++)
    {
        m_ppCacheL2[i]->InitializeLog(NULL, SimObj::LOG_GLOBAL);
        m_ppBus[i]->InitializeLog(NULL, SimObj::LOG_GLOBAL);
//        m_ppCacheL2[i]->InitializeLog(NULL, SimObj::LOG_GLOBAL, SimObj::VERBOSE_NONE);
//        m_ppBus[i]->InitializeLog(NULL, SimObj::LOG_GLOBAL, SimObj::VERBOSE_NONE);
    }
    for (unsigned int i=0;i<cf.m_nDirectory;i++)
    {
//        if (i == 4)
            m_ppDirectoryL0[i]->InitializeLog(NULL, SimObj::LOG_GLOBAL);
//        else
//            m_ppDirectoryL0[i]->InitializeLog(NULL, SimObj::LOG_GLOBAL, SimObj::VERBOSE_NONE);
        
        m_ppNetL0[i]->InitializeLog(NULL, SimObj::LOG_GLOBAL);
    }
    m_pNet->InitializeLog(NULL, SimObj::LOG_GLOBAL);


    for(unsigned int i=0;i<cf.m_nSplitRootNumber;i++)
    {
        m_ppDirectoryRoot[i]->InitializeLog(NULL, SimObj::LOG_GLOBAL);
#ifndef MEMBUS_CHANNELSWITCH
        m_ppBusMem[i]->InitializeLog(NULL, SimObj::LOG_GLOBAL);
#endif
    }

    for (unsigned int i=0;i<cf.m_nMemoryChannelNumber;i++)
        m_ppMem[i]->InitializeLog(NULL, SimObj::LOG_GLOBAL);

#ifdef MEMBUS_CHANNELSWITCH
    m_pBSMem->InitializeLog(NULL, SimObj::LOG_GLOBAL);
#endif

//    m_pDirectoryRoot->InitializeLog(NULL, SimObj::LOG_GLOBAL);
//    m_pBusMem->InitializeLog(NULL, SimObj::LOG_GLOBAL);
//    m_pMem->InitializeLog(NULL, SimObj::LOG_GLOBAL);
    m_pmemDataSim->InitializeLog(NULL, SimObj::LOG_GLOBAL);

#endif

    //////////////////////////////////////////////////////////////////////////
    //// initialize log
    //if (cf.m_pGlobalLogFile != NULL)
    //    SimObj::SetGlobalLog(cf.m_pGlobalLogFile);

    //p1.InitializeLog(NULL, p1.LOG_GLOBAL);
    //p2.InitializeLog(NULL, p2.LOG_GLOBAL);
    //p3.InitializeLog(NULL, p3.LOG_GLOBAL);
    //p4.InitializeLog(NULL, p4.LOG_GLOBAL);

    //bus2a.InitializeLog(NULL, bus2a.LOG_GLOBAL);
    //bus2b.InitializeLog(NULL, bus2b.LOG_GLOBAL);
    //bus2c.InitializeLog(NULL, bus2c.LOG_GLOBAL);
    //bus2d.InitializeLog(NULL, bus2d.LOG_GLOBAL);

    //cache2a.InitializeLog(NULL, cache2a.LOG_GLOBAL);
    //cache2b.InitializeLog(NULL, cache2b.LOG_GLOBAL);
    //cache2c.InitializeLog(NULL, cache2c.LOG_GLOBAL);
    //cache2d.InitializeLog(NULL, cache2d.LOG_GLOBAL);

    //net.InitializeLog(NULL, net.LOG_GLOBAL);

    //dirroot.InitializeLog(NULL, dirroot.LOG_GLOBAL);

    //busmem.InitializeLog(NULL, busmem.LOG_GLOBAL);

    //mem.InitializeLog(NULL, mem.LOG_GLOBAL);
    //memDataSim.InitializeLog(NULL, memDataSim.LOG_GLOBAL);
    ////memDataSim.InitializeLog("memData", memDataSim.LOG_LOCAL, memDataSim.VERBOSE_ALL);

    clog << endl << endl;

	//////////////////////////////////////////////////////////////////////////
	// push the objects to the object vector
	// add items into simobjs
    for (unsigned int i=0;i<cf.m_nProcLink;i++)
    {
        g_vSimObjs.push_back(m_ppProcessors[i]);
    }
	for (unsigned int i=0;i<cf.m_nCache;i++)
	{
		g_vSimObjs.push_back(m_ppCacheL2[i]);
	}
    for (unsigned int i=0;i<cf.m_nDirectory;i++)
    {
        g_vSimObjs.push_back(m_ppDirectoryL0[i]);
    }

   for (unsigned int i=0;i<cf.m_nSplitRootNumber;i++)
    {
        g_vSimObjs.push_back(m_ppDirectoryRoot[i]);
    }

//	g_vSimObjs.push_back(m_pMem);
	//g_vSimObjs.push_back(m_pmemDataSim);


#ifdef MEM_MODULE_STATISTICS
    //////////////////////////////////////////////////////////////////////////
    // statistics
 //   m_pMem->InitializeStatistics(STAT_MEMORY_COMP_STORE);
    for (unsigned int i=0;i<cf.m_nCache;i++)
    {
        //m_ppCacheL2[i]->InitializeStatistics(STAT_CACHE_COMP_SEND_NODE_INI);
        //m_ppCacheL2[i]->InitializeStatistics(STAT_CACHE_COMP_PROCESSING_INI);
        //m_ppCacheL2[i]->InitializeStatistics(STAT_CACHE_COMP_SEND_NODE_NET);
        //m_ppCacheL2[i]->InitializeStatistics(STAT_CACHE_COMP_REQUEST_NO);
    }
    //m_pMem->InitializeStatistics(STAT_MEMORY_COMP_PROCESSING_INI);
    //m_pMem->InitializeStatistics(STAT_MEMORY_COMP_REQUEST_NO);
    //m_ppCacheL2[0]->InitializeStatistics(STAT_CACHE_COMP_PROCESSING_INI);
    //m_ppCacheL2[0]->InitializeStatistics(STAT_CACHE_COMP_PROCESSING_NET);
    //m_ppCacheL2[0]->InitializeStatistics(STAT_CACHE_COMP_CLEANSING_INI);
    //m_ppCacheL2[0]->InitializeStatistics(STAT_CACHE_COMP_SEND_NODE_INI);
    //m_ppCacheL2[0]->InitializeStatistics(STAT_CACHE_COMP_PIPELINE_INI);
    //m_ppCacheL2[0]->InitializeStatistics(STAT_CACHE_COMP_TEST);
    //m_pDirectoryRoot->InitializeStatistics(STAT_RTDIR_COMP_SET_TRACE);
    for (unsigned int i=0;i<cf.m_nProcLink;i++)
    {
//        m_ppProcessors[i]->InitializeStatistics(STAT_PROC_COMP_LATENCY);
        //m_ppProcessors[i]->InitializeStatistics(STAT_PROC_COMP_REQ_SENT);
    }
#endif
}


TopologyS::~TopologyS()
{
    // parsed config
    LinkConfig &cf = LinkMGS::s_oLinkConfig;
    assert(cf.m_nCache <= cf.m_nProcLink);

    delete m_pclk;
    delete m_pclkmem;

    delete m_pmemDataSim;

    for (unsigned int i=0;i<cf.m_nProcLink;i++)
        delete m_ppProcessors[i];

    free(m_ppProcessors);

    for (unsigned int i=0;i<cf.m_nCache;i++)
    {
        delete m_ppBus[i];
        delete m_ppCacheL2[i];
    }

    free(m_ppBus);
    free(m_ppCacheL2);

    delete m_pNet;

    for (unsigned int i=0;i<cf.m_nSplitRootNumber;i++)
    {
#ifndef MEMBUS_CHANNELSWITCH
        delete m_ppBusMem[i];
#endif

        delete m_ppDirectoryRoot[i];
    }

    for (unsigned int i=0;i<cf.m_nMemoryChannelNumber;i++)
        delete m_ppMem[i];

#ifdef MEMBUS_CHANNELSWITCH
    delete(m_pBSMem);
#else
    free(m_ppBusMem);
#endif
    free(m_ppDirectoryRoot);
    free(m_ppMem);

//    delete m_pMem;
}


void TopologyS::PreFill()
{
    LinkConfig &cf = LinkMGS::s_oLinkConfig;
    char *data = (char*)malloc(g_nCacheLineSize);

    /////////////////////////////////////
    // for N == 8
    // arrange the reagion to prefill
    // 0x0 - 0x13f
    // 0x12000 - 0x112fff
    // -- 0x212020
    /////////////////////////////////////

    unsigned int ci=0;
    for (__address_t addr=0;addr<0x13f;addr+=0x40,ci=(ci+1)%cf.m_nCache)
    {
        m_pmemDataSim->Fetch(addr, g_nCacheLineSize, data);
        if (m_ppCacheL2[ci]->PreFill(addr, data))
//            m_ppDirectoryRoot[0]->PreFill(addr);
        assert (false);
    }

    ci=0;
    for (__address_t addr=0x12000;addr<0x112fff;addr+=0x40,ci=(ci+1)%cf.m_nCache)
    {
        m_pmemDataSim->Fetch(addr, g_nCacheLineSize, data);
        if (m_ppCacheL2[ci]->PreFill(addr, data))
//            m_pDirectoryRoot[0]->PreFill(addr);
        assert (false);
    }

    /////////////////////////////////////
    // for N == 12
    // arrange the reagion to prefill
    // 0x0 - 0x13f
    // 0x12000 - 0x121fff
    // -- 0x211fff
    /////////////////////////////////////

}

