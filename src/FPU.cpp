#include "FPU.h"
#include "RegisterFile.h"
#include "config.h"
#include <cassert>
#include <cmath>
using namespace std;

namespace Simulator
{

FPU::Unit::Unit(const Object& object, const std::string& name)
    : service(object, name)
{
}

static const char* const OperationNames[FPU_NUM_OPS] = {
    "ADD", "SUB", "MUL", "DIV", "SQRT"
};

size_t FPU::RegisterSource(RegisterFile& regfile)
{
    for (size_t i = 0; i < m_sources.size(); ++i)
    {
        if (m_sources[i].regfile == NULL)
        {
            m_sources[i].regfile = &regfile;
            return i;
        }
    }
    // This shouldn't happen
    assert(0);
    return SIZE_MAX;
}
    
bool FPU::QueueOperation(size_t source, FPUOperation fop, int size, double Rav, double Rbv, const RegAddr& Rc)
{
	// The size must be a multiple of the arch's native integer size
	assert(source < m_sources.size());
	assert(m_sources[source].regfile != NULL);
	assert(size > 0);
	assert(size % sizeof(Integer) == 0);
	assert(Rc.valid());

    Operation op;
    op.op   = fop;
    op.size = size;
    op.Rav  = Rav;
    op.Rbv  = Rbv;
    op.Rc   = Rc;
    
    if (!m_sources[source].inputs.push(op))
    {
        return false;
    }
    
    DebugSimWrite("Queued FP %s operation into queue %u", OperationNames[fop], source);
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
    if (!res.source->regfile->p_asyncW.Write(res.address))
    {
        DeadlockWrite("Unable to acquire port to write back to %s", res.address.str().c_str());
    	return false;
    }

    RegValue value;
    if (!res.source->regfile->ReadRegister(res.address, value))
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
	    if (!res.source->regfile->WriteRegister(a, value, false))
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
 		Unit& unit = *m_units[stateIndex];
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
        // Process an input queue
        const size_t input_index = stateIndex - m_units.size();
        Buffer<Operation>& input = m_sources[input_index].inputs;
        if (!input.empty())
        {
            const Operation& op = input.front();

            // We use a fixed (with modulo) mapping from inputs to units
            const size_t unit_index = m_mapping[op.op][ input_index % m_mapping[op.op].size() ];
            Unit& unit = *m_units[ unit_index ];
            
            // Request access
            if (!unit.service.Invoke())
            {
                return FAILED;
            }
            
            if (!IsAcquiring())
            {
                // See if the unit can accept a new request
                // Do this check after the Acquire phase, when the actual pipeline has
                // moved on and made room for our request.
                if (!unit.slots.empty() && (!unit.pipelined || unit.slots.back().state == 1))
                {
                    // The unit is busy or cannot accept a new operation
                    return FAILED;
                }
            }
        
            // Calculate the result and store it in the unit
            COMMIT{
                Result res = CalculateResult(op);
                res.source = &m_sources[input_index];
                unit.slots.push_back(res);
            }
            
            DebugSimWrite("Put %s operation from queue %u into pipeline %u", OperationNames[op.op], (unsigned)input_index, (unsigned)unit_index);
            
            // Remove the queued operation from the queue
            input.pop();
            return SUCCESS;
        }
    }
    return DELAYED;
}

static string GetStateNames(const Config& config, size_t num_inputs)
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
    
    for (size_t i = 0; i < num_inputs; ++i)
    {
        ss << "input" << i;
        if (i < num_inputs - 1) {
            ss << "|";
        }
    }
    return ss.str();
}

FPU::FPU(Object* parent, Kernel& kernel, const std::string& name, const Config& config, size_t num_inputs)
	: IComponent(parent, kernel, name, GetStateNames(config, num_inputs)),
	  m_sources(num_inputs, Source(kernel, config.getInteger<BufferSize>("FPUBufferSize", INFINITE)))
{
    try
    {
        static const char* const Names[FPU_NUM_OPS] = {
            "ADD","SUB","MUL","DIV","SQRT"
        };
    
        // Construct the FP units
        for (int i = 1;; ++i)
        {
            stringstream ssname;
            ssname << "FPUUnit" << i;
            string name = ssname.str();
        
            set<FPUOperation> ops;
                
            // Get ops for this unit
            string strops = config.getString(name + "Ops", "");
            transform(strops.begin(), strops.end(), strops.begin(), ::toupper);
            stringstream ss(strops);
            while (getline(ss, strops, ',')) {
                for (int j = 0; j < FPU_NUM_OPS; ++j) {
                    if (strops.compare(Names[j]) == 0) {
                        ops.insert( (FPUOperation)j );
                        break;
                    }
                }
            }
        
            if (ops.empty())
            {
                break;
            }
        
            // Add this unit into the mapping table for the ops it implements
            for (set<FPUOperation>::const_iterator p = ops.begin(); p != ops.end(); ++p)
            {
                m_mapping[*p].push_back(m_units.size());
            }

            m_units.push_back(NULL);
            Unit* unit = m_units.back() = new Unit(*this, name);
            unit->latency   = config.getInteger<CycleNo>( name + "Latency",   1);
            unit->pipelined = config.getBoolean         ( name + "Pipelined", false);            
        }
        
        // Set up priorities for the unit arbitrators;
        // all inputs have access to all units
        for (size_t i = 0; i < m_units.size(); ++i)
        {
            for (size_t j = 0; j < num_inputs; ++j)
            {
                const int state = (int)(m_units.size() + j);
                m_units[i]->service.AddSource(ArbitrationSource(this, state));
            }
        }
    }
    catch (...)
    {
        Cleanup();
        throw;
    }
}

void FPU::Cleanup()
{
    for (size_t i = 0; i < m_units.size(); ++i)
    {
        delete m_units[i];
    }
}

FPU::~FPU()
{
    Cleanup();
}

}
