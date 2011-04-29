#include "Processor.h"
#include <iomanip>

namespace Simulator
{

/*
 * Line printer device.
 * - offset 0*word, size 1:    output byte
 * - offset 1*word, size word: output unsigned decimal
 * - offset 2*word, size word: output signed decimal
 * - offset 3*word, size word: output unsigned hex
 * - offset 4*word, size 1:    set float precision
 * - offset 5*word, size word: output float decimal
 */

size_t Processor::DebugChannel::GetSize() const { return  6 * sizeof(Integer);  }


Result Processor::DebugChannel::Write(MemAddr address, const void *data, MemSize size, LFID fid, TID tid)
{
    address /= sizeof(Integer);

    if (size != 1 && size != sizeof(Integer))
        return SUCCESS;

    COMMIT{
    
        Integer value = UnserializeRegister(RT_INTEGER, data, size);
        Float   floatval;
        floatval.integer = value;
        
        switch (address)
        {
        case 0:
            if (std::isprint((char)value))
                m_output << (char)value;
            else if ((char)value == '\n')
                m_output << std::endl;
            break;
        case 1:
            m_output << std::dec << value;
            break;
        case 2:
            m_output << std::dec << (SInteger)value;
            break;
        case 3:
            m_output << std::hex << (Integer)value;
            break;
        case 4:
            m_floatprecision = value;
            break;
        case 5:
            m_output << std::setprecision(m_floatprecision) << std::scientific << floatval.tofloat();
            break;
        }
        
        m_output.flush();
        
        
        DebugIOWrite("Debug output by F%u/T%u: %#016llx (%llu) -> channel %u",
                     (unsigned)fid, (unsigned)tid, 
                     (unsigned long long)value, (unsigned long long)value,
                     (unsigned)address);
    }
    return SUCCESS;
}

Processor::DebugChannel::DebugChannel(const std::string& name, Object& parent, std::ostream& output)
    : Processor::MMIOComponent("debug_" + name, parent, parent.GetClock()),
      m_output(output),
      m_floatprecision(6)
{
}

}
