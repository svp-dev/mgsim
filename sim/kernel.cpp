#include "kernel.h"
#include "storage.h"
#include "arch/dev/Display.h"
#include "sampling.h"

#include <cassert>
#include <algorithm>
#include <cstdarg>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <set>
#include <map>
#include <cstdio>

using namespace std;

namespace Simulator
{

//
// Process class
//

std::string Process::GetName() const
{
    return GetObject()->GetFQN() + ":" + m_name;
}

void Process::Deactivate()
{
    // A process can be sensitive to multiple objects, so we only remove it from the list
    // if the count becomes zero
    if (--m_activations == 0)
    {
        // Remove the handle node from the list
        *m_pPrev = m_next;
        if (m_next != NULL) {
            m_next->m_pPrev = m_pPrev;
        }
        m_state = STATE_IDLE;
    }
}

std::set<const Process*> Process::m_registry;

Process::Process(Object& parent, const string& name, const delegate& delegate)
    : m_name(name), m_delegate(delegate), m_state(STATE_IDLE), m_activations(0), m_stalls(0)
{
    m_registry.insert(this);
    RegisterSampleVariable(m_stalls, parent.GetFQN() + ':' + name + ":stalls", SVC_CUMULATIVE);
}

Process::~Process()
{
    m_registry.erase(this);
}

//
// Object class
//
Object::Object(const std::string& name, Clock& clock)
    : m_parent(NULL), m_name(name), m_fqn(name), m_clock(clock), m_kernel(clock.GetKernel())
{
}

Object::Object(const std::string& name, Object& parent)
    : m_parent(&parent), 
      m_name(name), 
      m_fqn(parent.GetFQN() + '.' + name),
      m_clock(parent.m_clock), 
      m_kernel(m_clock.GetKernel())
{
    // Add ourself to the parent's children array
    parent.m_children.push_back(this);
}

Object::Object(const std::string& name, Object& parent, Clock& clock)
    : m_parent(&parent), 
      m_name(name), 
      m_fqn(parent.GetFQN() + '.' + name),
      m_clock(clock), 
      m_kernel(clock.GetKernel())
{
    // Add ourself to the parent's children array
    parent.m_children.push_back(this);
}

Object::~Object()
{
    if (m_parent != NULL)
    {
        // Remove ourself from the parent's children array
        for (vector<Object*>::iterator p = m_parent->m_children.begin(); p != m_parent->m_children.end(); ++p)
        {
            if (*p == this)
            {
                m_parent->m_children.erase(p);
                break;
            }
        }
    }
}

void Object::OutputWrite_(const char* msg, ...) const
{
    va_list args;

    string name = GetFQN();
    cerr << "[" << right << dec << setfill('0') << setw(8) << m_kernel.GetCycleNo() << ":" << name << "] ";

    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
        
    cerr << endl;
}

void Object::DeadlockWrite_(const char* msg, ...) const
{
    va_list args;
        
    if (!m_kernel.m_debugging) {
        const Process* process = m_kernel.GetActiveProcess(); 
        cerr << endl << process->GetName() << ":" << endl;
        m_kernel.m_debugging = true;
    }

    string name = GetFQN();
    cerr << "[" << right << dec << setfill('0') << setw(8) << m_kernel.GetCycleNo() << ":" << name << "] ";

    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);

    cerr << endl;
}

void Object::DebugSimWrite_(const char* msg, ...) const
{
    va_list args;

    string name = GetFQN();
    cerr << "[" << right << dec << setfill('0') << setw(8) << m_kernel.GetCycleNo() << ":" << name << "] ";

    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);

    cerr << endl;
}

//
// Kernel class
//
void Kernel::Abort()
{
    m_aborted = true;
}

// Returns the Greatest Common Denominator of a and b.
static unsigned long long gcd(unsigned long long a, unsigned long long b)
{
    // Euclid's algorithm
    while (b != 0){
        unsigned long long t = b;
        b = a % b;
        a = t;
    }
    return a;
}

// Returns the Lowest Common Multiple of a and b.
unsigned long long lcm(unsigned long long a, unsigned long long b)
{
    return (a * b / gcd(a,b));
}

