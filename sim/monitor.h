#ifndef MONITOR_H
# define MONITOR_H

#include "arch/MGSystem.h"
#include "sampling.h"

#include <fstream>
#include <ctime>
#include <pthread.h>

static void* runmonitor(void*);

class Monitor
{
    Simulator::MGSystem&  m_sys;
    std::ofstream*        m_outputfile;
    bool                  m_quiet;
    struct timespec       m_tsdelay;
    
    pthread_t             m_monitorthread;
    pthread_mutex_t       m_runlock;
    bool                  m_running;
    volatile bool         m_enabled;

    BinarySampler*        m_sampler;

    friend void* runmonitor(void*);
    void run();

public:
    Monitor(Simulator::MGSystem& sys, bool enable, const std::string& mdfile, const std::string& outfile, bool quiet);
    ~Monitor();

    void start();
    void stop();
};

#endif
