#include "RegisterFile.h"
#include "DRISC.h"
#include <sim/config.h>
#include <sim/range.h>

#include <cassert>
#include <iomanip>
#include <array>

using namespace std;

namespace Simulator
{
namespace drisc
{

//
// RegisterFile implementation
//

RegisterFile::RegisterFile(const std::string& name, DRISC& parent, Clock& clock)
  : Object(name, parent),
    ReadWriteStructure<RegAddr>(name, parent, clock),
    Storage("storage", *this, clock),
    p_pipelineR1(*this),
    p_pipelineR2(*this),
    p_pipelineW (*this),
    p_asyncR    (*this, GetName() + ".p_asyncR"),
    p_asyncW    (*this, GetName() + ".p_asyncW"),
    m_files     (),
    m_sizes     (),
    m_nUpdates(0),
    m_local_aliases()
{
    // Initialize all registers
    for (size_t i = 0; i < NUM_REG_TYPES; ++i)
    {
        static constexpr std::array<const char*, NUM_REG_TYPES> cfg_names = { {"NumIntRegisters", "NumFltRegisters"} };
        m_sizes[i] = GetConf(cfg_names[i], size_t);
        m_files[i] = new RegValue[m_sizes[i]];
        for (RegSize j = 0; j < m_sizes[i]; ++j)
        {
            m_files[i][j] = MAKE_EMPTY_REG();
        }
    }
    // Set write port priorities (from ReadWriteStructure); first port has highest priority
    AddPort(p_pipelineW);
    AddPort(p_asyncW);

    // Register aliases for debugging
    for (size_t i = 0; i < NUM_REG_TYPES; ++i)
    {
        static constexpr std::array<const char *, NUM_REG_TYPES> cfg_names = { {"IntRegAliases", "FltRegAliases"} };
        m_local_aliases[i] = GetTopConfStrings(cfg_names[i]);
        if (m_local_aliases[i].empty())
            m_local_aliases[i] = GetDefaultLocalRegisterAliases((RegType)i);
    }
}

RegisterFile::~RegisterFile()
{
    for (auto p : m_files)
        delete[] p;
}

bool RegisterFile::ReadRegister(const RegAddr& addr, RegValue& data, bool quiet) const
{
    auto& regs = m_files[addr.type];
    auto sz = m_sizes[addr.type];
    if (addr.index >= sz)
    {
        throw SimulationException("A component attempted to read from a non-existing register", *this);
    }
    data = regs[addr.index];

    if (!quiet)
        DebugRegWrite("read  %s -> %s", addr.str().c_str(), data.str(addr.type).c_str());

    return true;
}

// Admin version
bool RegisterFile::WriteRegister(const RegAddr& addr, const RegValue& data)
{
    auto& regs = m_files[addr.type];
    auto sz = m_sizes[addr.type];
    if (addr.index < sz)
    {
        DebugRegWrite("write %s <- %s (was %s, ADMIN)", addr.str().c_str(),
                      data.str(addr.type).c_str(),
                      regs[ addr.index ].str(addr.type).c_str());
        regs[addr.index] = data;
        return true;
    }
    return false;
}

bool RegisterFile::Clear(const RegAddr& addr, RegSize size)
{
    auto& regs = m_files[addr.type];
    auto sz = m_sizes[addr.type];
    if (addr.index + size > sz)
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

bool RegisterFile::WriteRegister(const RegAddr& addr, const RegValue& data, bool from_memory)
{
    auto& regs = m_files[addr.type];
    auto sz = m_sizes[addr.type];
    if (addr.index >= sz)
    {
        throw SimulationException("A component attempted to write to a non-existing register", *this);
    }

    assert(data.m_state == RST_EMPTY || data.m_state == RST_PENDING || data.m_state == RST_WAITING || data.m_state == RST_FULL);

    if (data.m_state == RST_EMPTY || data.m_state == RST_PENDING)
    {
        assert(data.m_waiting.head == INVALID_TID);
    }

    auto& value = regs[addr.index];
    if (value.m_state != RST_FULL)
    {
        if (value.m_state == RST_WAITING && data.m_state == RST_EMPTY)
        {
            throw exceptf<>(*this, "Invalid reset of %s: %s becomes %s", addr.str().c_str(), value.str(addr.type).c_str(), data.str(addr.type).c_str());
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
                    throw exceptf<>(*this, "Invalid reset of pending load %s: %s becomes %s", addr.str().c_str(), value.str(addr.type).c_str(), data.str(addr.type).c_str());
                }
            }
        }

        if (data.m_state == RST_FULL)
        {
            if (value.m_state == RST_WAITING && value.m_waiting.head != INVALID_TID)
            {
                // This write caused a reschedule
                auto& alloc = GetDRISC().GetAllocator();
                if (!alloc.ActivateThreads(value.m_waiting))
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

void RegisterFile::Update()
{
    // Commit the queued updates to registers
    assert(m_nUpdates > 0);
    for (unsigned i = 0; i < m_nUpdates; ++i)
    {
        auto& addr = m_updates[i].first;
        auto type = addr.type;
        auto& regs = m_files[type];

        DebugRegWrite("write %s <- %s (was %s)", addr.str().c_str(),
                      m_updates[i].second.str(type).c_str(),
                      regs[ addr.index ].str(type).c_str());

        regs[ addr.index ] = m_updates[i].second;
    }
    m_nUpdates = 0;
}

void RegisterFile::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
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


// IFPUClient::GetName()
const std::string& RegisterFile::GetName() const
{
    return GetParent()->GetName();
}

bool RegisterFile::CheckFPUOutputAvailability(RegAddr addr)
{
    if (!p_asyncW.Write(addr))
    {
        DeadlockWrite("Unable to acquire port to write back to %s", addr.str().c_str());
        return false;
    }

    // Read the old value
    RegValue value;
    if (!ReadRegister(addr, value))
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

    return true;
}

bool RegisterFile::WriteFPUResult(RegAddr addr, const RegValue& value)
{
    return WriteRegister(addr, value, false);
}

void RegisterFile::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    // Need to change this if the RF is not directly child of parent
    auto& cpu = GetDRISC();

    // Find the RAUnit in the same processor
    auto& rau = cpu.GetRAUnit();

    RegType type = RT_INTEGER;
    size_t  ix    = 0;
    if (!arguments.empty())
    {
        if (arguments[ix] == "float") {
            type = RT_FLOAT;
            ix++;
        } else if (arguments[ix] == "integer") {
            // already initialized above
            ix++;
        }
    }
    auto& aliases = m_local_aliases[type];

    auto regs = rau.GetBlockInfo(type);

    set<RegIndex> indices;
    if (ix < arguments.size())
    {
        indices = parse_range<RegIndex>(arguments[ix], 0, regs.size());
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

    auto& alloc = GetDRISC().GetAllocator();

    out << "Phy   | Fam | Thread | Name/Alias | State / Value" << endl
        << "------+-----+--------+------------+--------------------------------" << endl;
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
        size_t   rel   = 0;
        TID      tid   = (fid != INVALID_LFID) ? alloc.GetRegisterType(fid, addr, &group, &rel) : INVALID_TID;
        if (tid != INVALID_TID) {
            out << "T" << setw(4) << setfill('0') << tid;
        } else {
            out << "  -  ";
        }

        out << " | " << setfill(' ');

        char groupc = '\0';
        switch (group)
        {
        case RC_GLOBAL:    groupc = 'g'; break;
        case RC_DEPENDENT: groupc = 'd'; break;
        case RC_SHARED:    groupc = 's'; break;
        case RC_LOCAL:     groupc = 'l'; break;
        case RC_RAZ:       break;
        }

        if (tid != INVALID_TID)
        {
            if (type == RT_FLOAT)
                out << groupc << 'f' << setw(2) << left << rel << right;
            else
                out << ' ' << groupc << setw(2) << left << rel << right;
            out << ' ';
            if (group == RC_LOCAL && rel < aliases.size())
                out << setw(5) << aliases[rel];
            else
                out << "   - ";
        }
        else
            out << "  -     - ";

        out << " | "
            << value.str(type)
            << endl;
    }
}

}
}
