#include <cassert>
#include <sstream>
#include <iomanip>
#include "ISA.h"
#include "Pipeline.h"
#include "Processor.h"
using namespace Simulator;
using namespace std;

static const int A_OPCODE_SHIFT   = 26;
static const int A_RA_SHIFT       = 21;
static const int A_RB_SHIFT       = 16;
static const int A_INT_FUNC_SHIFT = 5;
static const int A_FLT_FUNC_SHIFT = 5;
static const int A_RC_SHIFT       = 0;
static const int A_MEMDISP_SHIFT  = 0;
static const int A_BRADISP_SHIFT  = 0;
static const int A_PALCODE_SHIFT  = 0;
static const int A_LITERAL_SHIFT  = 13;

static const uint32_t A_OPCODE_MASK   = 0x3F;
static const uint32_t A_REG_MASK      = 0x1F;
static const uint32_t A_MEMDISP_MASK  = 0x000FFFF;
static const uint32_t A_BRADISP_MASK  = 0x01FFFFF;
static const uint32_t A_PALCODE_MASK  = 0x3FFFFFF;
static const uint32_t A_INT_FUNC_MASK = 0x7F;
static const uint32_t A_FLT_FUNC_MASK = 0x7FF;
static const uint32_t A_LITERAL_MASK  = 0xFF;

InstrFormat Pipeline::DecodeStage::getInstrFormat(uint8_t opcode)
{
    if (opcode <= 0x3F)
    {
        switch (opcode >> 4)
        {
            case 0x0:
                switch (opcode & 0xF) {
                    case 0x0: return IFORMAT_PAL;
					case 0x1: return IFORMAT_OP;
					case 0x2: return IFORMAT_SPECIAL;
					case 0x3: return IFORMAT_JUMP;
					case 0x4: return IFORMAT_BRA;
					case 0x5: return IFORMAT_FPOP;
					case 0x6: return IFORMAT_SPECIAL;   // Remove when SETREGS becomes obsolete
					case 0x7: return IFORMAT_INVALID;
                    case 0x8:
                    case 0x9:
                    case 0xA:
                    case 0xB:
                    case 0xC: return IFORMAT_MEM_LOAD;
                    case 0xD:
                    case 0xE:
                    case 0xF: return IFORMAT_MEM_STORE;
                }
                break;

            case 0x1:
                switch (opcode & 0xF) {
                    case 0x0:
                    case 0x1:
                    case 0x2:
                    case 0x3: return IFORMAT_OP;
                    case 0x4:
                    case 0x5:
                    case 0x6:
                    case 0x7: return IFORMAT_FPOP;
                    case 0x8: return IFORMAT_MISC;
                    case 0xA: return IFORMAT_JUMP;
                    case 0xC: return IFORMAT_OP;
                }
                break;

            case 0x2: return ((opcode % 8) < 4 ? IFORMAT_MEM_LOAD : IFORMAT_MEM_STORE);
            case 0x3: return IFORMAT_BRA;
        }
    }
    return IFORMAT_INVALID;
}

// This function translates the 32-registers based address into the proper
// register file address.
RegAddr Pipeline::DecodeStage::translateRegister(uint8_t reg, RegType type, RemoteRegAddr* remoteReg) const
{
    // Default: no remote access
    remoteReg->fid = INVALID_GFID;

    if (reg < 31)
    {
        const Family::RegInfo& family = m_input.familyRegs[type];
        const Thread::RegInfo& thread = m_input.threadRegs[type];

        // Check if it's a global
        if (reg < family.count.globals)
        {
            return MAKE_REGADDR(type, family.globals + reg);
        }
        reg -= family.count.globals;
        RegIndex base = thread.base;

        // Check if it's a shared
        if (reg < family.count.shareds)
        {
            if (m_input.isLastThreadInBlock || m_input.isLastThreadInFamily)
            {
                // Remote write
				if (m_input.isLastThreadInFamily && m_input.onParent)
                {
                    // We should write back to the parent shareds on our own CPU
                    return MAKE_REGADDR(type, m_input.familyRegs[type].shareds + reg);
                }

				if (m_input.gfid != INVALID_GFID)
				{
                    // This is the last thread in the block, set the remoteReg as well,
                    // because we need to forward the shared value to the next CPU
                    // Obviously, this isn't necessary for local families
                    remoteReg->reg = MAKE_REGADDR(type, reg);
                    remoteReg->fid = m_input.gfid;
                }
            }
            return MAKE_REGADDR(type, base + reg);
        }
        reg  -= family.count.shareds;
        base += family.count.shareds;

        // Check if it's a local
        if (reg < family.count.locals)
        {
            return MAKE_REGADDR(type, base + reg);
        }
        reg  -= family.count.locals;
        base += family.count.locals;

        // Check if it's a dependent
        if (reg < family.count.shareds)
        {
            if (thread.producer != INVALID_REG_INDEX)
            {
                // It's a local dependency
                return MAKE_REGADDR(type, thread.producer + reg);
            }

            // It's a remote dependency
            remoteReg->reg = MAKE_REGADDR(type, reg);
            remoteReg->fid = m_input.gfid;
            return MAKE_REGADDR(type, family.base + family.size - family.count.shareds + reg);
        }
    }

    return MAKE_REGADDR(type, INVALID_REG_INDEX);
}

