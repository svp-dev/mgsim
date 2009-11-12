#include "kernel.h"
#include "storage.h"
#include "gfx.h"

#include <cassert>
#include <algorithm>
#include <cstdarg>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <set>
#include <map>
#include <cstdio>
#ifdef ENABLE_COMA
#include "coma/simlink/th.h"
#endif

using namespace std;

namespace Simulator
{

//
// Object class
//
Object::Object(Object* parent, Kernel* kernel, const std::string& name)
    : m_parent(parent), m_name(name), m_kernel(kernel)
{
    if (m_parent != NULL)
    {
        // Add ourself to the parent's children array
        m_parent->m_children.push_back(this);
    }
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

void Object::OutputWrite(const char* msg, ...) const
{
    if (m_kernel->GetCyclePhase() == PHASE_COMMIT)
    {
        va_list args;

        string name = GetFQN();
        transform(name.begin(), name.end(), name.begin(), ::toupper);
        cout << "[" << right << dec << setfill('0') << setw(8) << m_kernel->GetCycleNo() << ":" << name << "] ";

        va_start(args, msg);
        vprintf(msg, args);
        va_end(args);
        
        cout << endl;
    }
}

void Object::DeadlockWrite(const char* msg, ...) const
{
    if (m_kernel->GetDebugMode() & Kernel::DEBUG_DEADLOCK)
    {
        va_list args;
        
        if (!m_kernel->m_debugging) {
            const Kernel::ProcessInfo* process = m_kernel->GetActiveProcess(); 
            cout << endl << process->info->component->GetFQN() << ":" << process->name << ":" << endl;
            m_kernel->m_debugging = true;
        }

        string name = GetFQN();
        transform(name.begin(), name.end(), name.begin(), ::toupper);
        cout << "[" << right << dec << setfill('0') << setw(8) << m_kernel->GetCycleNo() << ":" << name << "] ";

        va_start(args, msg);
        vprintf(msg, args);
        va_end(args);

        cout << endl;
    }
}

void Object::DebugSimWrite(const char* msg, ...) const
{
    if ((m_kernel->GetDebugMode() & Kernel::DEBUG_SIM) && m_kernel->GetCyclePhase() == PHASE_COMMIT)
    {
        va_list args;

        string name = GetFQN();
        transform(name.begin(), name.end(), name.begin(), ::toupper);
        cout << "[" << right << dec << setfill('0') << setw(8) << m_kernel->GetCycleNo() << ":" << name << "] ";

        va_start(args, msg);
        vprintf(msg, args);
        va_end(args);

        cout << endl;
    }
}

void Object::DebugProgWrite(const char* msg, ...) const
{
    if ((m_kernel->GetDebugMode() & Kernel::DEBUG_PROG) && m_kernel->GetCyclePhase() == PHASE_COMMIT)
    {
        va_list args;

        string name = GetFQN();
        transform(name.begin(), name.end(), name.begin(), ::toupper);
        cout << "[" << right << dec << setfill('0') << setw(8) << m_kernel->GetCycleNo() << ":" << name << "] ";

        va_start(args, msg);
        vprintf(msg, args);
        va_end(args);

        cout << endl;
    }
}

//
// Component class
//
IComponent::IComponent(Object* parent, Kernel& kernel, const std::string& name, const std::string& states)
    : Object(parent, &kernel, name)
{
    GetKernel()->RegisterComponent(*this, states);
}

IComponent::~IComponent()
{
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
#ifdef ENABLE_COMA
            sem_post(&thpara.sem_sync);
            sem_wait(&thpara.sem_mgs);
#endif
            //
            // Acquire phase
            //
            m_phase = PHASE_ACQUIRE;
            for (ProcessInfo* process = m_activeProcesses; process != NULL; process = process->next)
            {
                m_process   = process;
                m_debugging = false; // Will be used by DeadlockWrite() for process-seperating newlines
                
                // If we fail in the acquire stage, don't bother with the check and commit stages
                process->state = (process->info->component->OnCycle(process - &process->info->processes[0]) == FAILED)
                    ? STATE_DEADLOCK
                    : STATE_RUNNING;
            }
            
            //
            // Arbitrate phase
            //
            for (ArbitratorInfo* arbitrator = m_activeArbitrators; arbitrator != NULL; arbitrator = arbitrator->next)
            {
                arbitrator->arbitrator->OnArbitrate();
                arbitrator->activated = false;
            }
            m_activeArbitrators = NULL;

            //
            // Commit phase
            //
            idle = true;
            for (ProcessInfo* process = m_activeProcesses; process != NULL; process = process->next)
            {
                if (process->state != STATE_DEADLOCK)
                {
                    m_process   = process;
                    m_phase     = PHASE_CHECK;
                    m_debugging = false; // Will be used by DeadlockWrite() for process-seperating newlines
                
                    Result result;
                    const int index = process - &process->info->processes[0];
                    if ((result = process->info->component->OnCycle(index)) == SUCCESS)
                    {
                        m_phase = PHASE_COMMIT;
                        result = process->info->component->OnCycle(index);
                            
                        // If the CHECK succeeded, the COMMIT cannot fail
 					    assert(result == SUCCESS);
                        process->state = STATE_RUNNING;
                    
                        // We've done something -- we're not idle
                        idle = false;
                    }
                    else
                    {
                        // If a process has nothing to do (DELAYED) it shouldn't have been
                        // called in the first place.
                        assert(result == FAILED);
                        process->state = STATE_DEADLOCK;
                    }
			    }
            }
            
            // Update statistics
            for (ComponentList::const_iterator i = m_components.begin(); i != m_components.end(); ++i)
            {
                i->component->UpdateStatistics();
            }      

            // Process the requested storage updates
            UpdateStorages();
            
            // Check if, after updating the storages, we have processes to run
            has_work = (m_activeProcesses != NULL);
            
            if (!idle)
            {
                // We did something this cycle
                ++m_cycle;
            }

	    assert(m_display != NULL);
	    m_display->cycle();
        }
        
        return (m_aborted)
            ? STATE_ABORTED
	        : idle ? has_work ? STATE_DEADLOCK : STATE_IDLE : STATE_RUNNING;
    }
    catch (SimulationException& e)
    {
        // Add information about what component/state we were executing
        stringstream details;
        details << "While executing process " << m_process->info->component->GetFQN() << ":" << m_process->name << endl;
        e.AddDetails(details.str());
        throw;
    }
}

  void Kernel::setDisplay(GfxDisplay* p)
  {
    assert(m_display == NULL);
    m_display = p;
  }

