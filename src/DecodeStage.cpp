#include "Pipeline.h"
#include "Processor.h"
#include <cassert>
#include <sstream>
#include <iomanip>
using namespace std;

namespace Simulator
{

struct IllegalInstruction
{
    IllegalInstruction() {}
};

/**
 \brief This function translates the 32-registers based address into the
        proper physical register file address.

 \param[in] reg     The register index in the thread's virtual context
 \param[in] type    The type of the register
 \param[in] size    The size, in bytes, of the register, to check
                    against overflowing allocated registers
 \param[in] writing Indicates if this register is used in a write
 \returns the physical register address to use for the read or write
 */
RegAddr Pipeline::DecodeStage::TranslateRegister(unsigned char reg, RegType type, unsigned int size, bool writing) const
{
    // We're always dealing with whole registers
    assert(size % sizeof(Integer) == 0);
    unsigned int nRegs = size / sizeof(Integer);
    
    if (nRegs == 0)
    {
        // Don't translate, just return the address as-is.
        // The register index has a special meaning for this instruction.
        return MAKE_REGADDR(type, reg);
    }
    
    const Family::RegInfo& family = m_input.regs.types[type].family;
    const Thread::RegInfo& thread = m_input.regs.types[type].thread;
    
    // Get register class and address within class
    RegClass rc;
    reg = GetRegisterClass(reg, family.count, &rc);
    switch (rc)
    {
        case RC_GLOBAL:
            if (reg + nRegs > family.count.globals)
            {
                throw IllegalInstruction();
            }
            
            /*
             We just suspend on the register if it's empty. The parent
             thread will push the globals to our family, which ensures
             it will come by this register file eventually.
            */
            
            // Use this family's globals
            return MAKE_REGADDR(type, family.base + family.size - family.count.globals + reg);
            
        case RC_SHARED:
            if (reg + nRegs > family.count.shareds || !writing)
            {
                throw IllegalInstruction();
            }
            
            // Use the thread's shareds
            return MAKE_REGADDR(type, thread.shareds + reg);
            
        case RC_LOCAL:
            if (reg + nRegs > family.count.locals)
            {
                throw IllegalInstruction();
            }
            
            // Just use the local register
            return MAKE_REGADDR(type, thread.locals + reg);

        case RC_DEPENDENT:
            if (reg + nRegs > family.count.shareds)
            {
                throw IllegalInstruction();
            }
            
            /*
             If we have a read miss, we just suspend on the empty register.
             Either the parent thread will push the family's first dependents,
             or the last thread in the previous block will push its shareds.
            */
            assert(thread.dependents != INVALID_REG_INDEX);
            return MAKE_REGADDR(type, thread.dependents + reg);

        case RC_RAZ:
            // Read As Zero; nothing to do
            break;
    }
    return MAKE_REGADDR(type, INVALID_REG_INDEX);
}

Pipeline::PipeAction Pipeline::DecodeStage::OnCycle()
{
    COMMIT
    {
        // Copy common latch data
        (CommonData&)m_output = m_input;
        m_output.regs         = m_input.regs;
        m_output.placeSize    = m_input.placeSize;
        m_output.RaNotPending = false;
        
        try
        {
            // Default cases are just naturally-sized operations
            m_output.RaSize = sizeof(Integer);
            m_output.RbSize = sizeof(Integer);
            m_output.RcSize = sizeof(Integer);
            
            DecodeInstruction(m_input.instr);
            
            // Translate registers from window to full register file
            m_output.Ra = TranslateRegister((unsigned char)m_output.Ra.index, m_output.Ra.type, m_output.RaSize, false);
            m_output.Rb = TranslateRegister((unsigned char)m_output.Rb.index, m_output.Rb.type, m_output.RbSize, false);
            m_output.Rc = TranslateRegister((unsigned char)m_output.Rc.index, m_output.Rc.type, m_output.RcSize, true);
#if TARGET_ARCH == ARCH_SPARC
            m_output.Rs = TranslateRegister((unsigned char)m_output.Rs.index, m_output.Rs.type, m_output.RsSize, false);
#endif
        }
        catch (IllegalInstruction&)
        {
            stringstream error;
            error << "Illegal instruction at 0x" << hex << setw(16) << setfill('0') << m_input.pc;
            throw IllegalInstructionException(*this, error.str());
        }
    }
    return PIPE_CONTINUE;
}

Pipeline::DecodeStage::DecodeStage(Pipeline& parent, Clock& clock, const FetchDecodeLatch& input, DecodeReadLatch& output, const Config& /*config*/)
  : Stage("decode", parent, clock),
    m_input(input),
    m_output(output)
{
}

}
