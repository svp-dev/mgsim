#include "Processor.h"
#include <sim/config.h>
#include <iomanip>

namespace Simulator
{

Processor& Processor::IOMatchUnit::GetProcessor() const
{
    return *static_cast<Processor*>(GetParent());
}

void Processor::MMIOComponent::Connect(IOMatchUnit& mmio, IOMatchUnit::AccessMode mode, Config& config)
{
    // we allow the base address to be set to zero to indicate it should not be mapped.
    MemAddr base = config.getValueOrDefault<MemAddr>(*this, "MMIO_BaseAddr", 0);
    if (base == 0)
    {
        std::cerr << "warning: " << GetFQN() << " not mapped to the I/O address space." << std::endl;
    }
    else
    {
        mmio.RegisterComponent(base, mode, *this);
    }
}

void
Processor::IOMatchUnit::RegisterComponent(MemAddr address, AccessMode mode, MMIOComponent& component)
{
    MemSize size = component.GetSize();
    assert(size > 0);

    // Check that there is no overlap
    RangeMap::iterator p = m_ranges.lower_bound(address);
    if (p != m_ranges.end())
    {
        if (p->first == address || (address < p->first && address + size > p->first))
        {
            // The range overlaps with an existing range after it
            throw exceptf<InvalidArgumentException>("Overlap in I/O reservation (%#016llx, %zd)",
                                                    (unsigned long long)address, (size_t)size);
        }

        if (p != m_ranges.begin())
        {
            RangeMap::iterator q = p; --q;
            if (q->first < address && q->first > address - q->second.size)
            {
                // The range overlaps with an existing range before it
                throw exceptf<InvalidArgumentException>("Overlap in I/O reservation (%#016llx, %zd)",
                                                        (unsigned long long)address, (size_t)size);
            }

        }
    }

    ComponentInterface ci;
    ci.size = size;
    ci.mode = mode;
    ci.component = &component;
    m_ranges.insert(p, std::make_pair(address, ci));
}

Processor::IOMatchUnit::RangeMap::const_iterator
Processor::IOMatchUnit::FindInterface(MemAddr address, MemSize size) const
{
    RangeMap::const_iterator p = m_ranges.lower_bound(address);
    if (p != m_ranges.begin() && (p == m_ranges.end() || p->first > address))
    {
        --p;
    }
    if (p != m_ranges.end() &&
        address >= p->first && p->second.size >= size &&
        address <= p->first + (p->second.size - size))
    {
        return p;
    }
    else
    {
        return m_ranges.end();
    }
}

bool Processor::IOMatchUnit::IsRegisteredReadAddress(MemAddr address, MemSize size) const
{
    RangeMap::const_iterator interface = FindInterface(address, size);
    return (interface != m_ranges.end() &&
            ((int)interface->second.mode & 1) != 0);
}

bool Processor::IOMatchUnit::IsRegisteredWriteAddress(MemAddr address, MemSize size) const
{
    RangeMap::const_iterator interface = FindInterface(address, size);
    return (interface != m_ranges.end() &&
            ((int)interface->second.mode & 2) != 0);
}

Result Processor::IOMatchUnit::Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback)
{
    RangeMap::const_iterator interface = FindInterface(address, size);
    assert(interface != m_ranges.end());
    assert(interface->second.mode == READ || interface->second.mode == READWRITE);

    MemAddr base = interface->first;
    MemAddr offset = address - base;

    return interface->second.component->Read(offset, data, size, fid, tid, writeback);
}

Result Processor::IOMatchUnit::Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid)
{
    RangeMap::const_iterator interface = FindInterface(address, size);
    assert(interface != m_ranges.end());
    assert(interface->second.mode == WRITE || interface->second.mode == READWRITE);

    MemAddr base = interface->first;
    MemAddr offset = address - base;

    return interface->second.component->Write(offset, data, size, fid, tid);
}

void Processor::IOMatchUnit::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
        "The memory-mapped I/O interface intercepts memory operations at the memory stage\n"
        "in the pipeline and redirects them to a processor-local I/O bus.\n\n"
        "Components recognized by this interface:\n";

    RangeMap::const_iterator p = m_ranges.begin();

    if (p == m_ranges.end())
    {
        out << "(no component defined)" << std::endl;
    }
    else
    {
        out << "Start address    | End address      | RW | Name" << std::endl
            << "-----------------+------------------+----+--------------------" << std::endl
            << std::hex << std::setfill('0');

        for ( ;
             p != m_ranges.end();
             ++p)
        {
            MemAddr begin = p->first;
            MemAddr size = p->second.size;
            AccessMode mode = p->second.mode;
            MMIOComponent &component = *p->second.component;

            out << std::setw(16) << begin
                << " | "
                << std::setw(16) << begin + size - 1
                << " | "
                << (mode & READ  ? "R" : ".")
                << (mode & WRITE ? "W" : ".")
                << " | "
                << component.GetFQN()
                << std::endl;
        }
    }
}

Processor::IOMatchUnit::IOMatchUnit(const std::string& name, Processor& parent, Clock& clock)
    : Object(name, parent, clock), m_ranges()
{
}

Processor::MMIOComponent::MMIOComponent(const std::string& name, Object& parent, Clock& clock)
    : Object(name, parent, clock)
{ }

}
