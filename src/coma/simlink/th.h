#ifndef _TH_H
#define _TH_H

#include <pthread.h>

#define USE_IPC_SEMS
#ifdef USE_IPC_SEMS
#include <sys/sem.h>
#include <cstdio>
#include <cstdlib>
#include <sys/errno.h>
#define sem_init(Sem, Shared, Val) do {					\
    if (-1 == (*(Sem) = semget(IPC_PRIVATE, 1, SEM_R|SEM_A))) { perror("semget"); abort(); } \
    union semun arg; arg.val = (Val);					\
    if (-1 == semctl(*(Sem), 0, SETVAL, arg)) { perror("semctl"); abort(); } \
  } while(0)
#define sem_post(Sem) do {					\
    struct sembuf sop = { 0, 1, 0 };					\
    if (-1 == semop(*(Sem), &sop, 1)) { if (errno == EIDRM) pthread_exit(0); perror("semop"); abort(); } \
  } while(0)
#define sem_wait(Sem) do {	  \
    struct sembuf sop = { 0, -1, 0 };					\
    if (-1 == semop(*(Sem), &sop, 1)) { if (errno == EIDRM) pthread_exit(0); perror("semop"); abort(); } \
  } while(0)
#define sem_destroy(Sem) do {	 \
    semctl(*(Sem), IPC_RMID, 0); \
  } while(0)
typedef int sem_t;
#else
#include <semaphore.h>
#endif


typedef struct _th_t{
    int argc;
    char** argv;
    int* pmtcmd;

	sem_t sem_sync;
	sem_t sem_mgs;

    bool bterm;
} th_t;

extern th_t thpara;
#endif

