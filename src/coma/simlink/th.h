#ifndef _TH_H
#define _TH_H

#include <pthread.h>

struct sem_t
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int signaled;
};

static inline void sem_init(sem_t* sem)
{
    pthread_mutex_init(&sem->mutex, NULL);
    pthread_cond_init(&sem->cond, NULL);
    sem->signaled = 0;
}

static inline void sem_post(sem_t* sem)
{
    pthread_mutex_lock(&sem->mutex);
    sem->signaled++;
    pthread_cond_signal(&sem->cond);
    pthread_mutex_unlock(&sem->mutex);
}

static inline void sem_wait(sem_t* sem)
{
    pthread_mutex_lock(&sem->mutex);
    while (sem->signaled == 0)
    {
        pthread_cond_wait(&sem->cond, &sem->mutex);
    }
    sem->signaled--;
    pthread_mutex_unlock(&sem->mutex);
}

static inline void sem_destroy(sem_t* sem)
{
    pthread_cond_destroy(&sem->cond);
    pthread_mutex_destroy(&sem->mutex);
}

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

