#include "kernel.h"
#include "profile.h"
#include <cassert>
#include <algorithm>
#include <cstdarg>
#include <iostream>
#include <iomanip>
#include <set>
#include <map>
using namespace Simulator;
using namespace std;


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
        for (vector<Object*>::iterator p = m_parent->m_children.begin(); p != m_parent->m_children.end(); p++)
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
        cout << "[" << dec << setfill('0') << setw(8) << m_kernel->GetCycleNo() << ":" << name << "] ";

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

        string name = GetFQN();
        transform(name.begin(), name.end(), name.begin(), ::toupper);
        cout << "[" << dec << setfill('0') << setw(8) << m_kernel->GetCycleNo() << ":" << name << "] ";

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
        cout << "[" << dec << setfill('0') << setw(8) << m_kernel->GetCycleNo() << ":" << name << "] ";

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
        cout << "[" << dec << setfill('0') << setw(8) << m_kernel->GetCycleNo() << ":" << name << "] ";

        va_start(args, msg);
        vprintf(msg, args);
        va_end(args);

        cout << endl;
    }
}

//
// IStructure class
//
IStructure::IStructure(Object* parent, Kernel& kernel, const std::string& name) : Object(parent, &kernel, name)
{
    GetKernel()->RegisterStructure(*this);
}

IStructure::~IStructure()
{
    GetKernel()->UnregisterStructure(*this);
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
    GetKernel()->UnregisterComponent(*this);
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
	bool idle = true, has_work = false;
	
	m_aborted = false;
    for (CycleNo i = 0; (cycles == INFINITE_CYCLES || i < cycles) && !m_aborted; ++i)
    {
  		idle     = true;
   		has_work = false;
        
       	//
        // Read phase
        //
        m_phase = PHASE_ACQUIRE;
        PROFILE_BEGIN("Read Acquire");
        for (CallbackList::iterator i = m_callbacks.begin(); i != m_callbacks.end(); ++i)
        {
 			for (size_t j = 0; j < i->second.states.size(); ++j)
            {
                i->first->OnCycleReadPhase(j);
            }
        }
        PROFILE_END("Read Acquire");

        PROFILE_BEGIN("Read Arbitrate");
        for (StructureList::iterator i = m_structures.begin(); i != m_structures.end(); ++i) (*i)->OnArbitrateReadPhase();
        for (FunctionList ::iterator i = m_functions .begin(); i != m_functions .end(); ++i) (*i)->OnArbitrateReadPhase();
        PROFILE_END("Read Arbitrate");

        PROFILE_BEGIN("Read Commit");
        for (CallbackList::iterator i = m_callbacks.begin(); i != m_callbacks.end(); ++i)
        {
 			for (size_t j = 0; j < i->second.states.size(); ++j)
            {
                m_phase = PHASE_CHECK;
  				Result result;
  				if ((result = i->first->OnCycleReadPhase(j)) == SUCCESS)
                {
                    m_phase = PHASE_COMMIT;
                    result = i->first->OnCycleReadPhase(j);
  					assert(result == SUCCESS);
                }
            }
        }
        PROFILE_END("Read Commit");

        //
        // Write phase
        //
        m_phase = PHASE_ACQUIRE;
        PROFILE_BEGIN("Write Acquire");
        for (CallbackList::iterator i = m_callbacks.begin(); i != m_callbacks.end(); ++i)
        {
 			for (size_t j = 0; j < i->second.states.size(); ++j)
            {
                Result result = i->first->OnCycleWritePhase(j);
                
                i->second.states[j].state = (result == FAILED) ? STATE_DEADLOCK : STATE_RUNNING;
            }
        }
        PROFILE_END("Write Acquire");

        PROFILE_BEGIN("Write Arbitrate");
        for (StructureList::iterator i = m_structures.begin(); i != m_structures.end(); ++i) (*i)->OnArbitrateWritePhase();
        for (FunctionList ::iterator i = m_functions .begin(); i != m_functions .end(); ++i) (*i)->OnArbitrateWritePhase();
        PROFILE_END("Write Arbitrate");

        PROFILE_BEGIN("Write Commit");
        for (CallbackList::iterator i = m_callbacks.begin(); i != m_callbacks.end(); ++i)
        {
  			for (size_t j = 0; j < i->second.states.size(); ++j)
            {
                RunState& state = i->second.states[j].state;
                if (state != STATE_DEADLOCK)
                {
                    m_phase = PHASE_CHECK;
     				Result result;
      				if ((result = i->first->OnCycleWritePhase(j)) == SUCCESS)
                    {
                        m_phase = PHASE_COMMIT;
                        result = i->first->OnCycleWritePhase(j);
     					assert(result == SUCCESS);
      					idle  = false;
      					state = STATE_RUNNING;
                    }
      				else if (result == FAILED)
       				{
       				    state    = STATE_DEADLOCK;
       					has_work = true;
       				}
   	    			else
   		    		{
   			    	    state = STATE_IDLE;
   				    }
                }
                else 
                {
                    has_work = true;
                }
            }
        }
        PROFILE_END("Write Commit");
    
        for (RegisterList::iterator i = m_registers.begin(); i != m_registers.end(); ++i) (*i)->OnUpdate();
        m_phase = PHASE_COMMIT;
        for (CallbackList::iterator i = m_callbacks.begin(); i != m_callbacks.end(); ++i)
        {
            i->first->UpdateStatistics();
        }
        
        m_cycle++;
        if (cycles == INFINITE_CYCLES && idle)
        {
            break;
        }
    }
	return (m_aborted)
	    ? STATE_ABORTED
	    : (idle) ? (has_work) ? STATE_DEADLOCK : STATE_IDLE : STATE_RUNNING;
}

CycleNo Kernel::GetCycleNo() const
{
    return m_cycle;
}

CyclePhase Kernel::GetCyclePhase() const
{
    return m_phase;
}

void Kernel::SetDebugMode(int mode) { m_debugMode = mode; }
int  Kernel::GetDebugMode() const   { return m_debugMode; }

void Kernel::RegisterStructure(IStructure& _structure)
{
	m_structures.insert(&_structure);
}

void Kernel::RegisterComponent(IComponent& _component, const std::string& states)
{
    m_components.insert(&_component);
    if (!states.empty())
    {
        CallbackInfo info;
    
	    string::const_iterator cur = states.begin();
        while (cur != states.end())
    	{
        	string::const_iterator delim = find(cur, states.end(), '|');

            ComponentState s;
            s.name  = string(cur, delim);
            s.state = STATE_IDLE;
      		info.states.push_back(s);
      		cur = (delim != states.end()) ? delim + 1 : delim;
        }
        m_callbacks.insert(make_pair(&_component, info));
    }
}

void Kernel::RegisterFunction (IFunction&  _function )    { m_functions .insert(&_function ); }
void Kernel::RegisterRegister (IRegister&  _register )    { m_registers .insert(&_register ); }
void Kernel::UnregisterStructure(IStructure& _structure)  { m_structures.erase(&_structure); }
void Kernel::UnregisterComponent(IComponent& _component)  { m_components.erase(&_component); m_callbacks.erase(&_component); }
void Kernel::UnregisterFunction (IFunction&  _function )  { m_functions .erase(&_function ); }
void Kernel::UnregisterRegister (IRegister&  _register )  { m_registers .erase(&_register ); }

Kernel::Kernel()
{
	m_cycle     = 0;
	m_debugMode = 0;
	m_phase     = PHASE_COMMIT;
}

Kernel::~Kernel()
{
}
