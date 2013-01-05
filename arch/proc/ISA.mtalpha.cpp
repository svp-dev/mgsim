#include "Processor.h"
#include <arch/FPU.h>
#include <arch/symtable.h>

#include <cassert>
#include <sstream>
#include <iomanip>
using namespace std;

namespace Simulator
{
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

// Function for naming local registers according to a standard ABI
const vector<string>& GetDefaultLocalRegisterAliases(RegType type)
{
    static const vector<string> intnames = {
        "rv",   "t0",  "t1", "t2",
        "t3",   "t4",  "t5", "t6",
        "t7",   "s0",  "s1", "s2",
        "s3",   "s4",  "s5", "fp",
        "a0",   "a1",  "a2", "a3",
        "a4",   "a5",  "t8", "t9",
        "t10",  "t11", "ra", "pv",
        "at",   "gp",  "sp", "31" };
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
unsigned char GetRegisterClass(unsigned char addr, const RegsNo& regs, RegClass* rc)
{
    assert(regs.globals < 32);
    assert(regs.shareds < 32);
    assert(regs.locals  < 32);

    if (addr < regs.locals)
    {
        *rc = RC_LOCAL;
        return addr;
    }
    addr -= regs.locals;
    if (addr < regs.globals)
    {
        *rc = RC_GLOBAL;
        return addr;
    }
    addr -= regs.globals;
    if (addr < regs.shareds)
    {
        *rc = RC_SHARED;
        return addr;
    }
    addr -= regs.shareds;
    if (addr < regs.shareds)
    {
        *rc = RC_DEPENDENT;
        return addr;
    }
    *rc = RC_RAZ;
    return 0;
}

/*static*/
Processor::Pipeline::InstrFormat Processor::Pipeline::DecodeStage::GetInstrFormat(uint8_t opcode)
{
    if (opcode <= 0x3F)
    {
        switch (opcode >> 4)
        {
            case 0x0:
                switch (opcode & 0xF) {
                    case 0x0: return IFORMAT_PAL;
                    case 0x1: return IFORMAT_OP;
                    case 0x3: return IFORMAT_JUMP;
                    case 0x4: return IFORMAT_BRA;
                    case 0x5: return IFORMAT_FPOP;
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

void Processor::Pipeline::DecodeStage::DecodeInstruction(const Instruction& instr)
{
    m_output.opcode = (uint8_t)((instr >> A_OPCODE_SHIFT) & A_OPCODE_MASK);
    m_output.format = GetInstrFormat(m_output.opcode);
    m_output.Ra     = INVALID_REG;
    m_output.Rb     = INVALID_REG;
    m_output.Rc     = INVALID_REG;

    RegIndex Ra    = (instr >> A_RA_SHIFT) & A_REG_MASK;
    RegIndex Rb    = (instr >> A_RB_SHIFT) & A_REG_MASK;
    RegIndex Rc    = (instr >> A_RC_SHIFT) & A_REG_MASK;

    // It is important that this is zero by default. If Rb is $R31, this value
    // will be used instead of RegFile[Rb]
    m_output.literal = 0;

    switch (m_output.format)
    {
        case IFORMAT_PAL:
            m_output.literal = (instr >> A_PALCODE_SHIFT) & A_PALCODE_MASK;
            break;

        case IFORMAT_MISC:
        case IFORMAT_MEM_LOAD:
        case IFORMAT_MEM_STORE:
        {
            uint32_t disp  = (instr >> A_MEMDISP_SHIFT) & A_MEMDISP_MASK;
            RegType  type  = (m_output.opcode >= 0x20 && m_output.opcode <= 0x27) ? RT_FLOAT : RT_INTEGER;

            // Note that for a load we also load the destination register (Ra) to ensure that we don't
            // load to a pending register (which causes problems).
            m_output.Rc           = MAKE_REGADDR(type, (m_output.format == IFORMAT_MEM_STORE) ? 31 : Ra);
            m_output.Ra           = MAKE_REGADDR(type, (m_output.opcode <= 0x0a) ? 31 : Ra); // Don't load Ra for LDA and LDAH
            m_output.Rb           = MAKE_REGADDR(RT_INTEGER, Rb);
            m_output.displacement = (int32_t)(int16_t)disp;
            m_output.function     = (uint16_t)disp;
            m_output.RaNotPending = (m_output.format != IFORMAT_MEM_STORE);

            if (m_output.opcode == A_OP_LDQ_U && disp == 0 && Ra == 31)
            {
                // This is a UNOP. Rb can be anything, but that might cause problems, so we force Rb to R31
                m_output.Rb = MAKE_REGADDR(type, 31);
            }
            break;
        }

        case IFORMAT_BRA:
        {
            uint32_t disp = (instr >> A_BRADISP_SHIFT) & A_BRADISP_MASK;
            RegType  type = (m_output.opcode > 0x30 && m_output.opcode < 0x38 && m_output.opcode != 0x34) ? RT_FLOAT : RT_INTEGER;
            if (m_output.opcode == A_OP_CREATE_D)
            {
                // Create reads from and writes to Ra
                m_output.Ra = MAKE_REGADDR(RT_INTEGER, Ra);
                m_output.Rc = MAKE_REGADDR(RT_INTEGER, Ra);
            }
            else
            {
                // Unconditional branches write the PC back to Ra.
                // Conditional branches test Ra.
                bool conditional = (m_output.opcode != A_OP_BR && m_output.opcode != A_OP_BSR);

                m_output.Ra = MAKE_REGADDR(type, (conditional) ? Ra : 31);
                m_output.Rc = MAKE_REGADDR(type, (conditional) ? 31 : Ra);
            }
            m_output.displacement = (int32_t)(disp << 11) >> 11; // Sign-extend it
            m_output.Rb = MAKE_REGADDR(type, 31);
            break;
        }

        case IFORMAT_JUMP: {
            // Jumps read the target from Rb and write the current PC back to Ra.
            // Microthreading doesn't need branch prediction, so we ignore the hints.

            // Create (indirect) also reads Ra
            bool crei = (m_output.opcode == A_OP_CREATE_I);

            m_output.Ra = MAKE_REGADDR(RT_INTEGER, crei ? Ra : 31);
            m_output.Rb = MAKE_REGADDR(RT_INTEGER, Rb);
            m_output.Rc = MAKE_REGADDR(RT_INTEGER, Ra);
            break;
        }

        case IFORMAT_FPOP:
        {
            // Floating point operate instruction
            m_output.function = (uint16_t)((instr >> A_FLT_FUNC_SHIFT) & A_FLT_FUNC_MASK);
            bool itof  = (m_output.opcode == A_OP_ITFP) && (m_output.function == A_ITFPFUNC_ITOFT || m_output.function == A_ITFPFUNC_ITOFS || m_output.function == A_ITFPFUNC_ITOFF);
            bool utfop = (m_output.opcode == A_OP_UTHREADF);

            m_output.Ra = MAKE_REGADDR(utfop || itof ? RT_INTEGER : RT_FLOAT, Ra);
            m_output.Rb = MAKE_REGADDR(RT_FLOAT, Rb);
            m_output.Rc = MAKE_REGADDR(RT_FLOAT, Rc);

            if (m_output.opcode == A_OP_UTHREADF)
            {
                // Do not translate some register, we want its index as-is.
                switch (m_output.function)
                {
                case A_UTHREADF_PUTG:
                case A_UTHREADF_PUTS:
                    m_output.regofs = Rc;
                    m_output.Rc     = INVALID_REG;
                    break;

                case A_UTHREADF_GETG:
                case A_UTHREADF_GETS:
                    m_output.regofs = Rb;
                    m_output.Rb     = INVALID_REG;
                    break;

                default:
                    break;
                }
            }
            break;
        }

        case IFORMAT_OP:
        {
            // Integer operate instruction
            m_output.function = (uint16_t)((instr >> A_INT_FUNC_SHIFT) & A_INT_FUNC_MASK);
            bool ftoi = (m_output.opcode == A_OP_FPTI) && (m_output.function == A_FPTIFUNC_FTOIT || m_output.function == A_FPTIFUNC_FTOIS);

            m_output.Ra = MAKE_REGADDR(ftoi ? RT_FLOAT : RT_INTEGER, Ra);
            m_output.Rc = MAKE_REGADDR(RT_INTEGER, Rc);
            if (!ftoi && instr & 0x0001000)
            {
                // Use literal instead of Rb
                m_output.Rb      = MAKE_REGADDR(RT_INTEGER, 31);
                m_output.literal = (instr >> A_LITERAL_SHIFT) & A_LITERAL_MASK;
            }
            else // ftoi || !(instr & 0x0001000)
            {
                m_output.Rb = MAKE_REGADDR(RT_INTEGER, Rb);
            }

            if (m_output.opcode == A_OP_UTHREAD)
            {
                // Do not translate some register, we want its index as-is.
                switch (m_output.function)
                {
                case A_UTHREAD_PUTG:
                case A_UTHREAD_PUTS:
                    m_output.regofs = Rc;
                    m_output.Rc     = INVALID_REG;
                    break;

                case A_UTHREAD_GETG:
                case A_UTHREAD_GETS:
                    m_output.regofs = Rb;
                    m_output.Rb     = INVALID_REG;
                    break;

                default:
                    break;
                }
            }
            break;
        }

        default:
            break;
    }
}

/*static*/
bool Processor::Pipeline::ExecuteStage::BranchTaken(uint8_t opcode, const PipeValue& value)
{
    switch (opcode)
    {
        case A_OP_BR  : return true;
        case A_OP_BSR : return true;
        case A_OP_BEQ : return ((int64_t)value.m_integer.get(value.m_size) == 0);
        case A_OP_BGE : return ((int64_t)value.m_integer.get(value.m_size) >= 0);
        case A_OP_BGT : return ((int64_t)value.m_integer.get(value.m_size) >  0);
        case A_OP_BLE : return ((int64_t)value.m_integer.get(value.m_size) <= 0);
        case A_OP_BLT : return ((int64_t)value.m_integer.get(value.m_size) <  0);
        case A_OP_BNE : return ((int64_t)value.m_integer.get(value.m_size) != 0);
        case A_OP_BLBC: return ((value.m_integer.get(value.m_size) & 0x1) == 0);
        case A_OP_BLBS: return ((value.m_integer.get(value.m_size) & 0x1) == 1);

        // Although, as described in the Alpha Handbook, section 4.9, FP branches are tested bitwise,
        // we're lazy and interpret them.
        case A_OP_FBEQ: return value.m_float.tofloat(value.m_size) == 0;
        case A_OP_FBGE: return value.m_float.tofloat(value.m_size) >= 0;
        case A_OP_FBLE: return value.m_float.tofloat(value.m_size) <= 0;
        case A_OP_FBGT: return value.m_float.tofloat(value.m_size) >  0;
        case A_OP_FBLT: return value.m_float.tofloat(value.m_size) <  0;
        case A_OP_FBNE: return value.m_float.tofloat(value.m_size) != 0;

    }
    return false;
}

//
// byteZAP()
//

static uint64_t byteZAP(uint64_t op, int mask)
{
    for (int i = 0; i < 8; i++, mask >>= 1)
    {
        if (mask & 1) {
            op &= ~(0xFFULL << (i * 8));
        }
    }

    return op;
}

static void mul128b(uint64_t op1, uint64_t op2, uint64_t *resultH, uint64_t *resultL)
{
    uint64_t op1H = op1 >> 32;
    uint64_t op1L = op1 & 0xFFFFFFFF;
    uint64_t op2H = op2 >> 32;
    uint64_t op2L = op2 & 0xFFFFFFFF;

    uint64_t x = op1L * op2L;
    uint64_t y = ((op1L * op2H) << 32);
    uint64_t z = x + y;

    uint64_t carry = (z < x || z < y) ? 1 : 0;

    x = z;
    y = (op1H * op2L) << 32;
    z = x + y;
    if (z < x || z < y) carry++;

    if (resultL != NULL) *resultL = z;
    if (resultH != NULL) *resultH = op1H * op2H + ((op1L * op2H) >> 32) + ((op1H * op2L) >> 32) + carry;
}

/*static*/
bool Processor::Pipeline::ExecuteStage::ExecuteINTA(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func)
{
    Rcv.m_state  = RST_FULL;
    uint64_t      Ra = Rav.m_integer.get(Rav.m_size);
    uint64_t      Rb = Rbv.m_integer.get(Rbv.m_size);
    MultiInteger& Rc = Rcv.m_integer;

    switch (func)
    {
        // Addition
        case A_INTAFUNC_ADDL_V:
        case A_INTAFUNC_ADDL:   Rc = (int64_t)(int32_t)(Ra + Rb); break;
        case A_INTAFUNC_S4ADDL: Rc = (int64_t)(int32_t)((Ra << 2) + Rb); break;
        case A_INTAFUNC_S8ADDL: Rc = (int64_t)(int32_t)((Ra << 3) + Rb); break;
        case A_INTAFUNC_ADDQ_V:
        case A_INTAFUNC_ADDQ:   Rc = Ra + Rb; break;
        case A_INTAFUNC_S4ADDQ: Rc = (Ra << 2) + Rb; break;
        case A_INTAFUNC_S8ADDQ: Rc = (Ra << 3) + Rb; break;

        // Signed compare
        case A_INTAFUNC_CMPEQ: Rc = ((int64_t)Ra == (int64_t)Rb) ? 1 : 0; break;
        case A_INTAFUNC_CMPLT: Rc = ((int64_t)Ra <  (int64_t)Rb) ? 1 : 0; break;
        case A_INTAFUNC_CMPLE: Rc = ((int64_t)Ra <= (int64_t)Rb) ? 1 : 0; break;

        // Unsigned compare
        case A_INTAFUNC_CMPULT: Rc = (Ra <  Rb) ? 1 : 0; break;
        case A_INTAFUNC_CMPULE: Rc = (Ra <= Rb) ? 1 : 0; break;

        // Subtract
        case A_INTAFUNC_SUBL_V:
        case A_INTAFUNC_SUBL:   Rc = (int64_t)(int32_t)(Ra - Rb); break;
        case A_INTAFUNC_S4SUBL: Rc = (int64_t)(int32_t)((Ra << 2) - Rb); break;
        case A_INTAFUNC_S8SUBL: Rc = (int64_t)(int32_t)((Ra << 3) - Rb); break;
        case A_INTAFUNC_SUBQ_V:
        case A_INTAFUNC_SUBQ:   Rc = Ra - Rb; break;
        case A_INTAFUNC_S4SUBQ: Rc = (Ra << 2) - Rb; break;
        case A_INTAFUNC_S8SUBQ: Rc = (Ra << 3) - Rb; break;

        // Parallel 8-byte compare
        case A_INTAFUNC_CMPBGE:
        {
            uint64_t result = 0;
            for (int i = 0; i < 8; ++i) {
                uint8_t a = (uint8_t)((Ra >> (i * 8)) & 0xFF);
                uint8_t b = (uint8_t)((Rb >> (i * 8)) & 0xFF);
                if (a >= b) result |= (uint64_t)(1 << i);
            }
            Rc = result;
            break;
        }
    }
    return true;
}

/*static*/
bool Processor::Pipeline::ExecuteStage::ExecuteINTL(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func)
{
    Rcv.m_state      = RST_FULL;
    uint64_t      Ra = Rav.m_integer.get(Rav.m_size);
    uint64_t      Rb = Rbv.m_integer.get(Rbv.m_size);
    MultiInteger& Rc = Rcv.m_integer;

    switch (func)
    {
        // Logical functions
        case A_INTLFUNC_AND:   Rc = Ra &  Rb; break;
        case A_INTLFUNC_BIC:   Rc = Ra & ~Rb; break;
        case A_INTLFUNC_BIS:   Rc = Ra |  Rb; break;
        case A_INTLFUNC_ORNOT: Rc = Ra | ~Rb; break;
        case A_INTLFUNC_XOR:   Rc = Ra ^  Rb; break;
        case A_INTLFUNC_EQV:   Rc = Ra ^ ~Rb; break;

        // Conditional move
        case A_INTLFUNC_CMOVEQ:  if ((int64_t)Ra == 0) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_INTLFUNC_CMOVNE:  if ((int64_t)Ra != 0) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_INTLFUNC_CMOVLT:  if ((int64_t)Ra <  0) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_INTLFUNC_CMOVGE:  if ((int64_t)Ra >= 0) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_INTLFUNC_CMOVLE:  if ((int64_t)Ra <= 0) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_INTLFUNC_CMOVGT:  if ((int64_t)Ra >  0) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_INTLFUNC_CMOVLBS: if ( Ra & 1)          Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_INTLFUNC_CMOVLBC: if (~Ra & 1)          Rc = Rb; else Rcv.m_state = RST_INVALID; break;

        // Misc functions
        case A_INTLFUNC_IMPLVER: Rc = IMPLVER_EV6; break; // We simulate an EV6 ISA
        case A_INTLFUNC_AMASK:   Rc = Rb & (AMASK_BWX | AMASK_FIX | AMASK_CIX | AMASK_MVI); break;
    }
    return true;
}

/*static*/
bool Processor::Pipeline::ExecuteStage::ExecuteINTS(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func)
{
    Rcv.m_state      = RST_FULL;
    uint64_t      Ra = Rav.m_integer.get(Rav.m_size);
    uint64_t      Rb = Rbv.m_integer.get(Rbv.m_size);
    MultiInteger& Rc = Rcv.m_integer;

    switch (func)
    {
        // Mask byte
        case A_INTSFUNC_MSKBL: Rc = byteZAP(Ra, 0x01 << (Rb & 7)); break;
        case A_INTSFUNC_MSKWL: Rc = byteZAP(Ra, 0x03 << (Rb & 7)); break;
        case A_INTSFUNC_MSKLL: Rc = byteZAP(Ra, 0x0F << (Rb & 7)); break;
        case A_INTSFUNC_MSKQL: Rc = byteZAP(Ra, 0xFF << (Rb & 7)); break;
        case A_INTSFUNC_MSKWH: Rc = byteZAP(Ra, (0x03 << (Rb & 7)) >> 8); break;
        case A_INTSFUNC_MSKLH: Rc = byteZAP(Ra, (0x0F << (Rb & 7)) >> 8); break;
        case A_INTSFUNC_MSKQH: Rc = byteZAP(Ra, (0xFF << (Rb & 7)) >> 8); break;

        // Extract byte
        case A_INTSFUNC_EXTBL: Rc = byteZAP(Ra >> (int)((Rb & 7) * 8), ~0x01); break;
        case A_INTSFUNC_EXTWL: Rc = byteZAP(Ra >> (int)((Rb & 7) * 8), ~0x03); break;
        case A_INTSFUNC_EXTLL: Rc = byteZAP(Ra >> (int)((Rb & 7) * 8), ~0x0F); break;
        case A_INTSFUNC_EXTQL: Rc = byteZAP(Ra >> (int)((Rb & 7) * 8), ~0xFF); break;
        case A_INTSFUNC_EXTWH: Rc = byteZAP(Ra << (64 - (Rb & 7) * 8), ~0x03); break;
        case A_INTSFUNC_EXTLH: Rc = byteZAP(Ra << (64 - (Rb & 7) * 8), ~0x0F); break;
        case A_INTSFUNC_EXTQH: Rc = byteZAP(Ra << (64 - (Rb & 7) * 8), ~0xFF); break;

        // Insert byte
        case A_INTSFUNC_INSBL: Rc = byteZAP(Ra << ((Rb & 7) * 8), ~(0x01 << (Rb & 7))); break;
        case A_INTSFUNC_INSWL: Rc = byteZAP(Ra << ((Rb & 7) * 8), ~(0x03 << (Rb & 7))); break;
        case A_INTSFUNC_INSLL: Rc = byteZAP(Ra << ((Rb & 7) * 8), ~(0x0F << (Rb & 7))); break;
        case A_INTSFUNC_INSQL: Rc = byteZAP(Ra << ((Rb & 7) * 8), ~(0xFF << (Rb & 7))); break;
        case A_INTSFUNC_INSWH: Rc = byteZAP(Ra << (64 - (Rb & 7) * 8), ~(0x03 << (int)(Rb & 7))); break;
        case A_INTSFUNC_INSLH: Rc = byteZAP(Ra << (64 - (Rb & 7) * 8), ~(0x0F << (int)(Rb & 7))); break;
        case A_INTSFUNC_INSQH: Rc = byteZAP(Ra << (64 - (Rb & 7) * 8), ~(0xFF << (int)(Rb & 7))); break;

        // Zero bytes
        case A_INTSFUNC_ZAP:    Rc = byteZAP(Ra, (uint8_t) Rb); break;
        case A_INTSFUNC_ZAPNOT: Rc = byteZAP(Ra, (uint8_t)~Rb); break;

        // Shift
        case A_INTSFUNC_SLL: Rc = Ra << (Rb & 0x3F); break;
        case A_INTSFUNC_SRL: Rc = Ra >> (Rb & 0x3F); break;
        case A_INTSFUNC_SRA: Rc = (int64_t)Ra >> (Rb & 0x3F); break;
    }
    return true;
}

bool Processor::Pipeline::ExecuteStage::ExecuteINTM(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func)
{
    Rcv.m_state = RST_FULL;
    uint64_t Ra = Rav.m_integer.get(Rav.m_size);
    uint64_t Rb = Rbv.m_integer.get(Rbv.m_size);

    uint64_t Rc;
    switch(func)
    {
        case A_INTMFUNC_MULL_V:
        case A_INTMFUNC_MULL:  Rc = (int64_t)(int32_t)((int32_t)Ra * (int32_t)Rb); break;
        case A_INTMFUNC_MULQ_V:
        case A_INTMFUNC_MULQ:  mul128b(Ra, Rb, NULL, &Rc); break;
        case A_INTMFUNC_UMULH: mul128b(Ra, Rb, &Rc, NULL); break;
#define CHECKDIV0(X) if ((X) == 0) ThrowIllegalInstructionException(*this, m_input.pc, "Division by zero")
        case A_INTMFUNC_DIVL:  CHECKDIV0((int32_t)Rb);  Rc = (int64_t)(int32_t)((int32_t)Ra / (int32_t)Rb); break;
        case A_INTMFUNC_DIVQ:  CHECKDIV0((int64_t)Rb);  Rc = (int64_t)Ra / (int64_t)Rb; break;
        case A_INTMFUNC_UDIVL: CHECKDIV0((uint32_t)Rb); Rc = (uint64_t)(uint32_t)((uint32_t)Ra / (uint32_t)Rb); break;
        case A_INTMFUNC_UDIVQ: CHECKDIV0(Rb);           Rc = Ra / Rb; break;
        default: UNREACHABLE; break;
    }
    Rcv.m_integer = Rc;
    return true;
}

/*static*/
bool Processor::Pipeline::ExecuteStage::ExecuteFLTV(PipeValue& Rcv, const PipeValue& /* Rav */, const PipeValue& /* Rbv */, int func)
{
    Rcv.m_state = RST_FULL;

    switch(func) {
        case A_FLTVFUNC_ADDF_C     : {}
        case A_FLTVFUNC_SUBF_C     : {}
        case A_FLTVFUNC_MULF_C     : {}
        case A_FLTVFUNC_DIVF_C     : {}
        case A_FLTVFUNC_CVTDG_C    : {}
        case A_FLTVFUNC_ADDG_C     : {}
        case A_FLTVFUNC_SUBG_C     : {}
        case A_FLTVFUNC_MULG_C     : {}
        case A_FLTVFUNC_DIVG_C     : {}
        case A_FLTVFUNC_CVTGF_C    : {}
        case A_FLTVFUNC_CVTGD_C    : {}
        case A_FLTVFUNC_CVTGQ_C    : {}
        case A_FLTVFUNC_CVTQF_C    : {}
        case A_FLTVFUNC_CVTQG_C    : {}
        case A_FLTVFUNC_ADDF       : {}
        case A_FLTVFUNC_SUBF       : {}
        case A_FLTVFUNC_MULF       : {}
        case A_FLTVFUNC_DIVF       : {}
        case A_FLTVFUNC_CVTDG      : {}
        case A_FLTVFUNC_ADDG       : {}
        case A_FLTVFUNC_SUBG       : {}
        case A_FLTVFUNC_MULG       : {}
        case A_FLTVFUNC_DIVG       : {}
        case A_FLTVFUNC_CMPGEQ     : {}
        case A_FLTVFUNC_CMPGLT     : {}
        case A_FLTVFUNC_CMPGLE     : {}
        case A_FLTVFUNC_CVTGF      : {}
        case A_FLTVFUNC_CVTGD      : {}
        case A_FLTVFUNC_CVTGQ      : {}
        case A_FLTVFUNC_CVTQF      : {}
        case A_FLTVFUNC_CVTQG      : {}
        case A_FLTVFUNC_ADDF_UC    : {}
        case A_FLTVFUNC_SUBF_UC    : {}
        case A_FLTVFUNC_MULF_UC    : {}
        case A_FLTVFUNC_DIVF_UC    : {}
        case A_FLTVFUNC_CVTDG_UC   : {}
        case A_FLTVFUNC_ADDG_UC    : {}
        case A_FLTVFUNC_SUBG_UC    : {}
        case A_FLTVFUNC_MULG_UC    : {}
        case A_FLTVFUNC_DIVG_UC    : {}
        case A_FLTVFUNC_CVTGF_UC   : {}
        case A_FLTVFUNC_CVTGD_UC   : {}
        case A_FLTVFUNC_CVTGQ_VC   : {}
        case A_FLTVFUNC_ADDF_U     : {}
        case A_FLTVFUNC_SUBF_U     : {}
        case A_FLTVFUNC_MULF_U     : {}
        case A_FLTVFUNC_DIVF_U     : {}
        case A_FLTVFUNC_CVTDG_U    : {}
        case A_FLTVFUNC_ADDG_U     : {}
        case A_FLTVFUNC_SUBG_U     : {}
        case A_FLTVFUNC_MULG_U     : {}
        case A_FLTVFUNC_DIVG_U     : {}
        case A_FLTVFUNC_CVTGF_U    : {}
        case A_FLTVFUNC_CVTGD_U    : {}
        case A_FLTVFUNC_CVTGQ_V    : {}
        case A_FLTVFUNC_ADDF_SC    : {}
        case A_FLTVFUNC_SUBF_SC    : {}
        case A_FLTVFUNC_MULF_SC    : {}
        case A_FLTVFUNC_DIVF_SC    : {}
        case A_FLTVFUNC_CVTDG_SC   : {}
        case A_FLTVFUNC_ADDG_SC    : {}
        case A_FLTVFUNC_SUBG_SC    : {}
        case A_FLTVFUNC_MULG_SC    : {}
        case A_FLTVFUNC_DIVG_SC    : {}
        case A_FLTVFUNC_CVTGF_SC   : {}
        case A_FLTVFUNC_CVTGD_SC   : {}
        case A_FLTVFUNC_CVTGQ_SC   : {}
        case A_FLTVFUNC_ADDF_S     : {}
        case A_FLTVFUNC_SUBF_S     : {}
        case A_FLTVFUNC_MULF_S     : {}
        case A_FLTVFUNC_DIVF_S     : {}
        case A_FLTVFUNC_CVTDG_S    : {}
        case A_FLTVFUNC_ADDG_S     : {}
        case A_FLTVFUNC_SUBG_S     : {}
        case A_FLTVFUNC_MULG_S     : {}
        case A_FLTVFUNC_DIVG_S     : {}
        case A_FLTVFUNC_CMPGEQ_S   : {}
        case A_FLTVFUNC_CMPGLT_S   : {}
        case A_FLTVFUNC_CMPGLE_S   : {}
        case A_FLTVFUNC_CVTGF_S    : {}
        case A_FLTVFUNC_CVTGD_S    : {}
        case A_FLTVFUNC_CVTGQ_S    : {}
        case A_FLTVFUNC_ADDF_SUC   : {}
        case A_FLTVFUNC_SUBF_SUC   : {}
        case A_FLTVFUNC_MULF_SUC   : {}
        case A_FLTVFUNC_DIVF_SUC   : {}
        case A_FLTVFUNC_CVTDG_SUC  : {}
        case A_FLTVFUNC_ADDG_SUC   : {}
        case A_FLTVFUNC_SUBG_SUC   : {}
        case A_FLTVFUNC_MULG_SUC   : {}
        case A_FLTVFUNC_DIVG_SUC   : {}
        case A_FLTVFUNC_CVTGF_SUC  : {}
        case A_FLTVFUNC_CVTGD_SUC  : {}
        case A_FLTVFUNC_CVTGQ_SVC  : {}
        case A_FLTVFUNC_ADDF_SU    : {}
        case A_FLTVFUNC_SUBF_SU    : {}
        case A_FLTVFUNC_MULF_SU    : {}
        case A_FLTVFUNC_DIVF_SU    : {}
        case A_FLTVFUNC_CVTDG_SU   : {}
        case A_FLTVFUNC_ADDG_SU    : {}
        case A_FLTVFUNC_SUBG_SU    : {}
        case A_FLTVFUNC_MULG_SU    : {}
        case A_FLTVFUNC_DIVG_SU    : {}
        case A_FLTVFUNC_CVTGF_SU   : {}
        case A_FLTVFUNC_CVTGD_SU   : {}
        case A_FLTVFUNC_CVTGQ_SV   : {}
   }
   return true;
}

/*static*/
bool Processor::Pipeline::ExecuteStage::ExecuteFLTI(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func)
{
    Rcv.m_state = RST_FULL;
    const Float64& Ra = Rav.m_float._64;
    const Float64& Rb = Rbv.m_float._64;
    Float64&       Rc = Rcv.m_float._64;

    switch(func) {
        default:
            // Add, Sub, Mul and Div are done in the FPU
            return false;

        // IEEE Floating Compare
        case A_FLTIFUNC_CMPTUN:
        case A_FLTIFUNC_CMPTUN_SU: Rc.fromfloat( (Ra.tofloat() != Ra.tofloat() || Rb.tofloat() != Rb.tofloat()) ? 2.0 : 0.0 ); break;
        case A_FLTIFUNC_CMPTEQ:
        case A_FLTIFUNC_CMPTEQ_SU: Rc.fromfloat( (Ra.tofloat() == Rb.tofloat()) ? 2.0 : 0.0 ); break;
        case A_FLTIFUNC_CMPTLT:
        case A_FLTIFUNC_CMPTLT_SU: Rc.fromfloat( (Ra.tofloat() <  Rb.tofloat()) ? 2.0 : 0.0 ); break;
        case A_FLTIFUNC_CMPTLE:
        case A_FLTIFUNC_CMPTLE_SU: Rc.fromfloat( (Ra.tofloat() <= Rb.tofloat()) ? 2.0 : 0.0 ); break;

        // Convert IEEE Floating to Integer
        case A_FLTIFUNC_CVTTQ_VC:
        case A_FLTIFUNC_CVTTQ_VM:
        case A_FLTIFUNC_CVTTQ_V:
        case A_FLTIFUNC_CVTTQ_VD:
        case A_FLTIFUNC_CVTTQ_SVC:
        case A_FLTIFUNC_CVTTQ_SVM:
        case A_FLTIFUNC_CVTTQ:
        case A_FLTIFUNC_CVTTQ_C:
        case A_FLTIFUNC_CVTTQ_M:
        case A_FLTIFUNC_CVTTQ_D:
        case A_FLTIFUNC_CVTTQ_SV:
        case A_FLTIFUNC_CVTTQ_SVD:
        case A_FLTIFUNC_CVTTQ_SVIC:
        case A_FLTIFUNC_CVTTQ_SVIM:
        case A_FLTIFUNC_CVTTQ_SVI:
        {
            Rc.integer = (int64_t)Rb.tofloat();
            break;
        }

        // Convert Integer to IEEE Floating (S_floating)
        case A_FLTIFUNC_CVTQS_C:
        case A_FLTIFUNC_CVTQS:
        case A_FLTIFUNC_CVTQS_M:
        case A_FLTIFUNC_CVTQS_D:
        case A_FLTIFUNC_CVTQS_SUIC:
        case A_FLTIFUNC_CVTQS_SUIM:
        case A_FLTIFUNC_CVTQS_SUI:
        case A_FLTIFUNC_CVTQS_SUID:
        // Convert Integer to IEEE Floating (T_floating)
        case A_FLTIFUNC_CVTQT_C:
        case A_FLTIFUNC_CVTQT_M:
        case A_FLTIFUNC_CVTQT:
        case A_FLTIFUNC_CVTQT_D:
        case A_FLTIFUNC_CVTQT_SUIC:
        case A_FLTIFUNC_CVTQT_SUIM:
        case A_FLTIFUNC_CVTQT_SUI:
        case A_FLTIFUNC_CVTQT_SUID:
            Rc.fromfloat( (double)(int64_t)Rb.integer );
            break;

        // Convert IEEE S_Floating to IEEE T_Floating
        case A_FLTIFUNC_CVTST:
        case A_FLTIFUNC_CVTST_S:
            Rc.fromfloat( Rb.tofloat() );
            break;

        // Convert IEEE T_Floating to IEEE S_Floating
        case A_FLTIFUNC_CVTTS_C:
        case A_FLTIFUNC_CVTTS_M:
        case A_FLTIFUNC_CVTTS:
        case A_FLTIFUNC_CVTTS_D:
        case A_FLTIFUNC_CVTTS_UC:
        case A_FLTIFUNC_CVTTS_UM:
        case A_FLTIFUNC_CVTTS_U:
        case A_FLTIFUNC_CVTTS_UD:
        case A_FLTIFUNC_CVTTS_SUC:
        case A_FLTIFUNC_CVTTS_SUM:
        case A_FLTIFUNC_CVTTS_SU:
        case A_FLTIFUNC_CVTTS_SUD:
        case A_FLTIFUNC_CVTTS_SUIC:
        case A_FLTIFUNC_CVTTS_SUIM:
        case A_FLTIFUNC_CVTTS_SUI:
        case A_FLTIFUNC_CVTTS_SUID:
        case A_FLTIFUNC_CVTTQ_SVID:
            Rc.fromfloat( Rb.tofloat() );
            break;
    }
    return true;
}

/*static*/
bool Processor::Pipeline::ExecuteStage::ExecuteFLTL(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func)
{
    Rcv.m_state = RST_FULL;
    const Float64& Ra = Rav.m_float._64;
    const Float64& Rb = Rbv.m_float._64;
    Float64&       Rc = Rcv.m_float._64;

    switch(func)
    {
        // Convert Integer to Integer
        case A_FLTIFUNC_CVTQL_V:
        case A_FLTIFUNC_CVTQL_SV:
        case A_FLTIFUNC_CVTQL: Rc.integer =           ((Rb.integer & 0xC0000000) << 32) | ((Rb.integer & 0x3FFFFFFF) << 29); break;
        case A_FLTIFUNC_CVTLQ: Rc.integer = (int64_t)(((Rb.integer >> 32) & 0xC0000000) | ((Rb.integer >> 29) & 0x03FFFFFF)); break;

        // Copy sign
        case A_FLTIFUNC_CPYS:  Rc.sign =  Ra.sign; Rc.exponent = Rb.exponent; Rc.fraction = Rb.fraction; break;
        case A_FLTIFUNC_CPYSN: Rc.sign = ~Ra.sign; Rc.exponent = Rb.exponent; Rc.fraction = Rb.fraction; break;
        case A_FLTIFUNC_CPYSE: Rc.sign =  Ra.sign; Rc.exponent = Ra.exponent; Rc.fraction = Rb.fraction; break;

        // Move from/to Floating-Point Control Register
        case A_FLTIFUNC_MT_FPCR:
        case A_FLTIFUNC_MF_FPCR:
            break;

        // Floating-Point Conditional Move
        case A_FLTIFUNC_FCMOVEQ: if (BranchTaken(A_OP_FBEQ, Rav)) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_FLTIFUNC_FCMOVNE: if (BranchTaken(A_OP_FBNE, Rav)) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_FLTIFUNC_FCMOVLT: if (BranchTaken(A_OP_FBLT, Rav)) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_FLTIFUNC_FCMOVGE: if (BranchTaken(A_OP_FBGE, Rav)) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_FLTIFUNC_FCMOVLE: if (BranchTaken(A_OP_FBLE, Rav)) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_FLTIFUNC_FCMOVGT: if (BranchTaken(A_OP_FBGT, Rav)) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
            break;
    }
    return true;
}

/*static*/
bool Processor::Pipeline::ExecuteStage::ExecuteITFP(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& /* Rbv */, int func)
{
    Rcv.m_state = RST_FULL;
    switch (func)
    {
        // Integer Register to Floating-Point Register Move
        case A_ITFPFUNC_ITOFF:
            break;

        case A_ITFPFUNC_ITOFS:
        case A_ITFPFUNC_ITOFT:
            {
                int size = (func == A_ITFPFUNC_ITOFS) ? 4 : 8;
                char data[sizeof(Integer)];
                SerializeRegister(RT_INTEGER, Rav.m_integer.get(Rav.m_size), data, size);
                Rcv.m_float._64.integer = UnserializeRegister(RT_FLOAT, data, size);
                break;
            }

        default:
            // Square Root is done in the FPU
            return false;
    }
    return true;
}

template <typename T>
static T BITS(const T& val, int offset, int size)
{
    const size_t s = (sizeof(T) * 8) - size;
    return (val << (s - offset)) >> s;
}

static uint64_t MASK1(int offset, int size)
{
    return ((1ULL << size) - 1) << offset;
}

/*static*/
bool Processor::Pipeline::ExecuteStage::ExecuteFPTI(PipeValue& Rcv, const PipeValue& Rav_, const PipeValue& Rbv_, int func)
{
    uint64_t Rav = Rav_.m_integer.get(Rav_.m_size);
    uint64_t Rbv = Rbv_.m_integer.get(Rbv_.m_size);

    Rcv.m_state   = RST_FULL;
    Rcv.m_integer = 0;
    switch (func)
    {
        // Count Leading Zero
        case A_FPTIFUNC_CTLZ:
        {
            unsigned int count = 0;
            for (int i = 63; i > 0; i--) {
                if ((Rbv >> i) & 1) {
                    break;
                }
                ++count;
            }
            Rcv.m_integer = count;
            break;
        }

        // Count Population
        case A_FPTIFUNC_CTPOP:
        {
            unsigned int count = 0;
            for (int i = 0; i < 63; ++i) {
                if ((Rbv >> i) & 1) {
                    ++count;
                }
            }
            Rcv.m_integer = count;
            break;
        }

        // Count Trailing Zero
        case A_FPTIFUNC_CTTZ:
        {
            unsigned int count = 0;
            for (int i = 0; i < 63; ++i) {
                if ((Rbv >> i) & 1) {
                    break;
                }
                ++count;
            }
            Rcv.m_integer = count;
            break;
        }

        // Sign Extend
        case A_FPTIFUNC_SEXTB: Rcv.m_integer = (int64_t)( int8_t)Rbv; break;
        case A_FPTIFUNC_SEXTW: Rcv.m_integer = (int64_t)(int16_t)Rbv; break;

        // Floating-Point Register to Integer Register Move
        case A_FPTIFUNC_FTOIS:
        case A_FPTIFUNC_FTOIT:
            {
                int size = (func == A_FPTIFUNC_FTOIS) ? 4 : 8;
                char data[8];
                SerializeRegister(RT_FLOAT, Rav_.m_float._64.integer, data, size);
                Rcv.m_integer = UnserializeRegister(RT_INTEGER, data, size);
                break;
            }

        // Pixel error
        case A_FPTIFUNC_PERR:
            for (int i = 0; i < 64; i += 8) {
                uint8_t a = (uint8_t)BITS<uint64_t>(Rav, i, 8);
                uint8_t b = (uint8_t)BITS<uint64_t>(Rbv, i, 8);
                Rcv.m_integer = Rcv.m_integer.get(Rcv.m_size) + (a >= b ? a - b : b - a);
            }
            break;

        // Pack Bytes
        case A_FPTIFUNC_PKLB: Rcv.m_integer = (BITS<uint64_t>(Rbv,  0, 8) <<  0) | (BITS<uint64_t>(Rbv, 32, 8) <<  8); break;
        case A_FPTIFUNC_PKWB: Rcv.m_integer = (BITS<uint64_t>(Rbv,  0, 8) <<  0) | (BITS<uint64_t>(Rbv, 16, 8) <<  8) |
                                              (BITS<uint64_t>(Rbv, 32, 8) << 16) | (BITS<uint64_t>(Rbv, 48, 8) << 24); break;

        // Unpack Bytes
        case A_FPTIFUNC_UNPKBL: Rcv.m_integer = (BITS<uint64_t>(Rbv,  0, 8) <<  0) | (BITS<uint64_t>(Rbv,  8, 8) << 32); break;
        case A_FPTIFUNC_UNPKBW: Rcv.m_integer = (BITS<uint64_t>(Rbv,  0, 8) <<  0) | (BITS<uint64_t>(Rbv,  8, 8) << 16) |
                                                (BITS<uint64_t>(Rbv, 16, 8) << 32) | (BITS<uint64_t>(Rbv, 24, 8) << 48); break;

        case A_FPTIFUNC_MINSB8:
        case A_FPTIFUNC_MINSW4:
        case A_FPTIFUNC_MAXSB8:
        case A_FPTIFUNC_MAXSW4:
        {
            int step = (func == A_FPTIFUNC_MINSB8 || func == A_FPTIFUNC_MAXSB8 ? 8 : 16);
            const int64_t& (*cmp)(const int64_t&,const int64_t&) = std::max<int64_t>;
            if (func == A_FPTIFUNC_MINSB8 || func == A_FPTIFUNC_MINSW4) cmp = std::min<int64_t>;
            uint64_t result = 0;
            for (int i = 0; i < 64; i += step) {
                result |= (cmp(BITS<int64_t>(Rav, i, step), BITS<int64_t>(Rbv, i, step)) & MASK1(0,step)) << i;
            }
            Rcv.m_integer = result;
            break;
        }

        case A_FPTIFUNC_MINUB8:
        case A_FPTIFUNC_MINUW4:
        case A_FPTIFUNC_MAXUB8:
        case A_FPTIFUNC_MAXUW4:
        {
            int step = (func == A_FPTIFUNC_MINUB8 || func == A_FPTIFUNC_MAXUB8 ? 8 : 16);
            const uint64_t& (*cmp)(const uint64_t&,const uint64_t&) = std::max<uint64_t>;
            if (func == A_FPTIFUNC_MINUB8 || func == A_FPTIFUNC_MINUW4) cmp = std::min<uint64_t>;
            uint64_t result = 0;
            for (int i = 0; i < 64; i += step) {
                result |= (cmp(BITS<uint64_t>(Rav, i, step), BITS<uint64_t>(Rbv, i, step)) & MASK1(0,step)) << i;
            }
            Rcv.m_integer = result;
            break;
        }
    }
    return true;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecuteInstruction()
{
    uint64_t Rav = m_input.Rav.m_integer.get(m_input.Rav.m_size);
    uint64_t Rbv = m_input.Rbv.m_integer.get(m_input.Rbv.m_size);

    switch (m_input.format)
    {
        case IFORMAT_BRA:
        {
            MemAddr  next         = m_input.pc + sizeof(Instruction);
            MemAddr  target       = next + (MemAddr)m_input.displacement * sizeof(Instruction);
            if (m_input.opcode == A_OP_CREATE_D)
            {
                // Direct create
                return ExecCreate(m_parent.GetProcessor().UnpackFID(Rav), target, m_input.Rc.index);
            }

            // Conditional and unconditional branches
            if (BranchTaken(m_input.opcode, m_input.Rav))
            {
                COMMIT
                {
                    if (m_input.opcode == A_OP_BR || m_input.opcode == A_OP_BSR)
                    {
                        // Store the address of the next instruction for BR and BSR
                        MemAddr retaddr = next;
                        if (!m_input.legacy && (retaddr & (m_parent.GetProcessor().GetICache().GetLineSize()-1)) == 0)
                        {
                            // If the next PC is at a cache-line boundary, skip the control word
                            // NB: we need to adjust this here, *even though the fetch stage does it too*
                            // because GP-addressing requires to compute GP from the real instruction
                            // offset stored in a register.
                            retaddr += sizeof(Instruction);
                        }
                        m_output.Rcv.m_integer = retaddr;
                        m_output.Rcv.m_state   = RST_FULL;
                    }
                }

                // We've branched, ignore the thread end
                COMMIT{ m_output.kill = false; }

                if (target != next)
                {
                    COMMIT
                    {
                        DebugFlowWrite("F%u/T%u(%llu) %s branch %s",
                                       (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                                       m_parent.GetProcessor().GetSymbolTable()[target].c_str());
                        m_output.pc   = target;
                        m_output.swch = true;
                    }
                    return PIPE_FLUSH;
                }
            }
        }
        break;

        case IFORMAT_JUMP:
        {
            MemAddr next   = m_input.pc + sizeof(Instruction);
            MemAddr target = Rbv & -(MemAddr)sizeof(Instruction);

            if (m_input.opcode == A_OP_CREATE_I)
            {
                return ExecCreate(m_parent.GetProcessor().UnpackFID(Rav), target, m_input.Rc.index);
            }

            // Unconditional Jumps
            COMMIT
            {
                DebugFlowWrite("F%u/T%u(%llu) %s branch %s",
                               (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                               m_parent.GetProcessor().GetSymbolTable()[target].c_str());

                // Store the address of the next instruction
                if (!m_input.legacy && (next & (m_parent.GetProcessor().GetICache().GetLineSize()-1)) == 0)
                {
                    // If the next PC is at a cache-line boundary, skip the control word.
                    // NB: we need to adjust this here, *even though the fetch stage does it too*
                    // because GP-addressing requires to compute GP from the real instruction
                    // offset stored in a register.
                    next += sizeof(Instruction);
                }
                m_output.Rcv.m_integer = next;
                m_output.Rcv.m_state   = RST_FULL;

                m_output.pc   = target;
                m_output.swch = true;
                m_output.kill = false;
            }
        }
        return PIPE_FLUSH;

        case IFORMAT_MEM_LOAD:
        case IFORMAT_MEM_STORE:
        COMMIT
        {
            // Create, LDA, LDAH and Memory reads and writes
            switch (m_input.opcode)
            {
            case A_OP_LDAH:
            case A_OP_LDA:
            {
                int64_t displacement  = (m_input.opcode == A_OP_LDAH) ? m_input.displacement << 16 : m_input.displacement;
                m_output.Rcv.m_integer = Rbv + displacement;
                m_output.Rcv.m_state   = RST_FULL;
                break;
            }

            default:
                m_output.address     = (MemAddr)(Rbv + m_input.displacement);
                m_output.sign_extend = false;
                switch (m_input.opcode)
                {
                    case A_OP_STB:   case A_OP_LDBU:  m_output.size = 1; break;
                    case A_OP_STW:   case A_OP_LDWU:  m_output.size = 2; break;
                    case A_OP_STS:   case A_OP_LDS:
                    case A_OP_STF:   case A_OP_LDF:
                    case A_OP_STL:   case A_OP_LDL:   m_output.size = 4; break;
                    case A_OP_STQ_U: case A_OP_LDQ_U: m_output.address &= ~7;
                    case A_OP_STT:   case A_OP_LDT:
                    case A_OP_STG:   case A_OP_LDG:
                    case A_OP_STQ:   case A_OP_LDQ:   m_output.size = 8; break;
                }

                if (m_output.size > 0)
                {
                    if (m_input.format == IFORMAT_MEM_LOAD)
                    {
                        // EX stage doesn't produce a value for loads
                        m_output.Rcv.m_state = RST_INVALID;
                    }
                    else
                    {
                        // Put the value of Ra into Rcv for storage by memory stage
                        m_output.Rcv = m_input.Rav;

                        // Also remember the source operand (for memory debugging only)
                        m_output.Ra  = m_input.Ra;
                    }
                }
                break;
            }
        }
        break;

    case IFORMAT_OP:
    case IFORMAT_FPOP:
        if (m_input.opcode == A_OP_UTHREAD)
        {
            if ((m_input.function & A_UTHREAD_DC_MASK) == A_UTHREAD_DC_VALUE)
            {
                COMMIT {
                    m_output.Rcv.m_state   = RST_FULL;
                    switch (m_input.function)
                    {
                    case A_UTHREAD_LDBP: m_output.Rcv.m_integer = m_parent.GetProcessor().GetTLSAddress(m_input.fid, m_input.tid); break;
                    case A_UTHREAD_LDFP:
                    {
                        const MemAddr tls_base = m_parent.GetProcessor().GetTLSAddress(m_input.fid, m_input.tid);
                        const MemAddr tls_size = m_parent.GetProcessor().GetTLSSize();
                        m_output.Rcv.m_integer = tls_base + tls_size;
                        break;
                    }
                    case A_UTHREAD_GETFID: m_output.Rcv.m_integer = m_input.fid; break;
                    case A_UTHREAD_GETTID: m_output.Rcv.m_integer = m_input.tid; break;
                    case A_UTHREAD_GETCID: m_output.Rcv.m_integer = m_parent.GetProcessor().GetPID(); break;
                    case A_UTHREAD_GETPID:
                    {
                        PlaceID place;
                        place.size = m_input.placeSize;
                        place.pid  = m_parent.GetProcessor().GetPID() & -place.size;
                        place.capability = 0x1337; // later: find a proper substitute
                        m_output.Rcv.m_integer = m_parent.GetProcessor().PackPlace(place);
                        break;
                    }
                    case A_UTHREAD_GETASR: m_output.Rcv.m_integer = m_parent.GetProcessor().ReadASR(Rbv); break;
                    case A_UTHREAD_GETAPR: m_output.Rcv.m_integer = m_parent.GetProcessor().ReadAPR(Rbv); break;
                    }
                }
            }
            else if ((m_input.function & A_UTHREAD_DZ_MASK) == A_UTHREAD_DZ_VALUE)
            {
                COMMIT{ m_output.Rc = INVALID_REG; }
                switch (m_input.function)
                {
                case A_UTHREAD_BREAK:    ExecBreak(); break;
                case A_UTHREAD_PRINT:    COMMIT{ ExecDebug(Rav, Rbv); }; break;
                }
            }
            else if ((m_input.function & A_UTHREAD_REMOTE_MASK) == A_UTHREAD_REMOTE_VALUE)
            {
                const FID fid = m_parent.GetProcessor().UnpackFID(Rav);
                switch (m_input.function)
                {
                case A_UTHREAD_SETSTART: return SetFamilyProperty(fid, FAMPROP_START, Rbv);
                case A_UTHREAD_SETLIMIT: return SetFamilyProperty(fid, FAMPROP_LIMIT, Rbv);
                case A_UTHREAD_SETSTEP:  return SetFamilyProperty(fid, FAMPROP_STEP,  Rbv);
                case A_UTHREAD_SETBLOCK: return SetFamilyProperty(fid, FAMPROP_BLOCK, Rbv);
                case A_UTHREAD_PUTG:     return WriteFamilyRegister(RRT_GLOBAL,          RT_INTEGER, fid, m_input.regofs);
                case A_UTHREAD_PUTS:     return WriteFamilyRegister(RRT_FIRST_DEPENDENT, RT_INTEGER, fid, m_input.regofs);
                case A_UTHREAD_DETACH:   return ExecDetach(fid);

                case A_UTHREAD_SYNC:     return ExecSync(fid);
                case A_UTHREAD_GETG:     return ReadFamilyRegister(RRT_GLOBAL,      RT_INTEGER, fid, m_input.regofs);
                case A_UTHREAD_GETS:     return ReadFamilyRegister(RRT_LAST_SHARED, RT_INTEGER, fid, m_input.regofs);
                }
            }
            else if ((m_input.function & A_UTHREAD_ALLOC_MASK) == A_UTHREAD_ALLOC_VALUE)
            {
                Integer flags  = Rbv;
                PlaceID place  = m_parent.GetProcessor().UnpackPlace(Rav);
                bool suspend   = (m_input.function & A_UTHREAD_ALLOC_S_MASK);
                bool exclusive = (m_input.function & A_UTHREAD_ALLOC_X_MASK);

                return ExecAllocate(place, m_input.Rc.index, suspend, exclusive, flags);
            }
            else if ((m_input.function & A_UTHREAD_CREB_MASK) == A_UTHREAD_CREB_VALUE)
            {
                return ExecBundle(Rav, (m_input.function == A_CREATE_B_I), Rbv, m_input.Rc.index);
            }
        }
        else if (m_input.opcode == A_OP_UTHREADF)
        {
            if (m_input.function == A_UTHREADF_PRINT)
            {
                COMMIT {
                    ExecDebug(m_input.Rbv.m_float.tofloat(m_input.Rbv.m_size), Rav);
                }
            }
            else
            {
                const FID fid = m_parent.GetProcessor().UnpackFID(Rav);
                switch(m_input.function)
                {
                case A_UTHREADF_PUTG: return WriteFamilyRegister(RRT_GLOBAL,          RT_FLOAT, fid, m_input.regofs);
                case A_UTHREADF_PUTS: return WriteFamilyRegister(RRT_FIRST_DEPENDENT, RT_FLOAT, fid, m_input.regofs);
                case A_UTHREADF_GETG: return ReadFamilyRegister(RRT_GLOBAL,           RT_FLOAT, fid, m_input.regofs);
                case A_UTHREADF_GETS: return ReadFamilyRegister(RRT_LAST_SHARED,      RT_FLOAT, fid, m_input.regofs);
                }
            }
        }
        else
        {
            bool (Processor::Pipeline::ExecuteStage::*execfunc)(PipeValue&, const PipeValue&, const PipeValue&, int) = NULL;

            FPUOperation fpuop = FPU_OP_NONE;
            switch (m_input.opcode)
            {
                case A_OP_INTA: execfunc = &Processor::Pipeline::ExecuteStage::ExecuteINTA; break;
                case A_OP_INTL: execfunc = &Processor::Pipeline::ExecuteStage::ExecuteINTL; break;
                case A_OP_INTS: execfunc = &Processor::Pipeline::ExecuteStage::ExecuteINTS; break;
                case A_OP_INTM: execfunc = &Processor::Pipeline::ExecuteStage::ExecuteINTM; break;
                case A_OP_FLTV: execfunc = &Processor::Pipeline::ExecuteStage::ExecuteFLTV; break;
                case A_OP_FLTL: execfunc = &Processor::Pipeline::ExecuteStage::ExecuteFLTL; break;
                case A_OP_FPTI: execfunc = &Processor::Pipeline::ExecuteStage::ExecuteFPTI; break;
                case A_OP_ITFP:
                    execfunc = &Processor::Pipeline::ExecuteStage::ExecuteITFP;
                    switch (m_input.function)
                    {
                        // IEEE Floating Square Root
                        case A_ITFPFUNC_SQRTS:      case A_ITFPFUNC_SQRTS_C:    case A_ITFPFUNC_SQRTS_D:    case A_ITFPFUNC_SQRTS_M:
                        case A_ITFPFUNC_SQRTS_SU:   case A_ITFPFUNC_SQRTS_SUC:  case A_ITFPFUNC_SQRTS_SUD:  case A_ITFPFUNC_SQRTS_SUIC:
                        case A_ITFPFUNC_SQRTS_SUID: case A_ITFPFUNC_SQRTS_SUIM: case A_ITFPFUNC_SQRTS_SUM:  case A_ITFPFUNC_SQRTS_SUU:
                        case A_ITFPFUNC_SQRTS_U:    case A_ITFPFUNC_SQRTS_UC:   case A_ITFPFUNC_SQRTS_UD:   case A_ITFPFUNC_SQRTS_UM:
                        case A_ITFPFUNC_SQRTT:      case A_ITFPFUNC_SQRTT_C:    case A_ITFPFUNC_SQRTT_D:    case A_ITFPFUNC_SQRTT_M:
                        case A_ITFPFUNC_SQRTT_SU:   case A_ITFPFUNC_SQRTT_SUC:  case A_ITFPFUNC_SQRTT_SUD:  case A_ITFPFUNC_SQRTT_SUI:
                        case A_ITFPFUNC_SQRTT_SUIC: case A_ITFPFUNC_SQRTT_SUID: case A_ITFPFUNC_SQRTT_SUIM: case A_ITFPFUNC_SQRTT_SUM:
                        case A_ITFPFUNC_SQRTT_U:    case A_ITFPFUNC_SQRTT_UC:   case A_ITFPFUNC_SQRTT_UD:   case A_ITFPFUNC_SQRTT_UM:
                            fpuop = FPU_OP_SQRT;
                            break;
                    }
                    break;

                case A_OP_FLTI:
                    execfunc = &Processor::Pipeline::ExecuteStage::ExecuteFLTI;
                    switch (m_input.function)
                    {
                        case A_FLTIFUNC_ADDS:      case A_FLTIFUNC_ADDS_C:    case A_FLTIFUNC_ADDS_D:   case A_FLTIFUNC_ADDS_M:
                        case A_FLTIFUNC_ADDS_SU:   case A_FLTIFUNC_ADDS_SUC:  case A_FLTIFUNC_ADDS_SUD: case A_FLTIFUNC_ADDS_SUI:
                        case A_FLTIFUNC_ADDS_SUIC: case A_FLTIFUNC_ADDS_SUIM: case A_FLTIFUNC_ADDS_SUM: case A_FLTIFUNC_ADDS_U:
                        case A_FLTIFUNC_ADDS_UC:   case A_FLTIFUNC_ADDS_UD:   case A_FLTIFUNC_ADDS_UM:
                        case A_FLTIFUNC_ADDT:      case A_FLTIFUNC_ADDT_C:    case A_FLTIFUNC_ADDT_D:   case A_FLTIFUNC_ADDT_M:
                        case A_FLTIFUNC_ADDT_SU:   case A_FLTIFUNC_ADDT_SUC:  case A_FLTIFUNC_ADDT_SUD: case A_FLTIFUNC_ADDT_SUI:
                        case A_FLTIFUNC_ADDT_SUIC: case A_FLTIFUNC_ADDT_SUIM: case A_FLTIFUNC_ADDT_SUM: case A_FLTIFUNC_ADDT_U:
                        case A_FLTIFUNC_ADDT_UC:   case A_FLTIFUNC_ADDT_UD:   case A_FLTIFUNC_ADDT_UM:
                            fpuop = FPU_OP_ADD;
                            break;

                        case A_FLTIFUNC_SUBS:      case A_FLTIFUNC_SUBS_C:    case A_FLTIFUNC_SUBS_D:   case A_FLTIFUNC_SUBS_M:
                        case A_FLTIFUNC_SUBS_SU:   case A_FLTIFUNC_SUBS_SUC:  case A_FLTIFUNC_SUBS_SUD: case A_FLTIFUNC_SUBS_SUI:
                        case A_FLTIFUNC_SUBS_SUIC: case A_FLTIFUNC_SUBS_SUIM: case A_FLTIFUNC_SUBS_SUM: case A_FLTIFUNC_SUBS_U:
                        case A_FLTIFUNC_SUBS_UC:   case A_FLTIFUNC_SUBS_UD:   case A_FLTIFUNC_SUBS_UM:
                        case A_FLTIFUNC_SUBT:      case A_FLTIFUNC_SUBT_C:    case A_FLTIFUNC_SUBT_D:   case A_FLTIFUNC_SUBT_M:
                        case A_FLTIFUNC_SUBT_SU:   case A_FLTIFUNC_SUBT_SUC:  case A_FLTIFUNC_SUBT_SUD: case A_FLTIFUNC_SUBT_SUI:
                        case A_FLTIFUNC_SUBT_SUIC: case A_FLTIFUNC_SUBT_SUIM: case A_FLTIFUNC_SUBT_SUM: case A_FLTIFUNC_SUBT_U:
                        case A_FLTIFUNC_SUBT_UC:   case A_FLTIFUNC_SUBT_UD:   case A_FLTIFUNC_SUBT_UM:
                            fpuop = FPU_OP_SUB;
                            break;

                        case A_FLTIFUNC_MULS:      case A_FLTIFUNC_MULS_C:    case A_FLTIFUNC_MULS_D:   case A_FLTIFUNC_MULS_M:
                        case A_FLTIFUNC_MULS_SU:   case A_FLTIFUNC_MULS_SUC:  case A_FLTIFUNC_MULS_SUD: case A_FLTIFUNC_MULS_SUI:
                        case A_FLTIFUNC_MULS_SUIC: case A_FLTIFUNC_MULS_SUIM: case A_FLTIFUNC_MULS_SUM: case A_FLTIFUNC_MULS_U:
                        case A_FLTIFUNC_MULS_UC:   case A_FLTIFUNC_MULS_UD:   case A_FLTIFUNC_MULS_UM:
                        case A_FLTIFUNC_MULT:      case A_FLTIFUNC_MULT_C:    case A_FLTIFUNC_MULT_D:   case A_FLTIFUNC_MULT_M:
                        case A_FLTIFUNC_MULT_SU:   case A_FLTIFUNC_MULT_SUC:  case A_FLTIFUNC_MULT_SUD: case A_FLTIFUNC_MULT_SUI:
                        case A_FLTIFUNC_MULT_SUIC: case A_FLTIFUNC_MULT_SUIM: case A_FLTIFUNC_MULT_SUM: case A_FLTIFUNC_MULT_U:
                        case A_FLTIFUNC_MULT_UC:   case A_FLTIFUNC_MULT_UD:   case A_FLTIFUNC_MULT_UM:
                            fpuop = FPU_OP_MUL;
                            break;

                        case A_FLTIFUNC_DIVS:      case A_FLTIFUNC_DIVS_C:    case A_FLTIFUNC_DIVS_D:   case A_FLTIFUNC_DIVS_M:
                        case A_FLTIFUNC_DIVS_SU:   case A_FLTIFUNC_DIVS_SUC:  case A_FLTIFUNC_DIVS_SUD: case A_FLTIFUNC_DIVS_SUI:
                        case A_FLTIFUNC_DIVS_SUIC: case A_FLTIFUNC_DIVS_SUIM: case A_FLTIFUNC_DIVS_SUM: case A_FLTIFUNC_DIVS_U:
                        case A_FLTIFUNC_DIVS_UC:   case A_FLTIFUNC_DIVS_UD:   case A_FLTIFUNC_DIVS_UM:
                        case A_FLTIFUNC_DIVT:      case A_FLTIFUNC_DIVT_C:    case A_FLTIFUNC_DIVT_D:   case A_FLTIFUNC_DIVT_M:
                        case A_FLTIFUNC_DIVT_SU:   case A_FLTIFUNC_DIVT_SUC:  case A_FLTIFUNC_DIVT_SUD: case A_FLTIFUNC_DIVT_SUI:
                        case A_FLTIFUNC_DIVT_SUIC: case A_FLTIFUNC_DIVT_SUIM: case A_FLTIFUNC_DIVT_SUM: case A_FLTIFUNC_DIVT_U:
                        case A_FLTIFUNC_DIVT_UC:   case A_FLTIFUNC_DIVT_UD:   case A_FLTIFUNC_DIVT_UM:
                            fpuop = FPU_OP_DIV;
                            break;
                    }
                    break;
            }

            PipeValue Rcv;
            Rcv.m_size = m_input.RcSize;
            if ((this->*execfunc)(Rcv, m_input.Rav, m_input.Rbv, m_input.function))
            {
                // Operation completed
                COMMIT {
                    m_output.Rcv = Rcv;
                }
            }
            else
            {
                assert(fpuop != FPU_OP_NONE);

                // Dispatch long-latency operation to FPU
                if (!m_fpu.QueueOperation(m_fpuSource, fpuop, 8,
                    m_input.Rav.m_float.tofloat(m_input.Rav.m_size),
                    m_input.Rbv.m_float.tofloat(m_input.Rbv.m_size),
                    m_input.Rc))
                {
                    DeadlockWrite("F%u/T%u(%llu) %s unable to queue FP operation %u on %s for %s",
                                  (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                                  (unsigned)fpuop, m_fpu.GetFQN().c_str(), m_input.Rc.str().c_str());

                    return PIPE_STALL;
                }

                COMMIT
                {
                    m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_output.Rcv.m_size);

                    // We've executed a floating point operation
                    m_flop++;
                }
            }
        }
        break;

    case IFORMAT_MISC:
        switch (m_input.function)
        {
            case A_MISCFUNC_MB:
            case A_MISCFUNC_WMB:
                // Memory barrier
                if (!MemoryWriteBarrier(m_input.tid))
                {
                    // Suspend thread at our PC
                    COMMIT {
                        m_output.pc      = m_input.pc;
                        m_output.suspend = SUSPEND_MEMORY_BARRIER;
                        m_output.swch    = true;
                        m_output.kill    = false;
                        m_output.Rc      = INVALID_REG;
                    }
                }
                return PIPE_FLUSH;

            case A_MISCFUNC_RPCC:
                // Read processor cycle count
            // NOTE: the Alpha spec specifies that the higher 32-bits
                // are operating-system dependent. In our case we stuff
                // extra precision from the cycle counter.
                COMMIT {
                    m_output.Rcv.m_state   = RST_FULL;
                    m_output.Rcv.m_integer = GetCycleNo();
                }
                break;
        }
        break;

    default:
        ThrowIllegalInstructionException(*this, m_input.pc, "Unknown instruction format: %#x", (int)m_input.format);
        break;
    }
    return PIPE_CONTINUE;
}

}
