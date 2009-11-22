//////////////////////////////////////////////////////////////////////////
// sc_main

#include "predef.h"
#include "topologys.h"
#include <unistd.h>

#include "../simlink/mgs_main.h"
// include linkmgs and th header files, 
// the instance is also defined in the realization file
#include "../simlink/linkrealization.h"
using namespace MemSim;

namespace MemSim{
MemoryDataContainer* g_MemoryDataContainer;
vector<SimObj*>  g_vSimObjs;
}


void* thread_proc(void*)
{
	mgs_main(thpara.argc, (const char**)thpara.argv);
	exit(0);
}


//void SimIni()
//{
//    SimObj::SetGlobalLog("..\\log\\log");
//}

int sc_main(int argc, char* argv[] )
{
	//////////////////////////////////////////////////////////////////////////
	// create thread for processor simulator

	cerr << "Starting MGSim thread..." << endl;

	// set parameter
	thpara.argc = argc;
	thpara.argv = argv;
	thpara.bterm = false;

	// thread creation and semaphore initialization
	sem_init(&thpara.sem_mgs, 0, 0);
	sem_init(&thpara.sem_sync, 0, 0);

	pthread_t pTh;
	int thret = pthread_create(&pTh, NULL, thread_proc, NULL);
	if (thret != 0)
	{
	  perror("pthread_create");
		cout << "SCM: Failed to create memory simulation thread." << endl ;
		return 1;
	}

	sem_wait(&thpara.sem_sync);
	cerr << "MGSim thread started, initializing memory..." << endl;

    if (thpara.bterm)
        return 0;

	//////////////////////////////////////////////////////////////////////////
	// create topology in SystemC
    TopologyS *top;
    try {
      top = new TopologyS();
    } catch(std::exception& e) {
      cerr << e.what() << endl;
      exit(1);
    }

	//////////////////////////////////////////////////////////////////////////
	// simulate both in synchronized steps
	// systemc setup is done, give the control back to simulator

	cerr << "Memory initialized, control back to simulator..." << endl;
	sem_post(&thpara.sem_mgs);
    sem_wait(&thpara.sem_sync);


// only for prefill purpose
#ifdef MEM_DATA_PREFILL

	sem_wait(&thpara.sem_sync);

    top->PreFill();

    sem_post(&thpara.sem_mgs);


#endif 

	//////////////////////////////////////////////////////////////////////////
	// start the simulation

	sc_start(0, SC_PS);
	while(1)
	{

		sem_wait(&thpara.sem_sync);

		if (thpara.bterm)
			break;

#if defined(MEM_ENABLE_DEBUG) && ( MEM_ENABLE_DEBUG >= MEM_DEBUG_LEVEL_LOG )
#ifdef MEM_MODULE_STATISTICS
        // perform statistics
        PerformStatistics();
#endif

        // perform monitor procedures
        AutoMonitorProc();
#endif

		sc_start(LinkMGS::s_oLinkConfig.m_nCycleTimeCore, SC_PS);

		sem_post(&thpara.sem_mgs);


	}

	sc_stop();


    sem_destroy(&thpara.sem_mgs);
    sem_destroy(&thpara.sem_sync);



    pthread_join(pTh, NULL);
    delete top;
	return 0;
}
