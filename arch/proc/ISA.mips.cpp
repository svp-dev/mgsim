#include "Processor.h"
#include <arch/symtable.h>
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
    {
        addr--;
        if (addr < regs.locals)
        {
            *rc = RC_LOCAL;
            return addr;
        }
    }
    *rc = RC_RAZ;
    return 0;
}


void Processor::Pipeline::DecodeStage::DecodeInstruction(const Instruction& instr)
{
    m_output.Ra     = INVALID_REG;
    m_output.Rb     = INVALID_REG;
    m_output.Rc     = INVALID_REG;

    // FIXME: FILL CODE HERE.

    /* NB: 

       - "instr" is the input of decode, ie the instruction word
       read from memory by the fetch stage. 

       - m_output is the decode-read latch, containing the buffers
       declared in ISA.mips.h and the following "standard" buffers from
       Pipeline.h:

          - uint32_t literal;  <- for instructions that have an "immediate" field
          - RegAddr Ra, Rb, Rc;  <- for register operands: Ra and Rb will be read automatically, Rc written to
    */
       
}


Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecuteInstruction()
{
    /* NB: 

       - m_input is the read-execute latch, containing all buffers
       between the read and execute stages. This has the buffers
       from the decode-read latch, and also the additional buffers declared
       in ISA.mips.h, and also the following from Pipeline.h:

          - PipeValue Rav, Rbv; <- the values read from the register file by the read stage

       - m_output is the execute-memory latch, with the following
       "standard" buffers from Pipeline.h:

          - PipeValue Rcv; <- the register value resulting from execute
          - MemAddr address; <- for memory instructions
          - MemSize size; <- for memory instructions

    */

    uint32_t Rav = m_input.Rav.m_integer.get(m_input.Rav.m_size);
    uint32_t Rbv = m_input.Rbv.m_integer.get(m_input.Rbv.m_size);


    // FIXME: FILL CODE HERE.

    
    return PIPE_CONTINUE;
}

}
