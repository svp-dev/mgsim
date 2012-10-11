#include "Processor.h"
#include <sim/config.h>

#include <iomanip>

using namespace std;

namespace Simulator
{
    void Processor::AncillaryRegisterFile::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
    {
        out <<
            "The ancillary registers hold information common to all threads on a processor.\n"
            "They allow for faster (1-cycle) access to commonly used information.\n";
    }

    void Processor::AncillaryRegisterFile::Cmd_Read(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
    {
        out << " Register | Value" << endl
            << "----------+----------------------" << endl;

        size_t numRegisters = GetNumRegisters();
        for (size_t i = 0; i < numRegisters; ++i)
        {
            Integer value = ReadRegister(i);
            out << setw(9) << setfill(' ') << right << i << left
                << " | "
                << setw(16 - sizeof(Integer) * 2) << setfill(' ') << ""
                << setw(     sizeof(Integer) * 2) << setfill('0') << hex << right << value << left
                << endl;
        }
    }


    Integer Processor::AncillaryRegisterFile::ReadRegister(ARAddr addr) const
    {
        if (addr >= m_numRegisters)
        {
            throw exceptf<InvalidArgumentException>(*this, "Invalid ancillary register number: %u", (unsigned)addr);
        }
        return m_registers[addr];
    }

    void Processor::AncillaryRegisterFile::WriteRegister(ARAddr addr, Integer data)
    {
        if (addr >= m_numRegisters)
        {
            throw exceptf<InvalidArgumentException>(*this, "Invalid ancillary register number: %u", (unsigned)addr);
        }
        m_registers[addr] = data;
    }


    Processor::AncillaryRegisterFile::AncillaryRegisterFile(const std::string& name, Processor& parent, Clock& clock, Config& config)
        : Object(name, parent, clock),
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
