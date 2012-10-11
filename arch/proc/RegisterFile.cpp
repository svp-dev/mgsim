#include "Processor.h"
#include <sim/config.h>
#include <sim/range.h>

#include <cassert>
#include <iomanip>
using namespace std;

namespace Simulator
{

//
// RegisterFile implementation
//

Processor::RegisterFile::RegisterFile(const std::string& name, Processor& parent, Clock& clock, Allocator& alloc, Config& config)
  : Object(name, parent, clock),
    Structure<RegAddr>(name, parent, clock),
    Storage("storage", *this, clock),
    p_pipelineR1(*this, "p_pipelineR1"),
    p_pipelineR2(*this, "p_pipelineR2"),
    p_pipelineW (*this, "p_pipelineW"),
    p_asyncR    (*this, "p_asyncR"),
    p_asyncW    (*this, "p_asyncW"),
    m_allocator(alloc),
    m_nUpdates(0),
    m_integers(config.getValue<size_t>(*this, "NumIntRegisters")),
    m_floats  (config.getValue<size_t>(*this, "NumFltRegisters"))
{
    // Initialize all registers to empty
    for (RegSize i = 0; i < m_integers.size(); ++i)
    {
        m_integers[i] = MAKE_EMPTY_REG();
    }

    for (RegSize i = 0; i < m_floats.size(); ++i)
    {
        m_floats[i] = MAKE_EMPTY_REG();
    }
    
    // Set port priorities; first port has highest priority
    AddPort(p_pipelineW);
    AddPort(p_asyncW);
}

RegSize Processor::RegisterFile::GetSize(RegType type) const
{
    const vector<RegValue>& regs = PickFile(type);
    return regs.size();
}

bool Processor::RegisterFile::ReadRegister(const RegAddr& addr, RegValue& data, bool quiet) const
{
    const vector<RegValue>& regs = PickFile(addr.type);
    if (addr.index >= regs.size())
    {
        throw SimulationException("A component attempted to read from a non-existing register", *this);
    }
    data = regs[addr.index];

    if (!quiet)
        DebugRegWrite("read  %s -> %s", addr.str().c_str(), data.str(addr.type).c_str());

    return true;
}

// Admin version
bool Processor::RegisterFile::WriteRegister(const RegAddr& addr, const RegValue& data)
{
    vector<RegValue>& regs = PickFile(addr.type);
    if (addr.index < regs.size())
    {
        DebugRegWrite("write %s <- %s (was %s, ADMIN)", addr.str().c_str(), 
                      data.str(addr.type).c_str(),
                      regs[ addr.index ].str(addr.type).c_str());
        regs[addr.index] = data;
        return true;
    }
    return false;
}

bool Processor::RegisterFile::Clear(const RegAddr& addr, RegSize size)
{
    std::vector<RegValue>& regs = PickFile(addr.type);
    if (addr.index + size > regs.size())
    {
        throw SimulationException("A component attempted to clear a non-existing register", *this);
    }

    COMMIT
    {
        const RegValue value = MAKE_EMPTY_REG();
        for (RegSize i = 0; i < size; ++i)
        {
            regs[addr.index + i] = value;
        }
    }

    return true;
}

bool Processor::RegisterFile::WriteRegister(const RegAddr& addr, const RegValue& data, bool from_memory)
{
    std::vector<RegValue>& regs = PickFile(addr.type);
    if (addr.index >= regs.size())
    {
        throw SimulationException("A component attempted to write to a non-existing register", *this);
    }

    assert(data.m_state == RST_EMPTY || data.m_state == RST_PENDING || data.m_state == RST_WAITING || data.m_state == RST_FULL);
    
    if (data.m_state == RST_EMPTY || data.m_state == RST_PENDING)
    {
        assert(data.m_waiting.head == INVALID_TID);
    }

    const RegValue& value = regs[addr.index];
    if (value.m_state != RST_FULL)
    {
        if (value.m_state == RST_WAITING && data.m_state == RST_EMPTY)
        {
            throw exceptf<SimulationException>(*this, "Invalid reset of %s: %s becomes %s", addr.str().c_str(), value.str(addr.type).c_str(), data.str(addr.type).c_str());
        }

        if (value.m_memory.size != 0)
        {
            // Check that the memory information isn't changed
            if (data.m_state              == RST_FULL ||
                data.m_memory.fid         != value.m_memory.fid ||
                data.m_memory.offset      != value.m_memory.offset ||
                data.m_memory.size        != value.m_memory.size ||
                data.m_memory.sign_extend != value.m_memory.sign_extend ||
                data.m_memory.next        != value.m_memory.next)
            {
                if (!from_memory)
                {
                    // Only the memory can change memory-pending registers
                    throw exceptf<SimulationException>(*this, "Invalid reset of pending load %s: %s becomes %s", addr.str().c_str(), value.str(addr.type).c_str(), data.str(addr.type).c_str());
                }
            }
        }        
       
        if (data.m_state == RST_FULL)
        {
            if (value.m_state == RST_WAITING && value.m_waiting.head != INVALID_TID)
            {
                // This write caused a reschedule
                if (!m_allocator.ActivateThreads(value.m_waiting))
                {
                    DeadlockWrite("Unable to wake up threads after write of %s: %s becomes %s", addr.str().c_str(), value.str(addr.type).c_str(), data.str(addr.type).c_str());
                    return false;
                }
            }
        }
    }
    
    COMMIT
    {
#ifndef NDEBUG
        // Paranoid sanity check:
        // We cannot have multiple writes to same register in a cycle
        for (unsigned int i = 0; i < m_nUpdates; ++i)
        {
            assert(m_updates[i].first != addr);
        }
#endif

        // Queue the update
        assert(m_nUpdates < MAX_UPDATES);
        m_updates[m_nUpdates] = make_pair(addr, data);
        if (m_nUpdates == 0) {
            RegisterUpdate();
        }
        m_nUpdates++;
    }
    return true;
}

void Processor::RegisterFile::Update()
{
    // Commit the queued updates to registers
    assert(m_nUpdates > 0);
    for (unsigned int i = 0; i < m_nUpdates; ++i)
    {
        RegAddr& addr = m_updates[i].first;
        RegType type = addr.type;
        vector<RegValue>& regs = PickFile(type);

        DebugRegWrite("write %s <- %s (was %s)", addr.str().c_str(), 
                      m_updates[i].second.str(type).c_str(),
                      regs[ addr.index ].str(type).c_str());

        regs[ addr.index ] = m_updates[i].second;
    }
    m_nUpdates = 0;
}

void Processor::RegisterFile::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The Register File stores the register for all threads running on a processor.\n"
    "Each register consists of data and state bits.\n\n"
    "Supported operations:\n"
    "- inspect <component> [type] [range]\n"
    "  Reads and displays the used registers. The optional type argument can be\n"
    "  used to select between the integer (\"int\") and floating-point (\"float\")\n"
    "  Register Files.\n"
    "  An optional range argument can be given to only read those registers. The\n"
    "  range is a comma-seperated list of register ranges. Example ranges:\n"
    "  \"1\", \"1-4,15,7-8\", \"all\"\n";
}

