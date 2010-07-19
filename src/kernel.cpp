#include "kernel.h"
#include "storage.h"
#include "display.h"
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
#ifdef ENABLE_COMA_ZL
#include "coma/simlink/th.h"
#endif

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

//
// Object class
//
Object::Object(const std::string& name, Kernel& kernel)
    : m_parent(NULL), m_name(name), m_kernel(kernel)
{
}

Object::Object(const std::string& name, Object& parent)
    : m_parent(&parent), m_name(name), m_kernel(parent.m_kernel)
{
    // Add ourself to the parent's children array
    parent.m_children.push_back(this);
}

Object::Object(const std::string& name, Object& parent, Kernel& kernel)
    : m_parent(&parent), m_name(name), m_kernel(kernel)
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

const string Object::GetFQN() const
{
    return (m_parent != NULL) ? m_parent->GetFQN() + "." + GetName() : GetName();
}

void Object::OutputWrite_(const char* msg, ...) const
{
    va_list args;

    string name = GetFQN();
    transform(name.begin(), name.end(), name.begin(), ::toupper);
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
    transform(name.begin(), name.end(), name.begin(), ::toupper);
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
    transform(name.begin(), name.end(), name.begin(), ::toupper);
    cerr << "[" << right << dec << setfill('0') << setw(8) << m_kernel.GetCycleNo() << ":" << name << "] ";

    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);

    cerr << endl;
}

void Object::DebugProgWrite_(const char* msg, ...) const
{
    va_list args;

    string name = GetFQN();
    transform(name.begin(), name.end(), name.begin(), ::toupper);
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

RunState Kernel::Step(CycleNo cycles)
{
    try
    {
        bool idle     = false;
        bool has_work = false;
        
        // Update any storages changed since the last run.
        // Practically this will just be to effect the initialization writes,
        // to activate the initial processes.
        UpdateStorages();
        
        m_aborted = false;
        for (CycleNo i = 0; !m_aborted && !idle && (cycles == INFINITE_CYCLES || i < cycles); ++i)
        {
#ifdef ENABLE_COMA_ZL
            sem_post(&thpara.sem_sync);
            sem_wait(&thpara.sem_mgs);
#endif
            //
            // Acquire phase
            //
            m_phase = PHASE_ACQUIRE;
            for (Process* process = m_activeProcesses; process != NULL; process = process->m_next)
            {
                m_process   = process;
                m_debugging = false; // Will be used by DeadlockWrite() for process-seperating newlines
                
                // If we fail in the acquire stage, don't bother with the check and commit stages
                process->m_state = (process->m_delegate() == FAILED)
                    ? STATE_DEADLOCK
                    : STATE_RUNNING;
            }
            
            //
            // Arbitrate phase
            //
            for (Arbitrator* arbitrator = m_activeArbitrators; arbitrator != NULL; arbitrator = arbitrator->m_next)
            {
                arbitrator->OnArbitrate();
                arbitrator->m_activated = false;
            }
            m_activeArbitrators = NULL;

            //
            // Commit phase
            //
            idle = true;
            for (Process* process = m_activeProcesses; process != NULL; process = process->m_next)
            {
                if (process->m_state != STATE_DEADLOCK)
                {
                    m_process   = process;
                    m_phase     = PHASE_CHECK;
                    m_debugging = false; // Will be used by DeadlockWrite() for process-seperating newlines
                
                    Result result;
                    if ((result = process->m_delegate()) == SUCCESS)
                    {
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
            
            // Process the requested storage updates
            if (UpdateStorages())
            {
                // Check if, after updating the storages, we have processes to run
                if (m_activeProcesses != NULL)
                {
                    idle = false;
                }
            }
            
            if (!idle)
            {
                // We did something this cycle
                ++m_cycle;
            }

#ifdef CHECK_DISPLAY_EVENTS
            m_display.OnCycle(m_cycle);
#endif
        }
        
        return (m_aborted)
            ? STATE_ABORTED
            : idle ? has_work ? STATE_DEADLOCK : STATE_IDLE : STATE_RUNNING;
    }
    catch (SimulationException& e)
    {
        // Add information about what component/state we were executing
        stringstream details;
        details << "While executing process " << m_process->GetName() << endl;
        e.AddDetails(details.str());
        throw;
    }
}

bool Kernel::UpdateStorages()
{
    bool updated = (m_activeStorages != NULL);
    for (Storage *s = m_activeStorages; s != NULL; s = s->m_next)
    {
        s->Update();
        s->m_activated = false;
    }
    m_activeStorages = NULL;
    return updated;
}        

void Kernel::ActivateProcess(Process& process)
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

Kernel::Kernel(Display& display, SymbolTable& symtable, BreakPoints& breakpoints)
 : m_debugMode(0),
   m_cycle(0),
   m_display(display),
   m_symtable(symtable),
   m_breakpoints(breakpoints),
   m_phase(PHASE_COMMIT),
   m_process(NULL),
   m_activeProcesses(NULL),
   m_activeStorages(NULL),
   m_activeArbitrators(NULL)
{
    RegisterSampleVariable(m_cycle, "kernel.cycle", SVC_CUMULATIVE);
    RegisterSampleVariable(m_phase, "kernel.phase", SVC_STATE);
}

Kernel::~Kernel()
{
}

}
