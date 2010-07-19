#ifndef MONITOR_H
# define MONITOR_H

#include "MGSystem.h"
#include "config.h"
#include <iostream>
#include <pthread.h>
#include <time.h>

static void* runmonitor(void*);

class Monitor
{
    Simulator::MGSystem&  m_sys;
    const Config&         m_config;
    std::ostream*         m_outputfile;
    bool                  m_quiet;
    struct timespec       m_tsdelay;
    
    pthread_t             m_monitorthread;
    pthread_mutex_t       m_runlock;
    bool                  m_running;
    volatile bool         m_enabled;

    friend void* runmonitor(void*);
    void run();

public:
    Monitor(Simulator::MGSystem& sys, const Config& config, const std::string& outfile, bool quiet);
    ~Monitor();

    void start();
    void stop();
};

#endif
