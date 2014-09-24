#include <sstream>
#include "demo/tinysim.h"
#include "sim/configparser.h"
#include "sim/except.h"
#include "sim/readfile.h"
#include "sim/unreachable.h"

MGSim::MGSim(const char* conf)
    : overrides(), extras(), cfg(0), k(0)
{
#ifdef STATIC_KERNEL
    Simulator::Kernel::InitGlobalKernel();
    k = &Simulator::Kernel::GetGlobalKernel();
#else
    k = new Simulator::Kernel();
#endif
    ConfigMap defaults;
    ConfigParser parser(defaults);
    parser(read_file(conf));
    cfg = new Config(defaults, overrides, extras);
}

void MGSim::DoSteps(Simulator::CycleNo nCycles)
{
    using namespace Simulator;

    RunState state = k->Step(nCycles);
    switch(state)
    {
    case STATE_ABORTED:
	// The simulation was aborted, because the user interrupted it.
	throw std::runtime_error("Interrupted!");

    case STATE_IDLE:
        // An idle state might actually be deadlock if there's a
        // suspended thread.  So check all cores to see if they're
        // really done.
        /*
          // REPLACE WITH YOUR OWN IDLE CHECK
        for (DRISC* p : m_procs)
            if (!p->IsIdle())
            {
                goto deadlock;
            }
        */

        // If all cores are done, but there are still some remaining
        // processes, and all the remaining processes are stalled,
        // then there is a deadlock too.  However since the kernel
        // state is idle, there cannot be any running process left. So
        // either there are no processes at all, or they are all
        // stalled. Deadlock only exists in the latter case, so
        // we only check for the existence of an active process.
        for (const Clock* clock = k->GetActiveClocks(); clock != NULL; clock = clock->GetNext())
        {
            if (clock->GetActiveProcesses() != NULL)
            {
                goto deadlock;
            }
        }

        break;

    case STATE_DEADLOCK:
    deadlock:
    {
        std::cerr << "Deadlock at cycle " << k->GetCycleNo()
                  << "; replaying the last cycle:" << std::endl;

        int savemode = k->GetDebugMode();
        k->SetDebugMode(-1);
        (void) k->Step(1);
        k->SetDebugMode(savemode);

        std::ostringstream ss;
        ss << "Stalled processes:" << std::endl;

        // See how many processes are in each of the states
        unsigned int num_stalled = 0, num_running = 0;

        for (const Clock* clock = k->GetActiveClocks(); clock != NULL; clock = clock->GetNext())
        {
            for (const Process* process = clock->GetActiveProcesses(); process != NULL; process = process->GetNext())
            {
                switch (process->GetState())
                {
                case STATE_DEADLOCK:
                    ss << "  " << process->GetName() << std::endl;
                    ++num_stalled;
                    break;
                case STATE_RUNNING:
                    ++num_running;
                    break;
                default:
                    UNREACHABLE;
                    break;
                }
            }
        }

        ss << std::endl
           << "Deadlock! (at cycle " << k->GetCycleNo() << ')' << std::endl
           << "(" << num_stalled << " processes stalled;  " << num_running << " processes running)";
        throw DeadlockException(ss.str());
        UNREACHABLE;
    }

    default:
        break;
    }

}
