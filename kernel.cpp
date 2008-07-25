#include <cassert>
#include <algorithm>
#include <cstdarg>
#include <iostream>
#include <iomanip>
#include <set>
#include <map>
#include "kernel.h"
#include "profile.h"
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

const string Object::getFQN() const
{
    return (m_parent != NULL) ? m_parent->getFQN() + "." + getName() : getName();
}

void Object::OutputWrite(const char* msg, ...) const
{
    if (m_kernel->getCyclePhase() == PHASE_COMMIT)
    {
        va_list args;

        string name = getFQN();
        transform(name.begin(), name.end(), name.begin(), ::toupper);
        cout << "[" << setfill('0') << setw(8) << m_kernel->getCycleNo() << ":" << name << "] ";

        va_start(args, msg);
        vprintf(msg, args);
        va_end(args);
    }
}

void Object::DebugSimWrite(const char* msg, ...) const
{
    if ((m_kernel->debug() & Kernel::DEBUG_SIM) && m_kernel->getCyclePhase() == PHASE_COMMIT)
    {
        va_list args;

        string name = getFQN();
        transform(name.begin(), name.end(), name.begin(), ::toupper);
        cout << "[" << setfill('0') << setw(8) << m_kernel->getCycleNo() << ":" << name << "] ";

        va_start(args, msg);
        vprintf(msg, args);
        va_end(args);
    }
}

void Object::DebugProgWrite(const char* msg, ...) const
{
    if ((m_kernel->debug() & Kernel::DEBUG_PROG) && m_kernel->getCyclePhase() == PHASE_COMMIT)
    {
        va_list args;

        string name = getFQN();
        transform(name.begin(), name.end(), name.begin(), ::toupper);
        cout << "[" << setfill('0') << setw(8) << m_kernel->getCycleNo() << ":" << name << "] ";

        va_start(args, msg);
        vprintf(msg, args);
        va_end(args);
    }
}

//
// IStructure class
//
IStructure::IStructure(Object* parent, Kernel& kernel, const std::string& name) : Object(parent, &kernel, name)
{
    getKernel()->registerStructure(*this);
}

IStructure::~IStructure()
{
    getKernel()->unregisterStructure(*this);
}

//
// Component class
//
IComponent::IComponent(Object* parent, Kernel& kernel, const std::string& name, unsigned int numStates)
    : Object(parent, &kernel, name)
{
    getKernel()->registerComponent(*this, numStates);
}

IComponent::~IComponent()
{
    getKernel()->unregisterComponent(*this);
}

//
// Kernel class
//
RunState Kernel::step(CycleNo cycles)
{
	bool idle = true, has_work = false;

    for (CycleNo i = 0; cycles == INFINITE_CYCLES || i < cycles; ++i)
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
			for (unsigned int j = 0; j < i->second.nStates; ++j)
            {
                i->first->onCycleReadPhase(j);
            }
        }
        PROFILE_END("Read Acquire");

        PROFILE_BEGIN("Read Arbitrate");
        for (StructureList::iterator i = m_structures.begin(); i != m_structures.end(); ++i) (*i)->onArbitrateReadPhase();
        for (FunctionList ::iterator i = m_functions .begin(); i != m_functions .end(); ++i) (*i)->onArbitrateReadPhase();
        PROFILE_END("Read Arbitrate");

        PROFILE_BEGIN("Read Commit");
        for (CallbackList::iterator i = m_callbacks.begin(); i != m_callbacks.end(); ++i)
        {
			for (unsigned int j = 0; j < i->second.nStates; ++j)
            {
                m_phase = PHASE_CHECK;
				Result result;
				if ((result = i->first->onCycleReadPhase(j)) == SUCCESS)
                {
                    m_phase = PHASE_COMMIT;
                    result = i->first->onCycleReadPhase(j);
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
			for (unsigned int j = 0; j < i->second.nStates; ++j)
            {
                i->first->onCycleWritePhase(j);
            }
        }
        PROFILE_END("Write Acquire");

        PROFILE_BEGIN("Write Arbitrate");
        for (StructureList::iterator i = m_structures.begin(); i != m_structures.end(); ++i) (*i)->onArbitrateWritePhase();
        for (FunctionList ::iterator i = m_functions .begin(); i != m_functions .end(); ++i) (*i)->onArbitrateWritePhase();
        PROFILE_END("Write Arbitrate");

        PROFILE_BEGIN("Write Commit");
        for (CallbackList::iterator i = m_callbacks.begin(); i != m_callbacks.end(); ++i)
        {
			bool comp_idle     = true;
			bool comp_has_work = false;
			for (unsigned int j = 0; j < i->second.nStates; ++j)
            {
                m_phase = PHASE_CHECK;
				Result result;
				if ((result = i->first->onCycleWritePhase(j)) == SUCCESS)
                {
                    m_phase = PHASE_COMMIT;
                    result = i->first->onCycleWritePhase(j);
					assert(result == SUCCESS);
					idle = comp_idle = false;
                }
				else if (result == FAILED)
				{
					has_work = comp_has_work = true;
				}
            }
			i->second.state = (comp_idle) ? (comp_has_work) ? STATE_DEADLOCK : STATE_IDLE : STATE_RUNNING;
        }
        PROFILE_END("Write Commit");

        for (RegisterList::iterator i = m_registers.begin(); i != m_registers.end(); ++i) (*i)->onUpdate();

        // Check which structures are empty
        for (StructureList::iterator i = m_structures.begin(); i != m_structures.end(); ++i)
        {
            if (!(*i)->empty())
            {
                has_work = true;
            }
        }

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
	return (idle) ? (has_work) ? STATE_DEADLOCK : STATE_IDLE : STATE_RUNNING;
}

CycleNo Kernel::getCycleNo() const
{
    return m_cycle;
}

CyclePhase Kernel::getCyclePhase() const
{
    return m_phase;
}

void Kernel::debug(int mode) { m_debugMode = mode; }
int  Kernel::debug() const   { return m_debugMode; }

void Kernel::registerStructure(IStructure& _structure)
{
	m_structures.insert(&_structure);
}

void Kernel::registerComponent(IComponent& _component, unsigned int numStates)
{
    m_components.insert(&_component);
    if (numStates > 0)
    {
		CallbackInfo info;
		info.nStates = numStates;
		info.state   = STATE_IDLE;
        m_callbacks.insert(make_pair(&_component, info));
    }
}
void Kernel::registerFunction (IFunction&  _function )    { m_functions .insert(&_function ); }
void Kernel::registerRegister (IRegister&  _register )    { m_registers .insert(&_register ); }
void Kernel::unregisterStructure(IStructure& _structure)  { m_structures.erase(&_structure); }
void Kernel::unregisterComponent(IComponent& _component)  { m_components.erase(&_component); m_callbacks.erase(&_component); }
void Kernel::unregisterFunction (IFunction&  _function )  { m_functions .erase(&_function ); }
void Kernel::unregisterRegister (IRegister&  _register )  { m_registers .erase(&_register ); }

Kernel::Kernel()
{
	m_cycle     = 0;
	m_debugMode = 0;
	m_phase     = PHASE_COMMIT;
}

Kernel::~Kernel()
{
}
