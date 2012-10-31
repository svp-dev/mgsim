#include "FPU.h"
#include <sim/config.h>

#include <cassert>
#include <cmath>
#include <iomanip>

using namespace std;

namespace Simulator
{

static const char* const OperationNames[FPU_NUM_OPS] = {
    "ADD", "SUB", "MUL", "DIV", "SQRT"
};

StorageTraceSet FPU::GetSourceTrace(size_t source) const
{
    return m_sources[source]->inputs;
}

StorageTraceSet FPU::CreateStoragePermutation(size_t num_sources, std::vector<bool>& visited)
{
    StorageTraceSet res;
    for (size_t i = 0; i < num_sources; ++i)
    {
        if (!visited[i])
        {
            visited[i] = true;
            StorageTraceSet perms = CreateStoragePermutation(num_sources, visited);
            visited[i] = false;

            res ^= m_sources[i]->outputs;
            res ^= m_sources[i]->outputs * perms;
        }
    }
    return res;
}

size_t FPU::RegisterSource(Processor::RegisterFile& regfile, const StorageTraceSet& output)
{
    for (size_t i = 0; i < m_sources.size(); ++i)
    {
        if (m_sources[i]->regfile == NULL)
        {
            m_sources[i]->regfile = &regfile;
            m_sources[i]->outputs = output;

            // Any number of outputs can be written in any order.
            size_t num_sources = i + 1;
            vector<bool> visited(num_sources, false);
            StorageTraceSet outputs = CreateStoragePermutation(num_sources, visited);
            p_Pipeline.SetStorageTraces(opt(outputs) * opt(m_active));
            return i;
        }
    }
    // This shouldn't happen
    assert(0);
    return SIZE_MAX;
}

string FPU::Operation::str() const
{
    ostringstream ss;
    ss << OperationNames[op] << size * 8
       << ' ' << setprecision(12) << Rav
       << ", " << setprecision(12) << Rbv
       << ", " << Rc.str();
    return ss.str();
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

    DebugFPUWrite("queued %s %s",
                  m_sources[source]->regfile->GetParent()->GetFQN().c_str(),
                  op.str().c_str());
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

    Source *source = m_sources[res.source];

    if (source->last_write == now && source->last_unit != unit)
    {
        DeadlockWrite("Unable to write back result because another FPU pipe already wrote back this cycle");
        return false;
    }
    source->last_write = now;
    source->last_unit  = unit;

    // Calculate the address of this register
    RegAddr addr = res.address;
    addr.index += res.index;

    if (!source->regfile->p_asyncW.Write(addr))
    {
        DeadlockWrite("Unable to acquire port to write back to %s", addr.str().c_str());
        return false;
    }

    // Read the old value
    RegValue value;
    if (!source->regfile->ReadRegister(addr, value))
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

    if (!source->regfile->WriteRegister(addr, value, false))
    {
        DeadlockWrite("Unable to write register %s", addr.str().c_str());
        return false;
    }

    DebugFPUWrite("unit %u completed %s %s <- %s",
                  (unsigned)unit,
                  source->regfile->GetParent()->GetFQN().c_str(),
                  addr.str().c_str(),
                  value.str(addr.type).c_str());
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
                    for (auto& p : unit.slots)
                        p.state++;
                }
            }
        }
    }

    size_t num_sources_failed = 0, num_sources_active = 0;
    for (size_t i = m_last_source; i < m_last_source + m_sources.size(); ++i)
    {
        size_t source_id = i % m_sources.size();

        // Process an input queue
        Buffer<Operation>& input = m_sources[source_id]->inputs;
        if (!input.Empty())
        {
            num_sources_active++;

            const Operation& op = input.Front();

            // We use a fixed (with modulo) mapping from inputs to units
            const size_t unit_index = m_mapping[op.op][ source_id % m_mapping[op.op].size() ];
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
                res.source = source_id;
                unit.slots.push_back(res);
            }
            num_units_full++;

            DebugFPUWrite("unit %u executing %s %s",
                          (unsigned)unit_index,
                          m_sources[source_id]->regfile->GetParent()->GetFQN().c_str(),
                          op.str().c_str());

            // Remove the queued operation from the queue
            input.Pop();
        }
    }

    COMMIT { m_last_source = (m_last_source + 1) % m_sources.size(); }

    if (num_units_full > 0) {
        m_active.Write(true);
    } else {
        m_active.Clear();
    }

    return (num_units_failed == num_units_active && num_sources_failed == num_sources_active) ? FAILED : SUCCESS;
}

