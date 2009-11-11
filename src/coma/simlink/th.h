#ifndef _TH_H
#define _TH_H

#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <semaphore.h>
#endif

typedef struct _th_t{
    int argc;
    char** argv;
    int* pmtcmd;
#ifdef WIN32
    unsigned long* heventsysc;
    unsigned long* heventmgs;
#else
	sem_t sem_sync;
	sem_t sem_mgs;
#endif
    bool bterm;
} th_t;

extern th_t thpara;
#endif

