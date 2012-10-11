#include "monitor.h"
#include "sampling.h"
#include <arch/MGSystem.h>

#include <ios>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#define pthread(Function, ...) do { if (pthread_ ## Function(__VA_ARGS__)) perror("pthread_" #Function); } while(0)

using namespace std;

void* runmonitor(void *arg)
{
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGHUP);
    sigaddset(&sigset, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigset, 0);

    Monitor *m = (Monitor*) arg;
    m->run();
    return 0;
}

Monitor::Monitor(Simulator::MGSystem& sys, bool enabled, const string& mdfile, const string& outfile, bool quiet)
    : m_sys(sys),
      m_outputfile(0),
      m_tsdelay(),
      m_monitorthread(),
      m_runlock(),
      m_sampler(0),
      m_quiet(quiet),
      m_running(false),
      m_enabled(true)
{
    if (!enabled)
    {
        if (!m_quiet)
            clog << "# monitoring disabled." << endl;
        return ;
    }

    ofstream metadatafile(mdfile.c_str(), ios_base::out|ios_base::trunc);
    if (!metadatafile.good())
    {
        clog << "# warning: cannot write to file " << mdfile << ". Monitoring disabled." << endl;
        return ;
    }

    vector<string> pats = sys.GetConfig().getWordList("MonitorSampleVariables");
    pats.insert(pats.begin(), "kernel.cycle");
    pats.push_back("kernel.cycle");
    m_sampler = new BinarySampler(metadatafile, sys.GetConfig(), pats);
    metadatafile << "# tv_sizes: " << sizeof(((struct timeval*)(void*)0)->tv_sec)
                 << ' ' << sizeof(((struct timeval*)(void*)0)->tv_usec)
                 << ' ' << sizeof(struct timeval) << endl;
    metadatafile.close();

    if (outfile.empty())
        /* only metadata was requested */
        return ;

    m_outputfile = new ofstream(outfile.c_str(), ios_base::binary|ios_base::out|ios_base::trunc);
    if (!m_outputfile->good())
    {
        clog << "# warning: cannot write to file " << outfile << ". Monitoring disabled." << endl;
        delete m_outputfile;
        m_outputfile = 0;
        return ;
    }

    float msd = sys.GetConfig().getValue<float>("MonitorSampleDelay");
    msd = fabs(msd);
    m_tsdelay.tv_sec = msd;
    m_tsdelay.tv_nsec = (msd - (float)m_tsdelay.tv_sec) * 1000000000.;

    if (!m_quiet)
        clog << "# monitoring enabled, sampling "
                  << m_sampler->GetBufferSize()
                  << " bytes every "
                  << m_tsdelay.tv_sec << '.'
                  << setfill('0') << setw(9) << m_tsdelay.tv_nsec
                  << "s to file " << outfile << endl
                  << "# metadata output to file " << mdfile << endl;

    pthread(mutex_init, &m_runlock, 0);
    pthread(mutex_lock, &m_runlock);

    pthread(create, &m_monitorthread, 0, runmonitor, this);
}

Monitor::~Monitor()
{
    if (m_outputfile)
    {
        if (!m_quiet)
            clog << "# shutting down monitoring..." << endl;

        m_enabled = false;
        pthread(mutex_unlock, &m_runlock);
        pthread(join, m_monitorthread, 0);
        pthread(mutex_destroy, &m_runlock);

        m_outputfile->close();
        delete m_outputfile;
        delete m_sampler;
        if (!m_quiet)
            clog << "# monitoring ended." << endl;
    }
}

void Monitor::start()
{
    if (m_outputfile) {
        if (!m_quiet)
            clog << "# starting monitor..." << endl;
        m_running = true;
        pthread(mutex_unlock, &m_runlock);
    }
}

void Monitor::stop()
{
    if (m_running) {
        if (!m_quiet)
            clog << "# stopping monitor..." << endl;
        pthread(mutex_lock, &m_runlock);
        m_running = false;
    }
}

void Monitor::run()
{
    if (!m_quiet)
        clog << "# monitor thread started." << endl;

    const size_t datasz = m_sampler->GetBufferSize();
    const size_t allsz = datasz + 2 * sizeof(struct timeval);
    char *allbuf = new char[allsz];

    struct timeval *tv_begin = (struct timeval*)(void*)allbuf;
    struct timeval *tv_end = (struct timeval*)(void*)(allbuf + sizeof(struct timeval));
    char *databuf = allbuf + 2 * sizeof(struct timeval);

    Simulator::CycleNo lastCycle = 0;

    while (m_enabled)
    {
#if defined(HAVE_NANOSLEEP)
        nanosleep(&m_tsdelay, 0);
#elif defined(HAVE_USLEEP)
        usleep(m_tsdelay.tv_sec * 1000000 + m_tsdelay.tv_usec);
#else
#error No sub-microsecond wait available on this system.
#endif

        Simulator::CycleNo currentCycle = m_sys.GetKernel().GetCycleNo();
        if (currentCycle == lastCycle)
            // nothing to do
            continue;
        lastCycle = currentCycle;

        pthread(mutex_lock, &m_runlock);

        gettimeofday(tv_begin, 0);
        m_sampler->SampleToBuffer(databuf);
        gettimeofday(tv_end, 0);

        m_outputfile->write(allbuf, allsz);

        pthread(mutex_unlock, &m_runlock);
    }
}
