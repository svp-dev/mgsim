#ifndef _TH_H
#define _TH_H

#include <pthread.h>

#define USE_IPC_SEMS
#ifdef USE_IPC_SEMS
#include <cstdio>
#include <cstdlib>
#include <sys/errno.h>
#include <sys/sem.h>
#include <sys/stat.h>

     union semun_ipc {
             int     val;            /* value for SETVAL */
             struct  semid_ds *buf;  /* buffer for IPC_STAT & IPC_SET */
             unsigned short *array;         /* array for GETALL & SETALL */
     };

extern const char* semaphore_journal;


#define sem_init(Sem, Shared, Val) do {					\
    if (-1 == (*(Sem) = semget(IPC_PRIVATE, 1, 0600|IPC_CREAT))) { perror("semget"); abort(); } \
    FILE *jf = fopen(semaphore_journal, "a");				\
    if (jf == 0) { perror("fopen"); abort(); }				\
    fprintf(jf, "%ld %lu\n", (long)(*(Sem)), (unsigned long)getpid());		\
    fclose(jf);								\
    union semun_ipc arg; arg.val = (Val);				\
    if (-1 == semctl(*(Sem), 0, SETVAL, arg)) { perror("semctl"); abort(); } \
  } while(0)
#define sem_post(Sem) do {					\
    struct sembuf sop = { 0, 1, 0 };					\
    int semop_ret;							\
    while (-1 == (semop_ret = semop(*(Sem), &sop, 1)) && errno == EINTR) {}; \
    if (-1 == semop_ret) { if (errno == EIDRM) pthread_exit(0); perror("semop"); abort(); } \
  } while(0)
#define sem_wait(Sem) do {	  \
    struct sembuf sop = { 0, -1, 0 };					\
    int semop_ret;							\
    while (-1 == (semop_ret = semop(*(Sem), &sop, 1)) && errno == EINTR) {}; \
    if (-1 == semop_ret) { if (errno == EIDRM) pthread_exit(0); perror("semop"); abort(); } \
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

