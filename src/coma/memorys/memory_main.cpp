//////////////////////////////////////////////////////////////////////////
// sc_main

#include "predef.h"
#include "topologys.h"


#include "../simlink/mgs_main.h"
// include linkmgs and th header files, 
// the instance is also defined in the realization file
#include "../simlink/linkrealization.h"
using namespace MemSim;

namespace MemSim{
MemoryDataContainer* g_MemoryDataContainer;
vector<SimObj*>  g_vSimObjs;
}

#ifdef WIN32
unsigned int thread_proc(void* param)
{
	mgs_main(thpara.argc, (const char**)thpara.argv);
	return 0;
}
#else
void* thread_proc(void*)
{
	mgs_main(thpara.argc, (const char**)thpara.argv);
	return NULL;
}
#endif

//void SimIni()
//{
//    SimObj::SetGlobalLog("..\\log\\log");
//}

int sc_main(int argc, char* argv[] )
{
	//////////////////////////////////////////////////////////////////////////
	// create thread for processor simulator

	// set parameter
	thpara.argc = argc;
	thpara.argv = argv;
	thpara.bterm = false;

#ifdef WIN32
	// windows thread creation and event creation
	thpara.heventsysc = (unsigned long*)CreateEvent(NULL,FALSE,FALSE,(LPWSTR)"Test");
	thpara.heventmgs = (unsigned long*)CreateEvent(NULL,FALSE,FALSE,(LPWSTR)"mgssim");

	unsigned long* hThread;
	hThread = (unsigned long*)CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)thread_proc, NULL,0,NULL);

	if(hThread == NULL)
	{
		DWORD dwError = GetLastError();
		cout<<"SCM:Error in Creating thread"<<dwError<<endl ;
		return 1;
	}

	WaitForSingleObject(thpara.heventsysc,INFINITE);
#else
	// linux thread creation and semaphore initialization
	sem_init(&thpara.sem_mgs, 0, 0);
	sem_init(&thpara.sem_sync, 0, 0);

	pthread_t pTh;
	int thret = pthread_create(&pTh, NULL, thread_proc, NULL);
	if (thret != 0)
	{
		cout << "SCM: Failed to create memory simulation thread." << endl ;
		return 1;
	}

	sem_wait(&thpara.sem_sync);
    if (thpara.bterm)
        return 0;
#endif

	//////////////////////////////////////////////////////////////////////////
	// create topology in SystemC
	TopologyS top;


	//////////////////////////////////////////////////////////////////////////
	// simulate both in synchronized steps
	// systemc setup is done, give the control back to simulator
#ifdef WIN32
	SetEvent(thpara.heventmgs);
	WaitForSingleObject(thpara.heventsysc,INFINITE);
#else
	sem_post(&thpara.sem_mgs);
    sem_wait(&thpara.sem_sync);
#endif

// only for prefill purpose
#ifdef MEM_DATA_PREFILL
#ifdef WIN32
	WaitForSingleObject(thpara.heventsysc,INFINITE);
#else
	sem_wait(&thpara.sem_sync);
#endif
    top.PreFill();
#ifdef WIN32
    SetEvent(thpara.heventmgs);
#else
    sem_post(&thpara.sem_mgs);
#endif

#endif 

	//////////////////////////////////////////////////////////////////////////
	// start the simulation

	sc_start(0, SC_PS);
	while(1)
	{
#ifdef WIN32
		WaitForSingleObject(thpara.heventsysc,INFINITE);
#else
		sem_wait(&thpara.sem_sync);
#endif
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

#ifdef WIN32
		SetEvent(thpara.heventmgs);
#else
		sem_post(&thpara.sem_mgs);
#endif

	}

	sc_stop();

#ifdef WIN32
    CloseHandle(thpara.heventsysc);
CloseHandle(thpara.heventmgs);
#else
    sem_destroy(&thpara.sem_mgs);
    sem_destroy(&thpara.sem_sync);
#endif

#ifdef WIN32
    SetEvent(thpara.heventmgs);
#else
    sem_post(&thpara.sem_mgs);
#endif

#ifdef WIN32
#else
    pthread_join(pTh, NULL);
#endif

	return 0;
}