Pipeline::PipeAction Pipeline::DecodeStage::read()
{
    // This stage has no external dependencies, so all the work is done
    // in the write phase
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::DecodeStage::write()
{
    COMMIT
    {
        // Copy common latch data
        (CommonLatch&)m_output = m_input;
        
		m_output.fpcr    = m_input.fpcr;
        m_output.opcode  = (m_input.instr >> A_OPCODE_SHIFT) & A_OPCODE_MASK;
        m_output.format  = getInstrFormat(m_output.opcode);
        RegIndex Ra      = (m_input.instr >> A_RA_SHIFT) & A_REG_MASK;
        RegIndex Rb      = (m_input.instr >> A_RB_SHIFT) & A_REG_MASK;
        RegIndex Rc      = (m_input.instr >> A_RC_SHIFT) & A_REG_MASK;

        for (RegType i = 0; i < NUM_REG_TYPES; i++)
        {
            m_output.familyRegs[i] = m_input.familyRegs[i];
            m_output.threadRegs[i] = m_input.threadRegs[i];
        }

        // It is important that this is zero by default. If Rb is $R31, this value
        // will be used instead of RegFile[Rb]
        m_output.literal = 0;

        switch (m_output.format)
        {
        case IFORMAT_PAL:
            m_output.literal = (m_input.instr >> A_PALCODE_SHIFT) & A_PALCODE_MASK;
            m_output.Ra      = MAKE_REGADDR(RT_INTEGER, 31);
            m_output.Rb      = MAKE_REGADDR(RT_INTEGER, 31);
            m_output.Rc      = MAKE_REGADDR(RT_INTEGER, 31);
            break;

        case IFORMAT_MISC:
        case IFORMAT_MEM_LOAD:
        case IFORMAT_MEM_STORE:
        {
            uint32_t disp  = (m_input.instr >> A_MEMDISP_SHIFT) & A_MEMDISP_MASK;
            RegType  type  = (m_output.opcode >= 0x20 && m_output.opcode <= 0x27) ? RT_FLOAT : RT_INTEGER;

            m_output.Rc           = MAKE_REGADDR(type, (m_output.format == IFORMAT_MEM_LOAD) ? Ra : 31);
            m_output.Ra           = MAKE_REGADDR(type, (m_output.format == IFORMAT_MEM_LOAD) ? 31 : Ra);
            m_output.Rb           = MAKE_REGADDR(RT_INTEGER, Rb);
            m_output.displacement = (m_output.opcode == A_OP_LDAH) ? (int32_t)(disp << 16) : (int16_t)disp;
            m_output.function     = (uint16_t)disp;
            
            if (m_output.opcode == A_OP_LDQ_U && disp == 0 && Ra == 31)
            {
                // This is a UNOP. Rb can be anything, but that might cause problems, so we force Rb to R31
                m_output.Rb = MAKE_REGADDR(type, 31);
            }
            break;
        }

        case IFORMAT_BRA:
		{
			uint32_t disp = (m_input.instr >> A_BRADISP_SHIFT) & A_BRADISP_MASK;
			RegType  type = (m_output.opcode > 0x30 && m_output.opcode < 0x38 && m_output.opcode != 0x34) ? RT_FLOAT : RT_INTEGER;
			if (m_output.opcode != A_OP_CREATE_D)
			{
				// Unconditional branches write the PC back to Ra.
				// Conditional branches test Ra.
				bool conditional = (m_output.opcode != A_OP_BR && m_output.opcode != A_OP_BSR);

				m_output.Ra = MAKE_REGADDR(type, (conditional) ? Ra : 31);
				m_output.Rc = MAKE_REGADDR(type, (conditional) ? 31 : Ra);
			}
			else
			{
				// Create reads from and writes to Ra
				m_output.Ra = MAKE_REGADDR(RT_INTEGER, Ra);
				m_output.Rc = MAKE_REGADDR(RT_INTEGER, Ra);
			}
			m_output.displacement = (int32_t)(disp << 11) >> 11;    // Sign-extend it
			m_output.Rb = MAKE_REGADDR(type, 31);
            break;
        }

        case IFORMAT_JUMP: {
            // Jumps read the target from Rb and write the current PC back to Ra.
            // Microthreading doesn't need branch prediction, so we ignore the hints.
            
            // Create (Indirect) also reads Ra
            bool crei = (m_output.opcode == A_OP_CREATE_I);
            
            m_output.Ra = MAKE_REGADDR(RT_INTEGER, crei ? Ra : 31);
            m_output.Rb = MAKE_REGADDR(RT_INTEGER, Rb);
            m_output.Rc = MAKE_REGADDR(RT_INTEGER, Ra);
            break;
        }

        case IFORMAT_FPOP:
        {
            // Floating point operate instruction
            m_output.function = (m_input.instr >> A_FLT_FUNC_SHIFT) & A_FLT_FUNC_MASK;
            bool itof  = (m_output.opcode == A_OP_ITFP)     && (m_output.function == A_ITFPFUNC_ITOFT || m_output.function == A_ITFPFUNC_ITOFS || m_output.function == A_ITFPFUNC_ITOFF);
            bool print = (m_output.opcode == A_OP_UTHREADF) && (m_output.function == A_UTHREADF_PRINT);
            
            m_output.Ra = MAKE_REGADDR(itof  ? RT_INTEGER : RT_FLOAT, Ra);
            m_output.Rb = MAKE_REGADDR(print ? RT_INTEGER : RT_FLOAT, Rb);
            m_output.Rc = MAKE_REGADDR(RT_FLOAT, Rc);
            break;
        }

        case IFORMAT_OP:
        {
            // Integer operate instruction
            m_output.function = (m_input.instr >> A_INT_FUNC_SHIFT) & A_INT_FUNC_MASK;
            bool ftoi = (m_output.opcode == A_OP_FPTI) && (m_output.function == A_FPTIFUNC_FTOIT || m_output.function == A_FPTIFUNC_FTOIS);

            m_output.Ra = MAKE_REGADDR(ftoi ? RT_FLOAT : RT_INTEGER, Ra);
            m_output.Rb = MAKE_REGADDR(RT_INTEGER, Rb);
            m_output.Rc = MAKE_REGADDR(RT_INTEGER, Rc);
            if (!ftoi && m_input.instr & 0x0001000)
            {
                // Use literal instead of Rb
                m_output.Rb      = MAKE_REGADDR(RT_INTEGER, 31);
                m_output.literal = (m_input.instr >> A_LITERAL_SHIFT) & A_LITERAL_MASK;
            }
            break;
        }

		case IFORMAT_SPECIAL:
		{
			bool setregs = (m_output.opcode == A_OP_SETREGS);

			// We encode the register specifiers (in branch-like displacement) in the literal
			m_output.literal = (m_input.instr >> A_BRADISP_SHIFT) & A_BRADISP_MASK;

			// Allocate writes to Ra
			// Setregs reads from Ra
            m_output.Ra = MAKE_REGADDR(RT_INTEGER, setregs ? Ra : 31);
            m_output.Rb = MAKE_REGADDR(RT_INTEGER, 31);
            m_output.Rc = MAKE_REGADDR(RT_INTEGER, setregs ? 31 : Ra);
			break;
		}

        default:
            {
                stringstream error;
                error << "Illegal opcode in instruction at " << hex << setw(16) << setfill('0') << m_input.pc;
                throw IllegalInstructionException(error.str());
            }
        }

        // Translate registers from 32-window to full register window
        m_output.Ra = translateRegister((uint8_t)m_output.Ra.index, m_output.Ra.type, &m_output.Rra);
        m_output.Rb = translateRegister((uint8_t)m_output.Rb.index, m_output.Rb.type, &m_output.Rrb);
        m_output.Rc = translateRegister((uint8_t)m_output.Rc.index, m_output.Rc.type, &m_output.Rrc);
    }
    return PIPE_CONTINUE;
}

Pipeline::DecodeStage::DecodeStage(Pipeline& parent, FetchDecodeLatch& input, DecodeReadLatch& output)
  : Stage(parent, "decode", &input, &output),
    m_input(input),
    m_output(output)
{
}
