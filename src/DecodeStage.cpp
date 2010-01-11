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

// This function translates the 32-registers based address into the proper
// register file address.
RegAddr Pipeline::DecodeStage::TranslateRegister(unsigned char reg, RegType type, unsigned int size, RemoteRegAddr* remoteReg, bool writing) const
{
    // We're always dealing with whole registers
    assert(size % sizeof(Integer) == 0);
    unsigned int nRegs = size / sizeof(Integer);
    
    // Default: no remote access
    remoteReg->fid = INVALID_LFID;
    
    const Family::RegInfo& family = m_input.regs.types[type].family;
    const Thread::RegInfo& thread = m_input.regs.types[type].thread;

    // Get register class and address within class
    RegClass rc;
    reg = GetRegisterClass(reg, family.count, &rc);
    switch (rc)
    {
        case RC_GLOBAL:
        {
            if (reg + nRegs > family.count.globals)
            {
                throw IllegalInstruction();
            }
            
            // Default is the global cache
            RegIndex base = family.base + family.size - family.count.shareds - family.count.globals;
            
            if (family.parent_globals != INVALID_REG_INDEX)
            {
                // The actual globals are on this (the parent) core                
                base = family.parent_globals;
                
                // This cannot be a delegated create
                assert(m_input.parent_gpid == INVALID_GPID);
            }
            else if (m_input.onParent && m_input.parent_gpid != INVALID_GPID)
            {
                // We're on the local parent core of a delegated create.
                // Request from remote parent.
                remoteReg->gpid = m_input.parent_gpid;
                remoteReg->lpid = INVALID_LPID;
                remoteReg->fid  = m_input.parent_fid;
                remoteReg->type = RRT_GLOBAL;
                remoteReg->reg  = MAKE_REGADDR(type, reg);
            }
            else
            {
                // We're on a non-parent core in a group or delegated create.
                // Request from previous in group.
                remoteReg->gpid = INVALID_GPID;
                remoteReg->lpid = INVALID_LPID;
                remoteReg->fid  = m_input.link_prev;
                remoteReg->type = RRT_GLOBAL;
                remoteReg->reg  = MAKE_REGADDR(type, reg);
            }
            
            return MAKE_REGADDR(type, base + reg);
        }
            
        case RC_SHARED:
            if (reg + nRegs > family.count.shareds)
            {
                throw IllegalInstruction();
            }
            
            if (m_input.isLastThreadInFamily || m_input.isLastThreadInBlock)
            {
                // We may need to do something special
                if (m_input.isLastThreadInFamily && m_input.onParent)
                {
                    // We map to the parent shareds (unless it's a delegated create)
                    if (m_input.parent_gpid == INVALID_GPID)
                    {
                        // We should write back to the parent shareds on our own CPU
                        assert(family.parent_shareds != INVALID_REG_INDEX);
                        return MAKE_REGADDR(type, family.parent_shareds + reg);
                    }
                    
                    // Last thread in a delegated create
                    if (writing)
                    {
                        // We should send the shareds back remotely
                        remoteReg->gpid = m_input.parent_gpid;
                        remoteReg->lpid = INVALID_LPID;
                        remoteReg->fid  = m_input.parent_fid;
                        remoteReg->type = RRT_PARENT_SHARED;
                        remoteReg->reg  = MAKE_REGADDR(type, reg);
                    }
                }
                else
                {
                    // This is the last thread in the block, set the remoteReg as well,
                    // because we need to forward the shared value to the next CPU
                    // Obviously, this isn't necessary for local families
                    if (writing && m_input.link_next != INVALID_LFID)
                    {
                        remoteReg->gpid = INVALID_GPID;
                        remoteReg->fid  = (m_input.isLastThreadInFamily) ? m_input.parent_fid  : m_input.link_next;
                        remoteReg->type = (m_input.isLastThreadInFamily) ? RRT_PARENT_SHARED   : RRT_FIRST_DEPENDENT;
                        remoteReg->lpid = (m_input.isLastThreadInFamily) ? m_input.parent_lpid : INVALID_LPID;
                        remoteReg->reg  = MAKE_REGADDR(type, reg);
                    }
                }
            }
            
            return MAKE_REGADDR(type, thread.base + reg);
            
        case RC_LOCAL:
            if (reg + nRegs > family.count.locals)
            {
                throw IllegalInstruction();
            }
            
            return MAKE_REGADDR(type, thread.base + family.count.shareds + reg);

        case RC_DEPENDENT:
            // Note that we can NEVER ever write to dependent registers
            if (reg + nRegs > family.count.shareds || writing)
            {
                throw IllegalInstruction();
            }
            
            if (thread.producer != INVALID_REG_INDEX)
            {
                // Grab dependent from the same core
                return MAKE_REGADDR(type, thread.producer + reg);
            }
            
            if (m_input.isFirstThreadInFamily && m_input.parent_gpid != INVALID_GPID && m_input.onParent)
            {
                // First thread in a delegated create on the parent core; get the parent shared remotely
                remoteReg->gpid = m_input.parent_gpid;
                remoteReg->lpid = INVALID_LPID;
                remoteReg->fid  = m_input.parent_fid;
                remoteReg->type = RRT_PARENT_SHARED;
                remoteReg->reg  = MAKE_REGADDR(type, reg);
            }
            else
            {
                // Get the shared from the previous processor in the group
                remoteReg->gpid = INVALID_GPID;
                remoteReg->lpid = INVALID_LPID;
                remoteReg->fid  = m_input.link_prev;
                remoteReg->type = (m_input.isFirstThreadInFamily) ? RRT_PARENT_SHARED : RRT_LAST_SHARED;
                remoteReg->reg  = MAKE_REGADDR(type, reg);
            }
            
            return MAKE_REGADDR(type, family.base + family.size - family.count.shareds + reg);

        case RC_RAZ:
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
        
        try
        {
            // Default cases are just naturally-sized operations
            m_output.RaSize = sizeof(Integer);
            m_output.RbSize = sizeof(Integer);
            m_output.RcSize = sizeof(Integer);

            DecodeInstruction(m_input.instr);
            
            // Translate registers from window to full register file
            m_output.Ra = TranslateRegister((unsigned char)m_output.Ra.index, m_output.Ra.type, m_output.RaSize, &m_output.Rra, false);
            m_output.Rb = TranslateRegister((unsigned char)m_output.Rb.index, m_output.Rb.type, m_output.RbSize, &m_output.Rrb, false);
            m_output.Rc = TranslateRegister((unsigned char)m_output.Rc.index, m_output.Rc.type, m_output.RcSize, &m_output.Rrc, true);
#if TARGET_ARCH == ARCH_SPARC
            m_output.Rs = TranslateRegister((unsigned char)m_output.Rs.index, m_output.Rs.type, m_output.RsSize, &m_output.Rrs, false);
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

Pipeline::DecodeStage::DecodeStage(Pipeline& parent, const FetchDecodeLatch& input, DecodeReadLatch& output, const Config& /*config*/)
  : Stage("decode", parent),
    m_input(input),
    m_output(output)
{
}

}
