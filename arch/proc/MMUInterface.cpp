#include "Processor.h"
#include <programs/mgsim.h>

#include <iomanip>

namespace Simulator
{

/*
 * MMU interface
 * word address bits:  0 0 L L L -> map memory range with given size/address, using PID 0
 *                     0 1 L L L -> unmap memory range at given size/address, any PID
 *                     1 0 L L L -> map memory range with given size/address, using PID taken from ASR
 *                     1 1 0 0 0 -> unmap all memory ranges with given PID
 *                     1 1 0 0 1 -> write given PID to ASR
 *    maximum address: 1 1 0 1 0
 */

size_t Processor::MMUInterface::GetSize() const { return 0x1A /* 11010 */ * sizeof(Integer);  }


Result Processor::MMUInterface::Read (MemAddr /*address*/, void* /*data*/, MemSize /*size*/, LFID /*fid*/, TID /*tid*/, const RegAddr& /*writeback*/)
{
    assert(0); // should not be here
    return FAILED;
}

Result Processor::MMUInterface::Write(MemAddr address, const void *data, MemSize size, LFID fid, TID tid)
{
    if (address % sizeof(Integer) != 0)
    {
        throw exceptf<SimulationException>(*this, "Invalid MMU configuration access: %#016llx (%u)", (unsigned long long)address, (unsigned)size);
    }

    
    Integer value = UnserializeRegister(RT_INTEGER, data, size);

    address /= sizeof(Integer);
    unsigned cmd = address >> 3;
    unsigned l = address & 0x7;
    MemSize req_size = 1 << (l + 12);

    DebugIOWrite("MMU configuration F%u/T%u: %#016llx (%llu), cmd %u, size %llu",
                 (unsigned)fid, (unsigned)tid,
                 (unsigned long long)value, (unsigned long long)value,
                 (unsigned)cmd, (unsigned long long)req_size);

    Processor* cpu = static_cast<Processor*>(GetParent());

    COMMIT{
        switch(cmd)
        {
        case 0:
            cpu->MapMemory(value, req_size, 0); break;
        case 1:
            cpu->UnmapMemory(value, req_size); break;
        case 2:
            cpu->MapMemory(value, req_size, cpu->ReadASR(ASR_PID)); break;
        case 3:
            if (req_size == 0)
                cpu->UnmapMemory(value);
            else if (req_size == 1)
                cpu->WriteASR(ASR_PID, value);
            break;
        default:
            assert(0);
            break;
        }
    }

    return SUCCESS;
}

Processor::MMUInterface::MMUInterface(const std::string& name, Object& parent)
    : Processor::MMIOComponent(name, parent, parent.GetClock())
{
}

}
