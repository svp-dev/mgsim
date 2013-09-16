#include "Pipeline.h"
#include "DRISC.h"
#include <arch/symtable.h>
#include <cassert>
#include <sstream>
#include <iomanip>
using namespace std;

namespace Simulator
{
namespace drisc
{

// Function for naming local registers according to a standard ABI
const vector<string>& GetDefaultLocalRegisterAliases(RegType type)
{
    static const vector<string> intnames = {
        "sp", "fp", "a0", "a1", "a2", "a3", "a4", "a5",
        "lr", "r10", "rv", "rvh", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22",
        "r23", "r24", "r25", "r26", "r27", "r28", "r29",
        "r30", "r31" };
    static const vector<string> fltnames = {
        "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
        "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
        "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
        "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31" };
    if (type == RT_INTEGER)
        return intnames;
    else
        return fltnames;
}

// Function for getting a register's type and index within that type
unsigned char GetRegisterClass(unsigned char addr, const RegsNo& regs, RegClass* rc, RegType /*type*/)
{
    // $0 is zero, otherwise all registers are local.
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

void Pipeline::DecodeStage::DecodeInstruction(const Instruction& instr)
{
    m_output.Ra     = INVALID_REG;
    m_output.Rb     = INVALID_REG;
    m_output.Rc     = INVALID_REG;

    m_output.literal = 0;

    // FIXME: FILL CODE HERE.

    /* NB:

       - "instr" is the input of decode, ie the instruction word
       read from memory by the fetch stage.

       - m_output is the decode-read latch, containing the buffers
       declared in ISA.mips.h and the following "standard" buffers from
       Pipeline.h:

          - uint32_t literal; <- for instructions that have an
            "immediate" field
          - RegAddr Ra, Rb, Rc; <- for register operands: Ra and Rb
            will be read automatically, Rc written to
    */
}



Pipeline::PipeAction Pipeline::ExecuteStage::ExecuteInstruction()
{
    auto& thread = m_threadTable[m_input.tid];

    // Fetch both potential input buffers.
    uint32_t Rav = m_input.Rav.m_integer.get(m_input.Rav.m_size);
    uint32_t Rbv = m_input.Rbv.m_integer.get(m_input.Rbv.m_size);

    // FIXME: FILL CODE HERE.

    /* NB:

       - m_input is the read-execute latch, containing all buffers
       between the read and execute stages. This has the buffers
       from the decode-read latch, and also the additional buffers declared
       in ISA.mips.h, and also the following from Pipeline.h:

          - PipeValue Rav, Rbv; <- the values read from the register
            file by the read stage

       - m_output is the execute-memory latch, with the following
       "standard" buffers from Pipeline.h:

          - PipeValue Rcv; <- the register value resulting from execute
          - MemAddr address; <- for memory instructions
          - MemSize size; <- for memory instructions

    */


    return PIPE_CONTINUE;

}

}
}