Clock& Kernel::CreateClock(unsigned long frequency)
{
    // We only allow creating clocks before the simulation starts
    assert(m_cycle == 0);

    // Gotta be at least 1 MHz
    assert(frequency > 0);
    frequency = std::max(1UL, frequency);

    /*
     Calculate new master frequency:
     If we have clocks of frequency [f_1, ..., f_n], we want a frequency
     that allows every cycle time to be an integer multiple of the master
     cycle time (e.g., a 300 MHz and 400 MHz clock would create a master
     clock of 1200 MHz, where the 300 MHz clock ticks once every 4 cycles
     and the 400 MHz clock ticks once every 3 cycles).
    
     This is simply equalizing fractions. e.g., 1/300 and 1/400 becomes
     4/1200 and 3/1200.
    
     Also, see if we already have this clock.
    */
    unsigned long long master_freq = 1;
    for (std::vector<Clock*>::const_iterator p = m_clocks.begin(); p != m_clocks.end(); ++p)
    {
        if ((*p)->m_frequency == frequency)
        {
            // We already have this clock, no need to calculate anything.
            return **p;
        }
        
        // Find Least Common Multiplier of master_freq and this clock's frequency.        
        master_freq = lcm(master_freq, (*p)->m_frequency);
    }
    master_freq = lcm(master_freq, frequency);
    
    if (m_master_freq != master_freq)
    {
        // The master frequency changed, update the clock periods
        m_master_freq = master_freq;
        for (std::vector<Clock*>::const_iterator p = m_clocks.begin(); p != m_clocks.end(); ++p)
        {
            assert(m_master_freq % (*p)->m_frequency == 0);
            (*p)->m_period = m_master_freq / (*p)->m_frequency;
        }
    }
    assert(m_master_freq % frequency == 0);
    
    m_clocks.push_back(new Clock(*this, frequency, m_master_freq / frequency));
    return *m_clocks.back();
}

RunState Kernel::Step(CycleNo cycles)
{
    try
    {
        // Time to simulate until
        const CycleNo endcycle = (cycles == INFINITE_CYCLES) ? cycles : m_cycle + cycles;
        
        if (m_cycle == 0)
        {
            // Update any changed storages.
            // This is just to effect the initialization writes,
            // in order to activate the initial processes.
            UpdateStorages();
        }
        
        // Advance time to the first clock to run.
        if (m_activeClocks != NULL)
        {
            assert(m_activeClocks->m_cycle >= m_cycle);
            m_cycle = m_activeClocks->m_cycle;
        }
        
        m_aborted = false;
        bool idle = false;
        while (!m_aborted && !idle && (endcycle == INFINITE_CYCLES || m_cycle < endcycle))
        {
            // We start each cycle being idle, and see if we did something this cycle
            idle = true;

            //
            // Acquire phase
            //
            m_phase = PHASE_ACQUIRE;
            for (Clock* clock = m_activeClocks; clock != NULL && m_cycle == clock->m_cycle; clock = clock->m_next)
            {
                for (Process* process = clock->m_activeProcesses; process != NULL; process = process->m_next)
                {
                    m_process   = process;
                    m_debugging = false; // Will be used by DeadlockWrite() for process-seperating newlines
                    
                    // This process begins the cycle
                    // This is a purely administrative function and has no simulation effect.
                    process->OnBeginCycle();
                            
                    // If we fail in the acquire stage, don't bother with the check and commit stages
                    Result result = process->m_delegate();
                    if (result == SUCCESS)
                    {
                        process->m_state = STATE_RUNNING;
                    }
                    else
                    {
                        assert(result == FAILED);
                        process->m_state = STATE_DEADLOCK;
                        ++process->m_stalls;
                    }
                }
            }
            
            //
            // Arbitrate phase
            //
            for (Clock* clock = m_activeClocks; clock != NULL && m_cycle == clock->m_cycle; clock = clock->m_next)
            {
                for (Arbitrator* arbitrator = clock->m_activeArbitrators; arbitrator != NULL; arbitrator = arbitrator->m_next)
                {
                    arbitrator->OnArbitrate();
                    arbitrator->m_activated = false;
                }
                clock->m_activeArbitrators = NULL;
            }
            
            //
            // Commit phase
            //
            for (Clock* clock = m_activeClocks; clock != NULL && m_cycle == clock->m_cycle; clock = clock->m_next)
            {
                for (Process* process = clock->m_activeProcesses; process != NULL; process = process->m_next)
                {
                    if (process->m_state != STATE_DEADLOCK)
                    {
                        m_process   = process;
                        m_phase     = PHASE_CHECK;
                        m_debugging = false; // Will be used by DeadlockWrite() for process-seperating newlines
                
                        Result result = process->m_delegate();
                        if (result == SUCCESS)
                        {
                            // This process is done this cycle.
                            // This is a purely administrative function and has no simulation effect.
                            // We call this before the COMMIT phase, so that if this produces an error,
                            // we can still inspect the state that caused it.
                            process->OnEndCycle();
                            
                            m_phase = PHASE_COMMIT;
                            result = process->m_delegate();
                            
                            // If the CHECK succeeded, the COMMIT cannot fail
                            assert(result == SUCCESS);
                            process->m_state = STATE_RUNNING;
                    
                            // We've done something -- we're not idle
                            idle = false;
                        }
                        else
                        {
                            // If a process has nothing to do (DELAYED) it shouldn't have been
                            // called in the first place.
                            assert(result == FAILED);
                            process->m_state = STATE_DEADLOCK;
                        }
                    }
                }
            }

            // Process the requested storage updates
            // This can activate or deactivate processes due to changes in storages
            // made by processes run in this cycle.
            if (UpdateStorages())
            {
                // We've update at least one storage
                idle = false;
            }
                
            if (idle)
            {
                // We haven't done anything this cycle. Check if there are clocks scheduled
                // for cycles in the future. If so, we want to still advance the simulation.
                for (Clock* clock = m_activeClocks; clock != NULL; clock = clock->m_next)
                {
                    if (clock->m_cycle > m_cycle)
                    {
                        idle = false;
                        break;
                    }
                }
            }
            
            if (Display::GetDisplay())
                Display::GetDisplay()->OnCycle(m_cycle);

            if (!idle)
            {
                // Advance the simulation
                
                // Update the clocks
                for (Clock *next, *clock = m_activeClocks; clock != NULL && m_cycle == clock->m_cycle; clock = next)
                {
                    next = clock->m_next;

                    // We ran this clock, remove it from the queue
                    m_activeClocks = clock->m_next;
                    clock->m_activated = false;

                    assert(clock->m_activeArbitrators == NULL);

                    if (clock->m_activeProcesses != NULL || clock->m_activeStorages != NULL)
                    {
                        // This clock still has active components, reschedule it
                        ActivateClock(*clock);
                    }
                }
                
                // Advance time to first clock to run
                if (m_activeClocks != NULL)
                {
                    assert(m_activeClocks->m_cycle > m_cycle);
                    m_cycle = m_activeClocks->m_cycle;
                }
            }
        }
        
        // In case we overshot the end with the last update
        m_cycle = std::min(m_cycle, endcycle);
        
        return (m_aborted)
            ? STATE_ABORTED
            : idle ? STATE_IDLE : STATE_RUNNING;
    }
    catch (SimulationException& e)
    {
        // Add information about what component/state we were executing
        stringstream details;
        details << "While executing process " << m_process->GetName() << endl
                << "At master cycle " << m_cycle << endl;
        e.AddDetails(details.str());
        throw;
    }
}

