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
	if (stateIndex < FPU_NUM_OPS)
	{
	    // Advance a pipeline
 		Pipeline& pipeline = m_pipelines[stateIndex];
	    if (!pipeline.slots.empty())
	    {
	        const Result& res = pipeline.slots.front();
		    if (res.state == pipeline.latency)
    	    {
    	        // This operation has completed
                // Write back result
		        if (!OnCompletion(res))
		        {
        	        // Stall pipeline
    		        return FAILED;
	            }
	    	
	            // Clear the result
	            COMMIT{ pipeline.slots.pop_front(); }
	        }
	        
	        COMMIT
	        {
	            // Advance the pipeline
 		        for (deque<Result>::iterator p = pipeline.slots.begin(); p != pipeline.slots.end(); ++p)
 		        {
     		        p->state++;
 	    	    }
     		}
   		    return SUCCESS;
        }
    }
    else
    {
        assert(stateIndex == FPU_NUM_OPS);
    
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
            Pipeline& pipeline = m_pipelines[op.op];
            if (!pipeline.slots.empty() && (!pipeline.pipelined || pipeline.slots.back().state == 1))
            {
                // Unit is busy or pipeline cannot accept a new operation
                return FAILED;
            }
        
            // Calculate the result and store it in the unit
            COMMIT{
                Result res = CalculateResult(op);
                res.regfile = q->first;
                pipeline.slots.push_back(res);
            }
            
            // Remove the queued operation from the queue
            q->second.pop();
            return SUCCESS;
        }
    }
    return DELAYED;
}

FPU::FPU(Object* parent, Kernel& kernel, const std::string& name, const Config& config)
	: IComponent(parent, kernel, name, "add|sub|mul|div|sqrt|read-input"),
	  m_queueSize  (config.getInteger<BufferSize>("FPUBufferSize", INFINITE))  
{
    static const char* const Names[FPU_NUM_OPS] = {
        "Add","Sub","Mul","Div","Sqrt"
    };
    
    for (int i = 0; i < FPU_NUM_OPS; ++i)
    {
        Pipeline& p = m_pipelines[i];
        p.latency   = config.getInteger<CycleNo>( string("FPU") + Names[i] + "Latency", 1);
        p.pipelined = config.getBoolean( string("FPUPipeline") + Names[i], false);
    }
}

}