void Processor::RegisterFile::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    const RAUnit*    rau    = NULL;

    // Need to change this if the RF is not directly child of parent
    Processor& parent = dynamic_cast<Processor&>(*GetParent());

    // Find the RAUnit in the same processor
    for (unsigned int i = 0; i < parent.GetNumChildren(); ++i)
    {
        const Object* child = parent.GetChild(i);
        if (rau   == NULL) rau   = dynamic_cast<const RAUnit*>(child);
    }

    RegType type = RT_INTEGER;
    size_t  i    = 0;
    if (!arguments.empty())
    {
        if (arguments[i] == "float") {
            type = RT_FLOAT;
            i++;
        } else if (arguments[i] == "integer") {
            type = RT_INTEGER;
            i++;
        }
    }
        
    vector<LFID> regs(GetSize(type), INVALID_LFID);
    if (rau != NULL)
    {
        const RAUnit::List& list = rau->m_types[type].list;
        const RegSize blockSize  = rau->m_types[type].blockSize;
        for (size_t i = 0; i < list.size();)
        {
            if (list[i].first != 0)
            {
                for (size_t j = 0; j < list[i].first * blockSize; ++j)
                {
                    regs[i * blockSize + j] = list[i].second;
                }
                i += list[i].first;
            }
            else i++;
        }
    }

    set<RegIndex> indices;
    if (i < arguments.size())
    {
        indices = parse_range<RegIndex>(arguments[i], 0, regs.size());
    }
    else
    {
        // Default: add all registers that are part of a family
        for (size_t i = 0; i < regs.size(); i++)
        {
            if (regs[i] != INVALID_LFID)
            {
                indices.insert(i);
            }
        }
    }


    out << "Addr  | Fam | Thread | Role      | State / Value" << endl
        << "------+-----+--------+-----------+--------------------------------" << endl;
    for (set<RegIndex>::const_reverse_iterator p = indices.rbegin(); p != indices.rend(); ++p)
    {
        RegAddr  addr = MAKE_REGADDR(type, *p);
        LFID     fid  = regs[*p];
        RegValue value;
        ReadRegister(addr, value, true);

        out << addr << " | ";

        if (fid != INVALID_LFID) out << "F" << setw(2) << setfill('0') << dec << fid; else out << "   ";

        out << " |  ";

        RegClass group = RC_LOCAL;
        TID      tid   = (fid != INVALID_LFID) ? m_allocator.GetRegisterType(fid, addr, &group) : INVALID_TID;
        if (tid != INVALID_TID) {
            out << "T" << setw(4) << setfill('0') << tid;
        } else {
            out << "  -  ";
        }

        out << " | ";

        const char *groupstr = "    -   ";
        switch (group)
        {
            case RC_GLOBAL:    groupstr = "Global"; break;
            case RC_DEPENDENT: groupstr = "Dependent"; break;
            case RC_SHARED:    groupstr = "Shared"; break;
            case RC_LOCAL:
                if (tid != INVALID_TID) groupstr = "Local";
                break;
            case RC_RAZ: break;
        }
        out << setw(9) << setfill(' ') << left << groupstr << right
            << " | "
            << value.str(type)
            << endl;
    }
}

}
