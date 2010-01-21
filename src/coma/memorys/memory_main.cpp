//////////////////////////////////////////////////////////////////////////
// sc_main

#include "predef.h"
#include "topologys.h"
#include <unistd.h>
#include <cstdio>

// include linkmgs and th header files, 
// the instance is also defined in the realization file
#include "../simlink/linkrealization.h"

using namespace MemSim;

int mgs_main(int argc, const char* argv[]);

void* thread_proc(void*)
{
	mgs_main(thpara.argc, (const char**)thpara.argv);
	exit(0);
}


void init_journal_name()
{
#ifdef USE_IPC_SEMS
    static char buf[128];
    snprintf(buf, sizeof(buf), "%s-%d", semaphore_journal, getuid());
    semaphore_journal = buf;
#endif
}

int sc_main(int argc, char* argv[] )
{
	//////////////////////////////////////////////////////////////////////////
	// create thread for processor simulator

    init_journal_name();

	cerr << "# SCM: starting MGSim thread..." << endl;

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
	cerr << "# SCM: MGSim thread started, initializing memory..." << endl;

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
	cerr << "# SCM: Memory initialized, control back to simulator..." << endl;
	sem_post(&thpara.sem_mgs);
    sem_wait(&thpara.sem_sync);

	//////////////////////////////////////////////////////////////////////////
	// start the simulation

	sc_start(0, SC_PS);
	while(1)
	{
		sem_wait(&thpara.sem_sync);
		if (thpara.bterm)
			break;
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
