#include "DRISC.h"
#include <sim/config.h>

#include <iomanip>

using namespace std;

namespace Simulator
{
    void DRISC::AncillaryRegisterFile::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
    {
        out << "The ancillary registers hold information common to all threads on a processor.\n";
    }

    void DRISC::AncillaryRegisterFile::Cmd_Read(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
    {
        out << " Register | Value" << endl
            << "----------+----------------------" << endl;

        for (size_t i = 0; i < m_numRegisters; ++i)
        {
            Integer value = ReadRegister(i);
            out << setw(9) << setfill(' ') << right << i << left
                << " | "
                << setw(16 - sizeof(Integer) * 2) << setfill(' ') << ""
                << setw(     sizeof(Integer) * 2) << setfill('0') << hex << right << value << left
                << endl;
        }
    }

    size_t DRISC::AncillaryRegisterFile::GetSize() const
    {
        return m_numRegisters * sizeof(Integer);
    }

    Integer DRISC::AncillaryRegisterFile::ReadRegister(ARAddr addr) const
    {
        if (addr >= m_numRegisters)
        {
            throw exceptf<InvalidArgumentException>(*this, "Invalid ancillary register number: %u", (unsigned)addr);
        }
        return m_registers[addr];
    }

    void DRISC::AncillaryRegisterFile::WriteRegister(ARAddr addr, Integer data)
    {
        if (addr >= m_numRegisters)
        {
            throw exceptf<InvalidArgumentException>(*this, "Invalid ancillary register number: %u", (unsigned)addr);
        }
        m_registers[addr] = data;
    }

    Result DRISC::AncillaryRegisterFile::Read (MemAddr addr, void* data, MemSize size, LFID /*fid*/, TID /*tid*/, const RegAddr& /*writeback*/)
    {
        if (addr % sizeof(Integer) != 0 || (size != sizeof(Integer)) || (addr > m_numRegisters * sizeof(Integer)))
            throw exceptf<InvalidArgumentException>(*this, "Invalid read from ancillary register: %#016llx (%u)", (unsigned long long)addr, (unsigned)size);

        ARAddr raddr = addr / sizeof(Integer);
        Integer value = ReadRegister(raddr);
        COMMIT{
            SerializeRegister(RT_INTEGER, value, data, size);
        }
        return SUCCESS;
    }

    Result DRISC::AncillaryRegisterFile::Write(MemAddr addr, const void *data, MemSize size, LFID /*fid*/, TID /*tid*/)
    {
        if (addr % sizeof(Integer) != 0 || (size != sizeof(Integer)) || (addr > m_numRegisters * sizeof(Integer)))
            throw exceptf<InvalidArgumentException>(*this, "Invalid write to ancillary register: %#016llx (%u)", (unsigned long long)addr, (unsigned)size);

        addr /= sizeof(Integer);
        Integer value = UnserializeRegister(RT_INTEGER, data, size);
        COMMIT{
            WriteRegister(addr, value);
        }
        return SUCCESS;
    }

    DRISC::AncillaryRegisterFile::AncillaryRegisterFile(const std::string& name, Object& parent, Config& config)
        : MMIOComponent(name, parent, parent.GetClock()),
          m_numRegisters(name == "aprs" ? config.getValue<size_t>(*this, "NumAncillaryRegisters") : NUM_ASRS),
          m_registers()
    {
        if (m_numRegisters == 0)
        {
            throw InvalidArgumentException(*this, "NumAncillaryRegisters must be 1 or larger");
        }

        m_registers.resize(m_numRegisters, 0);

        if (name == "asrs")
        {
            WriteRegister(ASR_SYSTEM_VERSION, ASR_SYSTEM_VERSION_VALUE);
        }
    }


}
