#include "FPU.h"
#include "RegisterFile.h"
#include "config.h"
#include <cassert>
#include <cmath>
using namespace std;

namespace Simulator
{

static const char* const OperationNames[FPU_NUM_OPS] = {
    "ADD", "SUB", "MUL", "DIV", "SQRT"
};
    
bool FPU::QueueOperation(FPUOperation fop, int size, double Rav, double Rbv, RegisterFile& regfile, const RegAddr& Rc)
{
	// The size must be a multiple of the arch's native integer size
	assert(size > 0);
	assert(size % sizeof(Integer) == 0);
	assert(Rc.valid());

    Operation op;
    op.op   = fop;
    op.size = size;
    op.Rav  = Rav;
    op.Rbv  = Rbv;
    op.Rc   = Rc;
    
    QueueMap::iterator p = m_queues.insert(make_pair(&regfile, Buffer<Operation>(*GetKernel(), m_queueSize))).first;
    if (!p->second.push(op))
    {
        return false;
    }
    
    DebugSimWrite("Queued FP %s operation into queue %p", OperationNames[fop], &regfile);
    return true;
}

FPU::Result FPU::CalculateResult(const Operation& op) const
{
	double value;
	switch (op.op)
	{
	case FPU_OP_SQRT: value = sqrt( op.Rbv ); break;
    case FPU_OP_ADD:  value = op.Rav + op.Rbv; break;
    case FPU_OP_SUB:  value = op.Rav - op.Rbv; break;
    case FPU_OP_MUL:  value = op.Rav * op.Rbv; break;
    case FPU_OP_DIV:  value = op.Rav / op.Rbv; break;
	default:	      value = 0.0; assert(0); break;
	}
	
	Result  res;
 	res.address = op.Rc;
 	res.size    = op.size;
 	res.state   = 1;
 	res.value.fromfloat(value, op.size);

	return res;
}

bool FPU::OnCompletion(const Result& res) const
{
    if (!res.regfile->p_asyncW.Write(res.address))
    {
        DeadlockWrite("Unable to acquire port to write back to %s", res.address.str().c_str());
    	return false;
    }

    RegValue value;
    if (!res.regfile->ReadRegister(res.address, value))
    {
        DeadlockWrite("Unable to read register %s", res.address.str().c_str());
	    return false;
    }

    if (value.m_state != RST_EMPTY && value.m_state != RST_WAITING)
    {
    	// We're too fast, wait!
    	DeadlockWrite("FP operation completed before register %s was cleared", res.address.str().c_str());
    	return false;
    }
    value.m_state = RST_FULL;
    
    uint64_t data = res.value.toint(res.size);
    unsigned int nRegs = res.size / sizeof(Integer);
    for (unsigned int i = 0; i < nRegs; ++i)
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
	    if (!res.regfile->WriteRegister(a, value, false))
	    {
            DeadlockWrite("Unable to write register %s", res.address.str().c_str());
	    	return false;
	    }

        // We do this in two steps; otherwise the compiler could complain
        // about shifting the whole data size.
        data >>= sizeof(Integer) * 4;
        data >>= sizeof(Integer) * 4;
	}
	return true;
}

Result FPU::OnCycleWritePhase(unsigned int stateIndex)
{
	if (stateIndex < m_units.size())
	{
	    // Advance a pipeline
 		Unit& unit = m_units[stateIndex];
	    if (!unit.slots.empty())
	    {
	        const Result& res = unit.slots.front();
		    if (res.state == unit.latency)
    	    {
    	        // This operation has completed
                // Write back result
		        if (!OnCompletion(res))
		        {
        	        // Stall pipeline
    		        return FAILED;
	            }
	    	
	            // Clear the result
	            COMMIT{ unit.slots.pop_front(); }
	        }
	        
	        COMMIT
	        {
	            // Advance the pipeline
 		        for (deque<Result>::iterator p = unit.slots.begin(); p != unit.slots.end(); ++p)
 		        {
     		        p->state++;
 	    	    }
     		}
   		    return SUCCESS;
        }
    }
    else
    {
        assert(stateIndex == m_units.size());
    
        /*
         Select an input to put in an execution unit.
         We grab an operation from the fullest queue.
      
         Note that we might pick a queue whose operation has to go in a unit
         that is full. If possible in hardware, we can optimize to check this
         and pick another queue instead. Here, however, we don't.
        */
        QueueMap::iterator q = m_queues.end();
        for (QueueMap::iterator p = m_queues.begin(); p != m_queues.end(); ++p)
        {
            if (q == m_queues.end() || p->second.size() > q->second.size())
            {
                q = p;
            }
        }
    
        if (q != m_queues.end() && !q->second.empty())
        {
            const Operation& op = q->second.front();
            
            // Find a unit that can accept this operation
            size_t i;
            for (i = 0; i < m_units.size(); ++i)
            {
                Unit& unit = m_units[i];
                if (unit.ops.find(op.op) != unit.ops.end() && (unit.slots.empty() || (unit.pipelined && unit.slots.back().state > 1)))
                {
                    // This unit can accept our operation
                    break;
                }
            }
            
            if (i == m_units.size())
            {
                // All units that can accept the op are busy or
                // the pipeline cannot accept a new operation
                return FAILED;
            }
        
            // Calculate the result and store it in the unit
            COMMIT{
                Result res = CalculateResult(op);
                res.regfile = q->first;
                m_units[i].slots.push_back(res);
            }
            
            // Remove the queued operation from the queue
            q->second.pop();
            return SUCCESS;
        }
    }
    return DELAYED;
}

static string GetStateNames(const Config& config)
{
    stringstream ss;
    for (int i = 1;; ++i)
    {
        stringstream name;
        name << "FPUUnit" << i << "Ops";
        if (config.getString(name.str(), "") == "")
        {
            break;
        }
        ss << "unit" << i << "|";
    }
    ss << "read-input";
    return ss.str();
}

FPU::FPU(Object* parent, Kernel& kernel, const std::string& name, const Config& config)
	: IComponent(parent, kernel, name, GetStateNames(config)),
	  m_queueSize  (config.getInteger<BufferSize>("FPUBufferSize", INFINITE))  
{
    static const char* const Names[FPU_NUM_OPS] = {
        "ADD","SUB","MUL","DIV","SQRT"
    };
    
    for (int i = 1;; ++i)
    {
        stringstream name;
        name << "FPUUnit" << i;
        
        Unit unit;
        
        // Get ops for this unit
        string ops = config.getString(name.str() + "Ops", "");
        transform(ops.begin(), ops.end(), ops.begin(), ::toupper);
        stringstream ss(ops);
        while (getline(ss, ops, ',')) {
            for (int j = 0; j < FPU_NUM_OPS; ++j) {
                if (ops.compare(Names[j]) == 0) {
                    unit.ops.insert( (FPUOperation)j );
                    break;
                }
            }
        }
        
        if (unit.ops.empty())
        {
            break;
        }
        
        unit.latency   = config.getInteger<CycleNo>( name.str() + "Latency",   1);
        unit.pipelined = config.getBoolean         ( name.str() + "Pipelined", false);
        m_units.push_back(unit);
    }
}

}