FPU::Source::Source(const std::string& name, Object& parent, Clock& clock, Config& config)
    : Object(name, parent, clock),
      inputs("b_source", *this, clock, config.getValue<BufferSize>(*this, "InputQueueSize")),
      outputs(),
      regfile(NULL),
      last_write(0),
      last_unit(0)
{}


FPU::FPU(const std::string& name, Object& parent, Clock& clock, Config& config, size_t num_inputs)
    : Object(name, parent, clock),
      m_active("r_active", *this, clock),
      m_sources(),
      m_units(),
      m_last_source(0),
      p_Pipeline(*this, "pipeline", delegate::create<FPU, &FPU::DoPipeline>(*this) )
{
    m_active.Sensitive(p_Pipeline);
    try
    {
        static const char* const Names[FPU_NUM_OPS] = {
            "ADD","SUB","MUL","DIV","SQRT"
        };

        // Construct the FP units
        size_t nUnits = config.getValue<size_t>(*this, "NumUnits");
        if (nUnits == 0)
        {
            throw InvalidArgumentException(*this, "NumUnits not set or zero");
        }
        for (size_t i = 0; i < nUnits; ++i)
        {
            stringstream ssname;
            ssname << "Unit" << i;
            string uname = ssname.str();

            set<FPUOperation> ops;

            // Get ops for this unit
            auto strops = config.getWordList(*this, uname + "Ops");
            for (auto& p : strops)
            {
                transform(p.begin(), p.end(), p.begin(), ::toupper);
                for (int j = 0; j < FPU_NUM_OPS; ++j) {
                    if (p.compare(Names[j]) == 0) {
                        ops.insert( (FPUOperation)j );
                        break;
                    }
                }
            }
            if (ops.empty())
            {
                throw exceptf<InvalidArgumentException>(*this, "No operation specified for unit %zu", i);
            }

            // Add this unit into the mapping table for the ops it implements
            for (auto& p : ops)
            {
                m_mapping[p].push_back(m_units.size());
            }

            Unit unit;
            unit.latency   = config.getValue<CycleNo>(*this, uname+"Latency");
            unit.pipelined = config.getValue<bool>   (*this, uname+"Pipelined");
            m_units.push_back(unit);
        }

        // Construct the sources
        for (size_t i = 0; i < num_inputs; ++i)
        {
            stringstream ssname;
            ssname << "source" << i;
            string sname = ssname.str();

            m_sources.push_back(NULL);
            Source* source = new Source(sname, *this, clock, config);
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
    for (auto s : m_sources)
        delete s;
}

FPU::~FPU()
{
    Cleanup();
}

void FPU::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The Floating-Point Unit executes floating-point operations asynchronously and\n"
    "can be shared among multiple processors. Results are written back asynchronously\n"
    "to the original processor's register file.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Reads and displays the FPU's queues and pipelines.\n";
}

void FPU::Cmd_Read(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out << fixed << setfill(' ');

    // Print the source queues
    for (auto source : m_sources)
    {
        // Print the source name
        out << "Source: ";
        Object* object = (source->regfile != NULL) ? source->regfile->GetParent() : NULL;
        if (object == NULL) {
            out << "???";
        } else {
            out << object->GetFQN();
        }
        out << endl;

        if (source->inputs.begin() != source->inputs.end())
        {
            // Print the queued operations
            out << " Op  | Sz |           A          |            B         | Dest " << endl;
            out << "-----+----+----------------------+----------------------+------" << endl;
            for (auto& p : source->inputs)
            {
                out << setw(4) << left << OperationNames[p.op] << right << " | "
                    << setw(2) << left << p.size * 8 << right << " | "
                    << setw(20) << setprecision(12) << p.Rav << " | "
                    << setw(20);
                if (p.op != FPU_OP_SQRT) {
                    out << setprecision(12) << fixed << p.Rbv;
                } else {
                    out << " ";
                }
                out << " | "
                    << p.Rc.str()
                    << endl;
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
    size_t i = 0;
    for (auto& unit : m_units)
    {
        // Print information of this unit
        out << "Unit:       #" << dec << i++ << endl;
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
            out << " t | Sz |        Result       |  Reg  | Destination" << endl;
            out << "---+----+---------------------+-------+--------------------" << endl;
            for (auto& p : unit.slots)
            {
                out << setw(2) << p.state << " | "
                    << setw(2) << p.size * 8 << " | "
                    << setw(20) << setprecision(12) << p.value.tofloat(p.size)  << " | "
                    << p.address.str() << " | "
                    << m_sources[p.source]->regfile->GetParent()->GetFQN()
                    << endl;
            }
            out << endl;
        }
        out << endl;
    }
}

}