void Kernel::UpdateStorages()
{
    for (StorageInfo *s = m_activeStorages; s != NULL; s = s->next)
    {
        s->storage->Update();
        s->activated = false;
    }
    m_activeStorages = NULL;
}        

void Kernel::ActivateProcess(ProcessInfo* process)
{
    if (++process->activations == 1)
    {
        // First time this process has been activated, queue it
        process->next  = m_activeProcesses;
        process->pPrev = &m_activeProcesses;
        if (process->next != NULL) {
            process->next->pPrev = &process->next;
        }
        m_activeProcesses = process;
    }
}

void Kernel::RegisterStorage(Storage& storage)
{
    m_storages.push_back(StorageInfo());
    StorageInfo& info = m_storages.back();
    info.storage = &storage;
    info.activated = false;
}

Kernel::ProcessInfo* Kernel::GetProcessInfo(IComponent* component, int state)
{
    // We don't care that this is O(n) -- it happens during initialization only
    for (ComponentList::iterator p = m_components.begin(); p != m_components.end(); ++p)
    {
        if (p->component == component)
        {
            return &p->processes[state];
        }
    }
    return NULL;
}

void Kernel::RegisterComponent(IComponent& component, const std::string& states)
{
    m_components.push_back(ComponentInfo());
    ComponentInfo& info = m_components.back();
    info.component = &component;
    
    // Split up the state string into states
    for (string::const_iterator cur = states.begin(); cur != states.end(); )
 	{
       	string::const_iterator delim = find(cur, states.end(), '|');

        ProcessInfo s;
        s.name        = string(cur, delim);
        s.state       = STATE_IDLE;
        s.activations = 0;
   		info.processes.push_back(s);
  		cur = (delim != states.end()) ? delim + 1 : delim;
    }
}

void Kernel::RegisterArbitrator(Arbitrator& arbitrator)
{
    m_arbitrators.push_back(ArbitratorInfo());
    ArbitratorInfo& info = m_arbitrators.back();
    info.arbitrator = &arbitrator;
    info.activated = false;
}

void Kernel::SetDebugMode(int flags)
{
    m_debugMode = flags;
}

void Kernel::ToggleDebugMode(int flags)
{
    m_debugMode ^= flags;
}

/// Called after everything has been created
void Kernel::Initialize()
{
    // Set all backlinks from ProcessInfo to ComponentInfo
    for (size_t i = 0; i < m_components.size(); ++i)
    {
        ComponentInfo* info = &m_components[i];
        for (size_t j = 0; j < info->processes.size(); ++j)
        {
            info->processes[j].info = info;
        }
    }
    
    // Tell all storages what handle they have
    for (size_t i = 0; i < m_storages.size(); ++i)
    {
        m_storages[i].storage->Initialize(&m_storages[i]);
    }

    // Tell all arbitrators what handle they have
    for (size_t i = 0; i < m_arbitrators.size(); ++i)
    {
        m_arbitrators[i].arbitrator->Initialize(&m_arbitrators[i]);
    }
}

Kernel::Kernel()
 : m_debugMode(0),
   m_cycle(0),
   m_phase(PHASE_COMMIT),
   m_process(NULL),
   m_activeProcesses(NULL),
   m_activeStorages(NULL),
   m_activeArbitrators(NULL),
   m_display(NULL)
{
}

Kernel::~Kernel()
{
  if (m_display)
    delete m_display;
}

}
