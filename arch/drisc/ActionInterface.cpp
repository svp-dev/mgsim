#include "ActionInterface.h"
#include <cctype>
#include <sstream>

namespace Simulator
{

namespace drisc
{

/*
 * MGSim control interface.
 *   address bits:
 *                     x A A -> status/action
 *                     0 - - -> don't print status
 *                     1 - - -> print encoded status before action with cycle/cpu/family/thread identity
 *                     - 0 0 -> continue
 *                     - 0 1 -> interrupt
 *                     - 1 0 -> abort
 *                     - 1 1 -> exit
 *    maximum address: 1 1 1
 */

size_t ActionInterface::GetSize() const { return 8 * sizeof(Integer);  }


Result ActionInterface::Read (MemAddr /*address*/, void* /*data*/, MemSize /*size*/, LFID /*fid*/, TID /*tid*/, const RegAddr& /*writeback*/)
{
    UNREACHABLE;
}

Result ActionInterface::Write(MemAddr address, const void *data, MemSize size, LFID fid, TID tid)
{
    if (address % sizeof(Integer) != 0)
    {
        throw exceptf<SimulationException>(*this, "Invalid action control: %#016llx", (unsigned long long)address);
    }

    address /= sizeof(Integer);
    Integer value = UnserializeRegister(RT_INTEGER, data, size);

    if (address & 4)
    {
        std::ostringstream msg;
        if ((address & 3) < 3)
        {
            // Anything but EXIT
            Integer imsg = value;
            for (unsigned i = 0; i < sizeof(imsg); ++i, imsg >>= 8)
            {
                char byte = imsg & 0xff;
                if (std::isprint(byte)) msg << byte;
            }
        }
        else
        {
            // EXIT
            msg << (value & 0xff);
        }

        const char *actiontype = 0;
        switch(address & 3)
        {
        case 0: actiontype = "CONTINUE"; break;
        case 1: actiontype = "INTERRUPT"; break;
        case 2: actiontype = "ABORT"; break;
        case 3: actiontype = "EXIT"; break;
        }

        DebugProgWrite("F%u/T%u %s: %s",
                       (unsigned)fid, (unsigned)tid, actiontype, msg.str().c_str());
    }

    COMMIT{
        switch(address & 3)
        {
        case 0: /* nothing, continue */ break;
        case 1:
            GetKernel()->Stop();
            break;
        case 2:
            throw ProgramTerminationException(*this, "Program requested simulator to abort.", 0, true);
        case 3:
        {
            int code = value & 0xff;
            std::ostringstream emsg;
            emsg << "Program requested simulator to exit with code " << code << ".";
            throw ProgramTerminationException(*this, emsg.str(), code, false);
        }
        }
    }

    return SUCCESS;
}

ActionInterface::ActionInterface(const std::string& name, Object& parent)
    : MMIOComponent(name, parent, parent.GetClock())
{
}

}
}
