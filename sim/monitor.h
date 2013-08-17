#ifndef MONITOR_H
# define MONITOR_H

#include <fstream>
#include <ctime>
#include <thread>
#include <mutex>

namespace Simulator {
    class MGSystem;
}

class BinarySampler;

class Monitor
{
    Simulator::MGSystem&  m_sys;
    std::ofstream*        m_outputfile;
    struct timespec       m_tsdelay;

    std::thread*          m_monitorthread;
    std::mutex            m_runlock;
    BinarySampler*        m_sampler;

    bool                  m_quiet;
    bool                  m_running;
    volatile bool         m_enabled;

    friend void* runmonitor(void*);
    void run();

public:
    Monitor(Simulator::MGSystem& sys, bool enable, const std::string& mdfile, const std::string& outfile, bool quiet);
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;
    ~Monitor();

    void start();
    void stop();
};

#endif
