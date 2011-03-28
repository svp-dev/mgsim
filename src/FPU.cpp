#include "FPU.h"
#include "RegisterFile.h"
#include "config.h"
#include <cassert>
#include <cmath>
#include <iomanip>
using namespace std;

namespace Simulator
{

static const char* const OperationNames[FPU_NUM_OPS] = {
    "ADD", "SUB", "MUL", "DIV", "SQRT"
};

size_t FPU::RegisterSource(RegisterFile& regfile)
{
    for (size_t i = 0; i < m_sources.size(); ++i)
    {
        if (m_sources[i]->regfile == NULL)
        {
            m_sources[i]->regfile = &regfile;
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
    assert(m_sources[source]->regfile != NULL);
    assert(size > 0);
    assert(size % sizeof(Integer) == 0);
    assert(Rc.valid());

    Operation op;
    op.op   = fop;
    op.size = size;
    op.Rav  = Rav;
    op.Rbv  = Rbv;
    op.Rc   = Rc;
    
    if (!m_sources[source]->inputs.Push(op))
    {
        return false;
    }
    
    DebugSimWrite("Queued FP %s operation into queue %u", OperationNames[fop], (unsigned)source);
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
    default:          value = 0.0; assert(0); break;
    }
    
    Result  res;
    res.address = op.Rc;
    res.size    = op.size;
    res.index   = 0;
    res.state   = 1;
    res.value.fromfloat(value, op.size);

    return res;
}

bool FPU::OnCompletion(unsigned int unit, const Result& res) const
{
    const CycleNo now = GetCycleNo();
    
    if (res.source->last_write == now && res.source->last_unit != unit)
    {
        DeadlockWrite("Unable to write back result because another FPU pipe already wrote back this cycle");
        return false;
    }
    res.source->last_write = now;
    res.source->last_unit  = unit;
    
    // Calculate the address of this register
    RegAddr addr = res.address;
    addr.index += res.index;

    if (!res.source->regfile->p_asyncW.Write(addr))
    {
        DeadlockWrite("Unable to acquire port to write back to %s", addr.str().c_str());
        return false;
    }

    // Read the old value
    RegValue value;
    if (!res.source->regfile->ReadRegister(addr, value))
    {
        DeadlockWrite("Unable to read register %s", addr.str().c_str());
        return false;
    }

    if (value.m_state != RST_PENDING && value.m_state != RST_WAITING)
    {
        // We're too fast, wait!
        DeadlockWrite("FP operation completed before register %s was cleared", addr.str().c_str());
        return false;
    }
        
    // Write new value
    unsigned int index = res.index;
#ifdef ARCH_BIG_ENDIAN
    const unsigned int size = res.size / sizeof(Integer);
    index = size - 1 - index;
#endif

    value.m_state         = RST_FULL;
    value.m_float.integer = (Integer)(res.value.toint(res.size) >> (sizeof(Integer) * 8 * index));
        
    if (!res.source->regfile->WriteRegister(addr, value, false))
    {
        DeadlockWrite("Unable to write register %s", addr.str().c_str());
        return false;
    }

    DebugSimWrite("Wrote FP result back to %s", addr.str().c_str());
    return true;
}

Result FPU::DoPipeline()
{
    size_t num_units_active = 0, num_units_failed = 0;
    size_t num_units_full = 0;
    for (size_t i = 0; i < m_units.size(); ++i)
    {
        // Advance a pipeline
        Unit& unit = m_units[i];
        if (!unit.slots.empty())
        {
            num_units_active++;
            num_units_full++;
            
            bool advance = true;
            Result&  res = unit.slots.front();
            if (res.state == unit.latency)
            {
                // This operation has completed
                // Write back result
                if (!OnCompletion(i, res))
                {
                    // Stall; stop processing this pipeline
                    num_units_failed++;
                    continue;
                }
                
                if (res.index + 1 == res.size / sizeof(Integer))
                {
                    // We've written the last register of the result;
                    // clear the result
                    if (unit.slots.size() == 1)
                    {
                        // It's empty now
                        num_units_full--;
                    }
                    COMMIT{ unit.slots.pop_front(); }
                }
                else
                {
                    // We're not done yet -- delay the FPU pipeline
                    advance = false;
                    COMMIT{ ++res.index; }
                }
            }
            
            if (advance)
            {
                COMMIT
                {
                    // Advance the pipeline
                    for (deque<Result>::iterator p = unit.slots.begin(); p != unit.slots.end(); ++p)
                    {
                        p->state++;
                    }
                }
            }
        }
    }
    
    size_t num_sources_failed = 0, num_sources_active = 0;
    for (size_t i = 0; i < m_sources.size(); ++i)
    {
        // Process an input queue
        Buffer<Operation>& input = m_sources[i]->inputs;
        if (!input.Empty())
        {
            num_sources_active++;
            
            const Operation& op = input.Front();

            // We use a fixed (with modulo) mapping from inputs to units
            const size_t unit_index = m_mapping[op.op][ i % m_mapping[op.op].size() ];
            Unit& unit = m_units[ unit_index ];
            
            if (!IsAcquiring())
            {
                // See if the unit can accept a new request
                // Do this check after the Acquire phase, when the actual pipeline has
                // moved on and made room for our request.
                if (!unit.slots.empty() && (!unit.pipelined || unit.slots.back().state == 1))
                {
                    // The unit is busy or cannot accept a new operation
                    num_sources_failed++;
                    continue;
                }
            }
        
            // Calculate the result and store it in the unit
            COMMIT{
                Result res = CalculateResult(op);
                res.source = m_sources[i];
                unit.slots.push_back(res);
            }
            num_units_full++;
            
            DebugSimWrite("Put %s operation from queue %u into pipeline %u",
                OperationNames[op.op], (unsigned)i, (unsigned)unit_index);
            
            // Remove the queued operation from the queue
            input.Pop();
        }
    }

    if (num_units_full > 0) {
        m_active.Write(true);
    } else {
        m_active.Clear();
    }
        
    return (num_units_failed == num_units_active && num_sources_failed == num_sources_active) ? FAILED : SUCCESS;
}

FPU::FPU(const std::string& name, Object& parent, Clock& clock, const Config& config, size_t num_inputs)
    : Object(name, parent, clock),
      m_active("r_active", *this, clock),
      p_Pipeline("pipeline", delegate::create<FPU, &FPU::DoPipeline>(*this) )
{
    m_active.Sensitive(p_Pipeline);
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
            vector<string> strops = config.getValueList<string>(name + "Ops");
            for (vector<string>::iterator p = strops.begin(); p != strops.end(); ++p)
            {
                transform(p->begin(), p->end(), p->begin(), ::toupper);
                for (int j = 0; j < FPU_NUM_OPS; ++j) {
                    if (p->compare(Names[j]) == 0) {
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

            Unit unit;
            unit.latency   = config.getValue<CycleNo>( name + "Latency",   1);
            unit.pipelined = config.getValue<bool>   ( name + "Pipelined", false);
            m_units.push_back(unit);
        }
        
        // Construct the sources
        const BufferSize input_buffer_size = config.getValue<BufferSize>("FPUBufferSize", INFINITE);
        for (size_t i = 0; i < num_inputs; ++i)
        {
            stringstream sname;
            sname << "source" << i;
            string name = sname.str();

            m_sources.push_back(NULL);
            Source* source = new Source(name, *this, clock, input_buffer_size);
            source->inputs.Sensitive(p_Pipeline);
            m_sources.back() = source;
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
    for (size_t i = 0; i < m_sources.size(); ++i)
    {
        delete m_sources[i];
    }
}

FPU::~FPU()
{
    Cleanup();
}

void FPU::Cmd_Help(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The Floating-Point Unit executes floating-point operations asynchronously and\n"
    "can be shared among multiple processors. Results are written back asynchronously\n"
    "to the original processor's register file.\n\n"
    "Supported operations:\n"
    "- read <component>\n"
    "  Reads and displays the FPU's queues and pipelines.\n";
}

void FPU::Cmd_Read(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out << fixed << setfill(' ');
    
    // Print the source queues
    for (size_t i = 0; i < m_sources.size(); ++i)
    {
        const Source& source = *m_sources[i];
        
        // Print the source name
        out << "Source: ";
        Object* object = (source.regfile != NULL) ? source.regfile->GetParent() : NULL;
        if (object == NULL) {
            out << "???";
        } else {
            out << object->GetFQN();
        }
        out << endl;
        
        if (source.inputs.begin() != source.inputs.end())
        {
            // Print the queued operations
            out << " Op  |           A            |            B           | Dest" << endl;
            out << "-----+------------------------+------------------------+-------" << endl;
            for (Buffer<Operation>::const_iterator p = source.inputs.begin(); p != source.inputs.end(); ++p)
            {
                out << setw(4) << left << OperationNames[p->op] << right << " | "
                    << setw(20) << setprecision(12) << p->Rav << "/" << p->size << " | "
                    << setw(20);
                if (p->op != FPU_OP_SQRT) { 
                    out << setprecision(12) << fixed << p->Rbv << "/" << p->size;
                } else {
                    out << " ";
                }
                out << " | "
                    << p->Rc.str() << endl;
            }
        }
        else
        {
            out << "(Empty)" << endl;
        }
        out << endl;
    }
    out << endl;
    
    // Print the execution units
    for (size_t i = 0; i < m_units.size(); ++i)
    {
        const Unit& unit = m_units[i];
        
        // Print information of this unit
        out << "Unit:       #" << dec << i << endl;
        out << "Pipelined:  " << boolalpha << unit.pipelined << endl;
        out << "Latency:    " << unit.latency << " cycles" << endl;
        out << "Operations:";
        for (unsigned int j = 0; j < FPU_NUM_OPS; ++j)
        {
            if (find(m_mapping[j].begin(), m_mapping[j].end(), i) != m_mapping[j].end()) {
                out << " " << OperationNames[j];
            }
        }
        out << endl << endl;
        
        // Print pipeline
        if (unit.slots.empty())
        {
            out << "(Empty)" << endl;
        }
        else
        {
            out << " t |         Result         |  Reg  | Destination" << endl;
            out << "---+------------------------+-------+--------------------" << endl;
            for (deque<Result>::const_iterator p = unit.slots.begin(); p != unit.slots.end(); ++p)
            {
                out << setw(2) << p->state << " | "
                    << setw(20) << setprecision(12) << p->value.tofloat(p->size) << "/" << p->size << " | "
                    << p->address.str() << " | "
                    << p->source->regfile->GetParent()->GetFQN()
                    << endl;
            }
            out << endl;
        }
        out << endl;
    }
}

}
