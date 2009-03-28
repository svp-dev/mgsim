#include "FPU.h"
#include "RegisterFile.h"
#include "Processor.h"
#include <cassert>
#include <cmath>
using namespace std;

namespace Simulator
{

bool FPU::QueueOperation(FPUOperation op, int size, double Rav, double Rbv, const RegAddr& Rc)
{
	CycleNo	latency = 0;
	double  value;
	Result  res;
	
	// The size must be a multiple of the arch's native integer size
	assert(size > 0);
	assert(size % sizeof(Integer) == 0);

    // The result is actually calculated at the queueing. The rest is just a delay
    // to simulate the supposed calculation.	
	switch (op)
	{
	    case FPU_OP_SQRT:
	        value = sqrt( Rbv );
	        latency = m_config.sqrtLatency;
	        break;
	            
	    case FPU_OP_ADD: 
			value = Rav + Rbv;
			latency = m_config.addLatency;
			break;
				
        case FPU_OP_SUB:
            value = Rav - Rbv;
			latency = m_config.subLatency;
			break;
			
        case FPU_OP_MUL:
            value = Rav * Rbv;
			latency = m_config.mulLatency;
			break;
			
        case FPU_OP_DIV:
			value = Rav / Rbv;
			latency = m_config.divLatency;
			break;

		default:
		    assert(0);
		    return false;
	}
	
	assert(latency > 0);

 	res.address    = Rc;
 	res.size       = size;
 	res.completion = GetKernel()->GetCycleNo() + latency;
 	res.value.fromfloat(value, size);

    if (!m_pipelines[latency].empty() && m_pipelines[latency].front().completion == res.completion)
   	{
   		// The pipeline is full (because of a stall)
   		return false;
    }

   	COMMIT{ m_pipelines[latency].push_back(res); }
	return true;
}

bool FPU::OnCompletion(const Result& res) const
{
    if (!m_registerFile.p_asyncW.Write(*this, res.address))
    {
    	return false;
    }

    RegValue value;
    if (!m_registerFile.ReadRegister(res.address, value))
    {
	    return false;
    }

    if (value.m_state != RST_PENDING && value.m_state != RST_WAITING)
    {
    	// We're too fast, wait!
    	return false;
    }
    value.m_state = RST_FULL;
    
    uint64_t data = res.value.toint(res.size);
    unsigned int nRegs = res.size / sizeof(Integer);
    for (unsigned int i = 0; i < nRegs; i++)
    {
        RegAddr a = res.address;
#if ARCH_ENDIANNESS == ARCH_BIG_ENDIAN
        // LSB goes in last register
        a.index += (nRegs - 1 - i);
#else
        // LSB goes in first register
        a.index += i;
#endif

	    value.m_float.integer = (Integer)data;
	    if (!m_registerFile.WriteRegister(a, value, *this))
	    {
	    	return false;
	    }

        // We do this in two steps; otherwise the compiler could complain
        // about shifting the whole data size.
        data >>= sizeof(Integer) * 4;
        data >>= sizeof(Integer) * 4;
	}
	return true;
}

Result FPU::OnCycleWritePhase(unsigned int /* stateIndex */)
{
	CycleNo now = GetKernel()->GetCycleNo();
	for (map<CycleNo, deque<Result> >::iterator p = m_pipelines.begin(); p != m_pipelines.end(); p++)
	{
	    if (!p->second.empty())
	    {
    		Result& res = p->second.front();
		    if (res.completion <= now)
    		{
	    		// Write back result
		    	if (!OnCompletion(res))
			    {
    				// Stall pipeline
    				COMMIT
    				{
	    				for (deque<Result>::iterator q = p->second.begin(); q != p->second.end(); q++)
		    			{
			    			q->completion++;
				    	}
    				}
    				return FAILED;
	    		}

		    	COMMIT
    			{
    				// Remove from queue
    				p->second.pop_front();
    				if (p->second.empty())
    				{
    					m_pipelines.erase(p);
    				}
    			}
    			return SUCCESS;
    		}
    	}
    }
    return m_pipelines.empty() ? DELAYED : SUCCESS;
}

FPU::FPU(Processor& parent, const std::string& name, RegisterFile& regFile, const Config& config)
	: IComponent(&parent, parent.GetKernel(), name), m_registerFile(regFile), m_config(config)
{
}

}
