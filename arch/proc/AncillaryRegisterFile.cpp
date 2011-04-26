#include "Processor.h"
#include "sim/config.h"
#include <iomanip>

using namespace std;

namespace Simulator
{
    void Processor::AncillaryRegisterInterface::Cmd_Help(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
    {
        out <<
            "The ancillary registers hold information common to all threads on a processor.\n"
            "They allow for faster (1-cycle) access to commonly used information.\n";
    }

    void Processor::AncillaryRegisterInterface::Cmd_Read(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
    {
        out << " Register | Value" << endl
            << "----------+----------------------" << endl;

        size_t numRegisters = GetNumRegisters();
        for (size_t i = 0; i < numRegisters; ++i)
        {
            Integer value;
            ReadRegister(i, value);
            out << setw(9) << setfill(' ') << right << i << left
                << " | "
                << setw(16 - sizeof(Integer) * 2) << setfill(' ') << ""
                << setw(     sizeof(Integer) * 2) << setfill('0') << hex << value
                << endl;
        }
    }


    bool Processor::AncillaryRegisterFile::ReadRegister(size_t addr, Integer& data) const
    {
        if (addr < m_numRegisters)
        {
            data = m_registers[addr];
            return true;
        }
        else
        {
            return false;
        }
    }
    
    bool Processor::AncillaryRegisterFile::WriteRegister(size_t addr, Integer data)
    {
        if (addr < m_numRegisters)
        {
            m_registers[addr] = data;
            return true;
        }
        else
        {
            return false;
        }
    }


    Processor::AncillaryRegisterFile::AncillaryRegisterFile(const std::string& name, Processor& parent, Clock& clock, const Config& config)
        : AncillaryRegisterInterface(name, parent, clock),
          m_numRegisters(config.getValue<size_t>("NumAncillaryRegisters", 8))
    {
        if (m_numRegisters == 0)
        {
            throw InvalidArgumentException("NumAncillaryRegisters must be 1 or larger");
        }

        m_registers.resize(m_numRegisters, 0);
    }


}