void Kernel::ActivateClock(Clock& clock)
{
    if (!clock.m_activated)
    {
        // Calculate new activation time for clock
        clock.m_cycle = (m_cycle / clock.m_period) * clock.m_period + clock.m_period;
                            
        // Insert clock into list based on activation time (earliest in front)
        Clock **before = &m_activeClocks, *after = m_activeClocks;
        while (after != NULL && after->m_cycle < clock.m_cycle)
        {
            before = &after->m_next;
            after  = after->m_next;
        }
        *before = &clock;
        clock.m_next = after;
        clock.m_activated = true;
    }
}

bool Kernel::UpdateStorages()
{
    bool updated = false;
    for (Clock* clock = m_activeClocks; clock != NULL && m_cycle == clock->m_cycle; clock = clock->m_next)
    {
        for (Storage *s = clock->m_activeStorages; s != NULL; s = s->m_next)
        {
            s->Update();
            s->m_activated = false;
            updated = true;
        }
        clock->m_activeStorages = NULL;
    }
    return updated;
}        

void Clock::ActivateProcess(Process& process)
{
    if (++process.m_activations == 1)
    {
        // First time this process has been activated, queue it
        process.m_next  = m_activeProcesses;
        process.m_pPrev = &m_activeProcesses;
        if (process.m_next != NULL) {
            process.m_next->m_pPrev = &process.m_next;
        }
        m_activeProcesses = &process;
        process.m_state = STATE_ACTIVE;

        m_kernel.ActivateClock(*this);
    }
}

void Kernel::SetDebugMode(int flags)
{
    m_debugMode = flags;
}

void Kernel::ToggleDebugMode(int flags)
{
    m_debugMode ^= flags;
}

Kernel::Kernel(SymbolTable& symtable, BreakPoints& breakpoints)
 : m_debugMode(0),
   m_cycle(0),
   m_symtable(symtable),
   m_breakpoints(breakpoints),
   m_phase(PHASE_COMMIT),
   m_master_freq(0),
   m_process(NULL),
   m_activeClocks(NULL)
{
    RegisterSampleVariable(m_cycle, "kernel.cycle", SVC_CUMULATIVE);
    RegisterSampleVariable(m_phase, "kernel.phase", SVC_STATE);
}

Kernel::~Kernel()
{
    for (size_t i = 0; i < m_clocks.size(); ++i)
    {
        delete m_clocks[i];
    }
}

}
