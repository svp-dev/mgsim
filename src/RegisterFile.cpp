#include "RegisterFile.h"
#include "Processor.h"
#include <cassert>
using namespace Simulator;
using namespace std;

//
// RegisterFile implementation
//

RegisterFile::RegisterFile(Processor& parent, Allocator& alloc, const Config& config)
:   Structure<RegAddr>(&parent, parent.GetKernel(), "registers"),
    p_pipelineR1(*this), p_pipelineR2(*this), p_pipelineW(*this), p_asyncR(*this), p_asyncW(*this),
    m_integers(config.numIntegers),
    m_floats(config.numFloats),
    m_parent(parent),
    m_allocator(alloc)
{
    // Initialize all registers to empty
    for (RegSize i = 0; i < config.numIntegers; ++i)
    {
        m_integers[i].m_state        = RST_EMPTY;
		m_integers[i].m_request.size = 0;
    }

    for (RegSize i = 0; i < config.numFloats; ++i)
    {
        m_floats[i].m_state        = RST_EMPTY;
		m_floats[i].m_request.size = 0;
    }

    // Set port priorities
    SetPriority(p_asyncW,    1);
    SetPriority(p_pipelineW, 0);
}

RegSize RegisterFile::GetSize(RegType type) const
{
    const vector<RegValue>& regs = (type == RT_FLOAT) ? m_floats : m_integers;
    return regs.size();
}

bool RegisterFile::ReadRegister(const RegAddr& addr, RegValue& data) const
{
    const vector<RegValue>& regs = (addr.type == RT_FLOAT) ? m_floats : m_integers;
    if (addr.index >= regs.size())
    {
        throw SimulationException(*this, "A component attempted to read from a non-existing register");
    }
    data = regs[addr.index];
    return true;
}

// Admin version
bool RegisterFile::WriteRegister(const RegAddr& addr, const RegValue& data)
{
    vector<RegValue>& regs = (addr.type == RT_FLOAT) ? m_floats : m_integers;
    if (addr.index < regs.size())
    {
	    regs[addr.index] = data;
	    return true;
	}
	return false;
}

bool RegisterFile::Clear(const RegAddr& addr, RegSize size, const RegValue& value)
{
    std::vector<RegValue>& regs = (addr.type == RT_FLOAT) ? m_floats : m_integers;
    if (addr.index + size > regs.size())
    {
        throw SimulationException(*this, "A component attempted to clear a non-existing register");
    }

    for (RegSize i = 0; i < size; ++i)
    {
        COMMIT
		{
			regs[addr.index + i] = value;
		}
    }

    return true;
}

bool RegisterFile::WriteRegister(const RegAddr& addr, RegValue& data, const IComponent& component)
{
	std::vector<RegValue>& regs = (addr.type == RT_FLOAT) ? m_floats : m_integers;
    if (addr.index >= regs.size())
    {
        throw SimulationException(component, "A component attempted to write to a non-existing register");
    }
    
	assert(data.m_state == RST_EMPTY || data.m_state == RST_PENDING || data.m_state == RST_WAITING || data.m_state == RST_FULL);

    RegValue& value = regs[addr.index];
    if (data.m_state == RST_WAITING)
    {
		// Must come from the pipeline (i.e., an instruction read a non-full register)
		if (value.m_state != RST_PENDING && value.m_state != RST_FULL)
		{
			throw SimulationException(component, "Waiting on a non-pending register");
		}

    	if (value.m_state == RST_FULL)
    	{
    		// The data we wanted to wait for has returned before we could write the register.
    		// Write back the state as FULL, the pipeline will reschedule the thread instead of
    		// suspending it
    		data.m_state = RST_FULL;
    	}
	    else
		{
            COMMIT
            {
			    // Just copy the TID because we need to preserve the m_request member
				value.m_tid   = data.m_tid;
				value.m_state = RST_WAITING;
			}
		}
	}
	else
	{
		if (value.m_state == RST_PENDING)
		{
			if (data.m_state != RST_FULL)
			{
				throw SimulationException(component, "Writing to a pending register");
			}

			if (value.m_component != &component)
			{
				throw SimulationException(component, "Invalid component is overwriting a pending register");
			}
		}
		else if (value.m_state == RST_WAITING)
        {
            assert (data.m_state == RST_FULL);

			// This write caused a reschedule
            if (!m_allocator.ActivateThread(value.m_tid, component))
            {
                DeadlockWrite("Unable to wake up thread T%u from %s", value.m_tid, addr.str().c_str());
                return false;
            }
        }

        COMMIT{ value = data; }
    }
    return true;
}
