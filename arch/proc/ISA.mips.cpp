#include "Processor.h"
#include "symtable.h"
#include <cassert>
#include <sstream>
#include <iomanip>
using namespace std;

namespace Simulator
{
// Function for getting a register's type and index within that type
unsigned char GetRegisterClass(unsigned char addr, const RegsNo& regs, RegClass* rc)
{
    // DO NOT CHANGE THIS
    if (addr > 0)
        *rc = RC_LOCAL;
    else
        *rc = RC_RAZ;
    return addr;
}


void Processor::Pipeline::DecodeStage::DecodeInstruction(const Instruction& instr)
{
    m_output.Ra     = INVALID_REG;
    m_output.Rb     = INVALID_REG;
    m_output.Rc     = INVALID_REG;

    // FIXME: FILL CODE HERE.
}


Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecuteInstruction()
{
    uint32_t Rav = m_input.Rav.m_integer.get(m_input.Rav.m_size);
    uint32_t Rbv = m_input.Rbv.m_integer.get(m_input.Rbv.m_size);

    // FIXME: FILL CODE HERE.
    
    return PIPE_CONTINUE;
}

}
