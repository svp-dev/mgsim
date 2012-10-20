#include "Processor.h"
#include <arch/symtable.h>
#include <cassert>
#include <sstream>
#include <iomanip>
using namespace std;

namespace Simulator
{

static void ThrowIllegalInstructionException(Object& obj, MemAddr pc)
{
    stringstream error;
    error << "Illegal instruction at "
        << hex << setw(sizeof(MemAddr) * 2) << setfill('0') << pc;
    throw IllegalInstructionException(obj, error.str());
}

// Function for getting a register's type and index within that type
unsigned char GetRegisterClass(unsigned char addr, const RegsNo& regs, RegClass* rc)
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

Processor::Pipeline::InstrFormat Processor::Pipeline::DecodeStage::GetInstrFormat(uint8_t opcode)
{
    switch (opcode) {
        case M_OP_SPECIAL:
            return IFORMAT_RTYPE;
        case M_OP_REGIMM:
            return IFORMAT_REGIMM;
        case M_OP_J:
        case M_OP_JAL:
            return IFORMAT_JTYPE;
        case M_OP_COP2:
            return IFORMAT_SPECIAL;
        default:
            return IFORMAT_ITYPE;
    }
}

void Processor::Pipeline::DecodeStage::DecodeInstruction(const Instruction& instr)
{
    m_output.opcode = (instr >> 26) & 0x3f;
    m_output.format = GetInstrFormat(m_output.opcode);
    m_output.Ra     = INVALID_REG;
    m_output.Rb     = INVALID_REG;
    m_output.Rc     = INVALID_REG;

    RegIndex Ra = (instr >> 21) & 0x1f;
    RegIndex Rb = (instr >> 16) & 0x1f;
    RegIndex Rc = (instr >> 11) & 0x1f;

    m_output.literal = 0;

    switch (m_output.format) {
        case IFORMAT_SPECIAL:
            if ((instr >> 21) & 0x1f)
            {
                ThrowIllegalInstructionException(*this, m_input.pc);
            }
            // We overload MFC2 x, $N for getpid/getcid/gettid/getfid
            m_output.function = Rc; // bits 11..15 give the register to read.
            m_output.Rc = MAKE_REGADDR(RT_INTEGER, Rb);
            break;

        case IFORMAT_RTYPE:
            m_output.function = instr & 0x3f;
            m_output.Ra = MAKE_REGADDR(RT_INTEGER, Ra);
            m_output.Rb = MAKE_REGADDR(RT_INTEGER, Rb);
            m_output.shift = (instr >> 6) & 0x1f;
            // Some special instructions do not write back a result
            switch(m_output.function)
            {
            case M_ROP_JR:
            case M_ROP_SYSCALL:
            case M_ROP_BREAK:
            case M_ROP_MTHI:
            case M_ROP_MTLO:
            case M_ROP_MULT:
            case M_ROP_MULTU:
            case M_ROP_DIV:
            case M_ROP_DIVU:
                break;
            default:
                m_output.Rc = MAKE_REGADDR(RT_INTEGER, Rc);
                break;
            }
            break;

        case IFORMAT_REGIMM:
            m_output.Ra = MAKE_REGADDR(RT_INTEGER, Ra);
            m_output.regimm = (instr >> 16) & 0x1f;
            m_output.displacement = (int32_t)((instr & 0xffff) << 16) >> 14; // sign-extend
            if (m_output.regimm == M_REGIMMOP_BGEZAL || m_output.regimm == M_REGIMMOP_BLTZAL)
                m_output.Rc = MAKE_REGADDR(RT_INTEGER, 31);
            break;

        case IFORMAT_JTYPE:
            // 26-bit target field
            m_output.displacement = instr & 0x3ffffff;
            if (m_output.opcode == M_OP_JAL)
                m_output.Rc = MAKE_REGADDR(RT_INTEGER, 31);
            break;

        case IFORMAT_ITYPE:
            m_output.Ra = MAKE_REGADDR(RT_INTEGER, Ra);
            // we write back to the second register
            // but for stores we also read from it
            m_output.Rb = MAKE_REGADDR(RT_INTEGER, Rb);
            m_output.immediate = instr & 0xffff;
            switch(m_output.opcode)
            {
            case M_OP_BNE:
            case M_OP_BEQ:
            case M_OP_BLEZ:
            case M_OP_BGTZ:
                m_output.displacement = (int32_t)(m_output.immediate << 16) >> 14;
                break;
            case M_OP_SB:
            case M_OP_SH:
            case M_OP_SW:
            case M_OP_SWL:
            case M_OP_SWR:
                break;
            default:
                // all I-type instructions except for cond. branches and stores write back to $rt (Rb)
                m_output.Rc = MAKE_REGADDR(RT_INTEGER, Rb);
                break;
            }
            break;
    }
}



Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecuteInstruction()
{
    Thread& thread = m_threadTable[m_input.tid];

    // Fetch both potential input buffers.
    uint32_t Rav = m_input.Rav.m_integer.get(m_input.Rav.m_size);
    uint32_t Rbv = m_input.Rbv.m_integer.get(m_input.Rbv.m_size);

    // Note that:
    //   * Overflow is not handled, so instructions like ADDI and ADDIU share implementations.
    //   * We don't verify that the low bits of addresses are zero (could be handled in the memory stage).
    //   * We ignore delay slots; the pipeline is flushed immediately upon branching.
    //   * For various instructions (e.g. BLEZ/BGTZ), we don't check that registers (e.g. Rb) are actually 0.
    switch (m_input.format) {
        case IFORMAT_SPECIAL:
            COMMIT { m_output.Rcv.m_state = RST_FULL; }

            switch (m_input.function) {
            case M_SPECIAL_GETTID: COMMIT { m_output.Rcv.m_integer = m_input.tid; } break;
            case M_SPECIAL_GETFID: COMMIT { m_output.Rcv.m_integer = m_input.fid; } break;
            case M_SPECIAL_GETCID: COMMIT { m_output.Rcv.m_integer = m_parent.GetProcessor().GetPID(); } break;
            case M_SPECIAL_GETPID:
            {
                PlaceID place;
                place.size = m_input.placeSize;
                place.pid  = m_parent.GetProcessor().GetPID() & -place.size;
                place.capability = 0x1337; // later: find a proper substitute
                COMMIT { m_output.Rcv.m_integer = m_parent.GetProcessor().PackPlace(place); }
                break;
            }
            case M_SPECIAL_LDBP: m_output.Rcv.m_integer = m_parent.GetProcessor().GetTLSAddress(m_input.fid, m_input.tid); break; 
            case M_SPECIAL_LDFP:
            {
                const MemAddr tls_base = m_parent.GetProcessor().GetTLSAddress(m_input.fid, m_input.tid);
                const MemAddr tls_size = m_parent.GetProcessor().GetTLSSize();
                COMMIT { m_output.Rcv.m_integer = tls_base + tls_size; }
                break;
            }
            default:
                // Read ASR (6..15) / APR (16...31)
                if ((m_input.function) < M_SPECIAL_GETAPR_FIRST)
                {
                    COMMIT { m_output.Rcv.m_integer = m_parent.GetProcessor().ReadASR(m_input.function - M_SPECIAL_GETASR_FIRST); break; }
                }
                else
                {
                    COMMIT { m_output.Rcv.m_integer = m_parent.GetProcessor().ReadAPR(m_input.function - M_SPECIAL_GETAPR_FIRST); break; }
                }
                break;
            }
            break;
        case IFORMAT_RTYPE:
            switch (m_input.function) {
                case M_ROP_SLL:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rbv << m_input.shift;
                    }
                    break;
                case M_ROP_SRL:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rbv >> m_input.shift;
                    }
                    break;
                case M_ROP_SRA:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = (int32_t)Rbv >> m_input.shift;
                    }
                    break;
                case M_ROP_SLLV:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rbv << (Rav & 0x1f);
                    }
                    break;
                case M_ROP_SRLV:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rbv >> (Rav & 0x1f);
                    }
                    break;
                case M_ROP_SRAV:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = (int32_t)Rbv >> (Rav & 0x1f);
                    }
                    break;
                case M_ROP_JR:
                    if (Rav != m_input.pc + sizeof(Instruction)) {
                        DebugFlowWrite("F%u/T%u(%llu) %s branch %s",
                                       (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                                       m_parent.GetProcessor().GetSymbolTable()[Rav].c_str());
                        COMMIT {
                            m_output.pc = Rav;
                            m_output.swch = true;
                        }
                        return PIPE_FLUSH;
                    }
                    break;
                case M_ROP_JALR:
                    {
                        MemAddr next = m_input.pc + sizeof(Instruction);
                        COMMIT {
                            m_output.Rcv.m_state = RST_FULL;
                            m_output.Rcv.m_integer = next;
                        }
                        if (Rav == next)
                            break;
                        DebugFlowWrite("F%u/T%u(%llu) %s branch %s",
                                       (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                                       m_parent.GetProcessor().GetSymbolTable()[Rav].c_str());
                        COMMIT {
                            m_output.pc = Rav;
                            m_output.swch = true;
                        }
                        return PIPE_FLUSH;
                    }
                    break;
                case M_ROP_BREAK:
                case M_ROP_SYSCALL:
                    ThrowIllegalInstructionException(*this, m_input.pc);
                    break;
                case M_ROP_MFHI:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = thread.HI;
                    }
                    break;
                case M_ROP_MTHI:
                    COMMIT {
                        thread.HI = Rav;
                    }
                    break;
                case M_ROP_MFLO:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = thread.LO;
                    }
                    break;
                case M_ROP_MTLO:
                    COMMIT {
                        thread.LO = Rav;
                    }
                    break;
                case M_ROP_MULT:
                    COMMIT {
                        uint64_t result = (int64_t)(int32_t)Rav * (int64_t)(int32_t)Rbv;
                        thread.LO = result & 0xffffffff;
                        thread.HI = result >> 32;
                    }
                    break;
                case M_ROP_MULTU:
                    COMMIT {
                        uint64_t result = (uint64_t)Rav * (uint64_t)Rbv;
                        thread.LO = result & 0xffffffff;
                        thread.HI = result >> 32;
                    }
                    break;
                case M_ROP_DIV:
                    if (Rbv == 0)
                        ThrowIllegalInstructionException(*this, m_input.pc); // undefined
                    COMMIT {
                        thread.LO = (int32_t)Rav / (int32_t)Rbv;
                        thread.HI = (int32_t)Rav % (int32_t)Rbv;
                    }
                    break;
                case M_ROP_DIVU:
                    if (Rbv == 0)
                        ThrowIllegalInstructionException(*this, m_input.pc); // undefined
                    COMMIT {
                        thread.LO = Rav / Rbv;
                        thread.HI = Rav % Rbv;
                    }
                    break;
                case M_ROP_ADD:
                case M_ROP_ADDU:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rav + Rbv;
                    }
                    break;
		case M_ROP_SUB:
		case M_ROP_SUBU:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rav - Rbv;
                    }
                    break;
                case M_ROP_AND:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rav & Rbv;
                    }
                    break;
                case M_ROP_OR:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rav | Rbv;
                    }
                    break;
                case M_ROP_XOR:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rav ^ Rbv;
                    }
                    break;
                case M_ROP_NOR:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = ~(Rav | Rbv);
                    }
                    break;
                case M_ROP_SLT:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = (int32_t)Rav < (int32_t)Rbv ? 1 : 0;
                    }
                    break;
                case M_ROP_SLTU:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rav < Rbv ? 1 : 0;
                    }
                    break;
                default:
                    ThrowIllegalInstructionException(*this, m_input.pc);
            }
            break;

        case IFORMAT_REGIMM:
            switch (m_input.regimm) {
                case M_REGIMMOP_BLTZ:
                case M_REGIMMOP_BGEZ:
                case M_REGIMMOP_BLTZAL:
                case M_REGIMMOP_BGEZAL:
                    {
                        MemAddr next = m_input.pc + sizeof(Instruction);
                        if (m_input.regimm == M_REGIMMOP_BGEZAL || m_input.regimm == M_REGIMMOP_BLTZAL) {
                            COMMIT {
                                m_output.Rcv.m_state = RST_FULL;
                                m_output.Rcv.m_integer = next;
                            }
                        }

                        if (m_input.regimm == M_REGIMMOP_BLTZ || m_input.regimm == M_REGIMMOP_BLTZAL) {
                            if ((int32_t)Rav >= 0)
                                break;
                        } else {
                            if ((int32_t)Rav < 0)
                                break;
                        }

                        MemAddr target = next + m_input.displacement;
                        if (target != next) {
                            DebugFlowWrite("F%u/T%u(%llu) %s branch %s",
                                           (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                                           m_parent.GetProcessor().GetSymbolTable()[target].c_str());
                            COMMIT {
                                m_output.pc = target;
                                m_output.swch = true;
                            }
                            return PIPE_FLUSH;
                        }
                    }
                    break;
                default:
                    ThrowIllegalInstructionException(*this, m_input.pc);
            }
            break;

        case IFORMAT_JTYPE:
            switch (m_input.opcode) {
	        case M_OP_J:
	        case M_OP_JAL:
                {
                    MemAddr next = m_input.pc + sizeof(Instruction);
                    MemAddr target = (next & 0xf0000000) | (m_input.displacement << 2);
                    if (m_input.opcode == M_OP_JAL) {
                        COMMIT {
                            m_output.Rcv.m_state = RST_FULL;
                            m_output.Rcv.m_integer = next;
                        }
                    }
                    if (target != next) {
                        DebugFlowWrite("F%u/T%u(%llu) %s branch %s",
                                       (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                                       m_parent.GetProcessor().GetSymbolTable()[target].c_str());
                        COMMIT {
                            m_output.swch = true;
                            m_output.pc = target;
                        }
                        return PIPE_FLUSH;
                    }
                }
                break;
                default:
                    ThrowIllegalInstructionException(*this, m_input.pc);
            }
            break;

        case IFORMAT_ITYPE:
            switch (m_input.opcode) {
                case M_OP_BEQ:
                case M_OP_BNE:
                case M_OP_BLEZ:
                case M_OP_BGTZ:
                {
                    if (m_input.opcode == M_OP_BEQ && Rav != Rbv)
                        break;
                    if (m_input.opcode == M_OP_BNE && Rav == Rbv)
                        break;
                    if (m_input.opcode == M_OP_BLEZ && (int32_t)Rav > (int32_t)Rbv)
                        break;
                    if (m_input.opcode == M_OP_BGTZ && (int32_t)Rav <= (int32_t)Rbv)
                        break;

                    MemAddr next = m_input.pc + sizeof(Instruction);
                    MemAddr target = next + m_input.displacement;
                    if (target != next) {
                        DebugFlowWrite("F%u/T%u(%llu) %s branch %s",
                                       (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                                       m_parent.GetProcessor().GetSymbolTable()[target].c_str());
                        COMMIT {
                            m_output.swch = true;
                            m_output.pc = target;
                        }
                        return PIPE_FLUSH;
                    }
                    break;
                }
                case M_OP_ADDI:
                case M_OP_ADDIU:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rav + (int16_t)m_input.immediate;
                    }
                    break;
                case M_OP_SLTI:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = (int32_t)Rav < (int16_t)m_input.immediate ? 1 : 0;
                    }
                    break;
                case M_OP_SLTIU:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rav < (uint32_t)(int16_t)m_input.immediate ? 1 : 0;
                    }
                    break;
                case M_OP_ANDI:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rav & (uint16_t)m_input.immediate;
                    }
                    break;
                case M_OP_ORI:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rav | (uint16_t)m_input.immediate;
                    }
                    break;
                case M_OP_XORI:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = Rav ^ (uint16_t)m_input.immediate;
                    }
                    break;
                case M_OP_LUI:
                    COMMIT {
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = m_input.immediate << 16;
                    }
                    break;
                case M_OP_LB:
                case M_OP_LBU:
                case M_OP_LH:
                case M_OP_LHU:
                case M_OP_LW:
                    COMMIT {
                        m_output.sign_extend = (m_input.opcode == M_OP_LB || m_input.opcode == M_OP_LH);
                        if (m_input.opcode == M_OP_LB || m_input.opcode == M_OP_LBU)
                            m_output.size = 1;
                        else if (m_input.opcode == M_OP_LH || m_input.opcode == M_OP_LHU)
                            m_output.size = 2;
                        else if (m_input.opcode == M_OP_LW)
                            m_output.size = 4;
                        else
                            assert(false && "unreachable");
                        m_output.address = Rav + (int16_t)m_input.immediate;
                        m_output.Rcv.m_state = RST_INVALID;
                    }
                    break;
                case M_OP_SB:
                case M_OP_SH:
                case M_OP_SW:
                case M_OP_SWL:
                case M_OP_SWR:
                {
                    MemAddr address = Rav + (int16_t)m_input.immediate;
                    uint32_t value = m_input.Rbv.m_integer.get(4);
                    unsigned size = 0;

                    switch(m_input.opcode)
                    {
                    case M_OP_SB: size = 1; break;
                    case M_OP_SH: size = 2; break;
                    case M_OP_SW: size = 4; break;
                    case M_OP_SWL:
                    {
                        unsigned shift = address & 3;
                        if (ARCH_ENDIANNESS == ARCH_LITTLE_ENDIAN)
                            address &= ~(uint32_t)3;
                        else
                            shift ^= 3;
                        value >>= (8 * shift);
                        size = shift;
                    }
                    break;
                    case M_OP_SWR:
                    {
                        unsigned shift = address & 3;
                        if (ARCH_ENDIANNESS == ARCH_LITTLE_ENDIAN)
                            address &= ~(uint32_t)3;
                        else
                            shift ^= 3;
                        value &= ~(uint32_t)((1 << shift) - 1);
                        size = 4 - shift;
                    }
                    break;
                    default:
                        assert(false && "unreachable");
                    }

                    COMMIT {
                        m_output.address = address;
                        m_output.size = size;
                        m_output.Rcv.m_state = RST_FULL;
                        m_output.Rcv.m_integer = value;
                    }
                }
                break;
                default:
                    ThrowIllegalInstructionException(*this, m_input.pc);
            }
            break;
    }
  
      return PIPE_CONTINUE;


}

}
