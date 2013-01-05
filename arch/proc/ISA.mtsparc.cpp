#include "Processor.h"
#include <arch/FPU.h>
#include <arch/symtable.h>
#include <programs/mgsim.h>

#include <cassert>
#include <cmath>
#include <sstream>
#include <iomanip>
using namespace std;

namespace Simulator
{
static const int RA_SHIFT       = 14;
static const int RB_SHIFT       = 0;
static const int RC_SHIFT       = 25;
static const int REG_MASK       = 0x1F;
static const int OP1_SHIFT      = 30;
static const int OP1_MASK       = 0x3;
static const int OP2_SHIFT      = 22;
static const int OP2_MASK       = 0x7;
static const int OP3_SHIFT      = 19;
static const int OP3_MASK       = 0x3F;
static const int COND_SHIFT     = 25;
static const int COND_MASK      = 0xF;
static const int ASI_SHIFT      = 5;
static const int ASI_MASK       = 0xFF;
static const int UTASI_SHIFT    = 5;
static const int UTASI_MASK     = 0xF;
static const int BIT_IMMEDIATE  = (1 << 13);
static const int IMM30_SHIFT    = 0;
static const int IMM30_SIZE     = 30;
static const int IMM30_MASK     = (1 << IMM30_SIZE) - 1;
static const int IMM22_SHIFT    = 0;
static const int IMM22_SIZE     = 22;
static const int IMM22_MASK     = (1 << IMM22_SIZE) - 1;
static const int IMM13_SHIFT    = 0;
static const int IMM13_SIZE     = 13;
static const int IMM13_MASK     = (1 << IMM13_SIZE) - 1;
static const int IMM9_SHIFT     = 0;
static const int IMM9_SIZE      = 9;
static const int IMM9_MASK      = (1 << IMM9_SIZE) - 1;
static const int OPF_SHIFT      = 5;
static const int OPF_MASK       = (1 << 9) - 1;
static const int OPT_SHIFT      = 9;
static const int OPT_MASK       = (1 << 4) - 1;

// Function for naming local registers according to a standard ABI
const vector<string>& GetDefaultLocalRegisterAliases(RegType type)
{
    static const vector<string> intnames = {
        "g1", "g2", "g3", "g4", "g5", "g6", "g7",
        "o0", "o1", "o2", "o3", "o4", "o5", "sp", "o7",
        "l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7",
        "i0", "i1", "i2", "i3", "i4", "i5", "fp", "i7", "g0" };
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

    if (addr > 0)
    {
        addr--;
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
    }
    *rc = RC_RAZ;
    return 0;
}

// Sign extend the value, which has the specified size
static int32_t SEXT(uint32_t value, int bits)
{
    bits = 32 - bits;
    return (int32_t)(value << bits) >> bits;
}

void Processor::Pipeline::DecodeStage::DecodeInstruction(const Instruction& instr)
{
    m_output.op1 = (uint8_t)((instr >> OP1_SHIFT) & OP1_MASK);
    RegIndex Ra  = (instr >> RA_SHIFT) & REG_MASK;
    RegIndex Rb  = (instr >> RB_SHIFT) & REG_MASK;
    RegIndex Rc  = (instr >> RC_SHIFT) & REG_MASK;

    m_output.Ra = INVALID_REG;
    m_output.Rb = INVALID_REG;
    m_output.Rc = INVALID_REG;
    m_output.Rs = INVALID_REG;
    m_output.literal = 0;
    switch (m_output.op1)
    {
    case S_OP1_BRANCH:
        // SETHI & Branches
        m_output.op2 = (uint8_t)((instr >> OP2_SHIFT) & OP2_MASK);
        switch (m_output.op2)
        {
        case S_OP2_SETHI:
            // We need to read it first
            m_output.literal = (instr >> IMM22_SHIFT) & IMM22_MASK;
            m_output.Rc      = MAKE_REGADDR(RT_INTEGER, Rc);
            break;

        default:
            // We don't care about the annul bit (not supported; obviously this presents a problem with legacy code later)
            m_output.displacement = SEXT((instr >> IMM22_SHIFT) & IMM22_MASK, IMM22_SIZE);
            m_output.function     = (uint16_t)((instr >> COND_SHIFT) & COND_MASK);
            break;
        }
        break;

    case S_OP1_CALL:
        m_output.displacement = SEXT((instr >> IMM30_SHIFT) & IMM30_MASK, IMM30_SIZE);
        m_output.Rc = MAKE_REGADDR(RT_INTEGER, 15);
        break;

    case S_OP1_MEMORY:
        m_output.op3 = (uint8_t)((instr >> OP3_SHIFT) & OP3_MASK);
        m_output.Ra  = MAKE_REGADDR(RT_INTEGER, Ra);
        if (instr & BIT_IMMEDIATE) {
            m_output.literal = SEXT((instr >> IMM13_SHIFT) & IMM13_MASK, IMM13_SIZE);
        } else  {
            m_output.asi = (uint8_t)((instr >> ASI_SHIFT) & ASI_MASK);
            m_output.Rb  = MAKE_REGADDR(RT_INTEGER, Rb);
        }

        // Determine operation size
        switch (m_output.op3)
        {
            case S_OP3_STB:  case S_OP3_LDUB:  case S_OP3_LDSB:
            case S_OP3_STBA: case S_OP3_LDSBA: case S_OP3_LDUBA:
            case S_OP3_STH:  case S_OP3_LDUH:  case S_OP3_LDSH:
            case S_OP3_STHA: case S_OP3_LDSHA: case S_OP3_LDUHA:
            case S_OP3_ST:   case S_OP3_LD:
            case S_OP3_STF:  case S_OP3_LDF:
            case S_OP3_STA:  case S_OP3_LDA:   m_output.RcSize = 4; break;
            case S_OP3_STD:  case S_OP3_LDD:
            case S_OP3_STDF: case S_OP3_LDDF:
            case S_OP3_STDA: case S_OP3_LDDA:  m_output.RcSize = 8; break;
            default:
                // We don't support this instruction (yet)
                break;
        }

        // Determine register type
        switch (m_output.op3)
        {
            case S_OP3_STF:  case S_OP3_LDF:
            case S_OP3_STDF: case S_OP3_LDDF:
                // FP load or store
                m_output.Rc  = MAKE_REGADDR(RT_FLOAT, Rc);
                break;

            default:
                // Integer load or store
                m_output.Rc  = MAKE_REGADDR(RT_INTEGER, Rc);
                break;
        }

        // Both loads and stores have three operands.
        // For stores Rc specifies the value to write to [Ra + Rb].
        // For loads we must load Rc to check if we're loading a pending register (which could cause problems).
        m_output.Rs     = m_output.Rc;
        m_output.RsSize = m_output.RcSize;

        // However, for stores we don't actually use Rc to write
        switch (m_output.op3)
        {
            case S_OP3_STB: case S_OP3_STBA:
            case S_OP3_STH: case S_OP3_STHA:
            case S_OP3_ST:  case S_OP3_STA:
            case S_OP3_STD: case S_OP3_STDA:
            case S_OP3_STF: case S_OP3_STDF:
                // Stores
                m_output.Rc = INVALID_REG;
                break;

            default:
                // Loads
                m_output.RaNotPending = true;
                break;
        }
        break;

    case S_OP1_OTHER:
        m_output.op3 = (uint8_t)((instr >> OP3_SHIFT) & OP3_MASK);
        m_output.asi = (uint8_t)((instr >> ASI_SHIFT) & ASI_MASK);
        switch (m_output.op3)
        {
        case S_OP3_FPOP1:
        case S_OP3_FPOP2:
            // FP operation
            m_output.function = (uint16_t)((instr >> OPF_SHIFT) & OPF_MASK);
            m_output.Ra = MAKE_REGADDR(RT_FLOAT, Ra);
            m_output.Rb = MAKE_REGADDR(RT_FLOAT, Rb);
            m_output.Rc = MAKE_REGADDR(RT_FLOAT, Rc);

            switch (m_output.function)
            {
            // Convert Int to FP
            case S_OPF_FITOS: m_output.RaSize = m_output.RcSize =  4; break;
            case S_OPF_FITOD: m_output.RaSize = m_output.RcSize =  8; break;
            case S_OPF_FITOQ: m_output.RaSize = m_output.RcSize = 16; break;

            // Convert FP to Int
            case S_OPF_FSTOI: m_output.RaSize = m_output.RbSize =  4; break;
            case S_OPF_FDTOI: m_output.RaSize = m_output.RbSize =  8; break;
            case S_OPF_FQTOI: m_output.RaSize = m_output.RbSize = 16; break;

            // Convert FP to FP
            case S_OPF_FSTOD: m_output.RaSize = m_output.RbSize =  4; m_output.RcSize =  8; break;
            case S_OPF_FSTOQ: m_output.RaSize = m_output.RbSize =  4; m_output.RcSize = 16; break;
            case S_OPF_FDTOS: m_output.RaSize = m_output.RbSize =  8; m_output.RcSize =  4; break;
            case S_OPF_FDTOQ: m_output.RaSize = m_output.RbSize =  8; m_output.RcSize = 16; break;
            case S_OPF_FQTOS: m_output.RaSize = m_output.RbSize = 16; m_output.RcSize =  4; break;
            case S_OPF_FQTOD: m_output.RaSize = m_output.RbSize = 16; m_output.RcSize =  8; break;

            case S_OPF_FPRINTS: m_output.RaSize = 4;  m_output.Rb = MAKE_REGADDR(RT_INTEGER, Rb); break;
            case S_OPF_FPRINTD: m_output.RaSize = 8;  m_output.Rb = MAKE_REGADDR(RT_INTEGER, Rb); break;
            case S_OPF_FPRINTQ: m_output.RaSize = 16; m_output.Rb = MAKE_REGADDR(RT_INTEGER, Rb); break;

            // FP Move
            case S_OPF_FMOV:
            case S_OPF_FNEG:
            case S_OPF_FABS: break;

            // FP Compare
            case S_OPF_FCMPS: case S_OPF_FCMPES: m_output.RcSize =  4; break;
            case S_OPF_FCMPD: case S_OPF_FCMPED: m_output.RcSize =  8; break;
            case S_OPF_FCMPQ: case S_OPF_FCMPEQ: m_output.RcSize = 16; break;

            case S_OPF_FSQRTS: case S_OPF_FADDS: case S_OPF_FSUBS:
            case S_OPF_FMULS:  case S_OPF_FDIVS: m_output.RaSize = m_output.RbSize = m_output.RcSize =  4; break;
            case S_OPF_FSQRTD: case S_OPF_FADDD: case S_OPF_FSUBD:
            case S_OPF_FMULD:  case S_OPF_FDIVD: m_output.RaSize = m_output.RbSize = m_output.RcSize =  8; break;
            case S_OPF_FSQRTQ: case S_OPF_FADDQ: case S_OPF_FSUBQ:
            case S_OPF_FMULQ:  case S_OPF_FDIVQ: m_output.RaSize = m_output.RbSize = m_output.RcSize = 16; break;

            case S_OPF_FSMULD: m_output.RaSize = m_output.RbSize = 4; m_output.RcSize =  8; break;
            case S_OPF_FDMULQ: m_output.RaSize = m_output.RbSize = 8; m_output.RcSize = 16; break;
            }
            break;

        case S_OP3_WRASR:
            // This instruction needs the Rc specifier, but we can't put it in the literal,
            // so we abuse the displacement field so the EX stage can use it
            m_output.Ra = MAKE_REGADDR(RT_INTEGER, Ra);
            m_output.displacement = Rc;
            m_output.function = (uint16_t)((instr >> OPT_SHIFT) & OPT_MASK);
            m_output.asi = (instr >> UTASI_SHIFT) & UTASI_MASK;

            if (instr & BIT_IMMEDIATE) {
                m_output.literal = ((Rc == 0x14/*ASR20*/) || (Rc == 0x13/*ASR19*/))
                    ? SEXT((instr >> IMM9_SHIFT ) & IMM9_MASK , IMM9_SIZE)
                    : SEXT((instr >> IMM13_SHIFT) & IMM13_MASK, IMM13_SIZE);
            } else {
                m_output.Rb = MAKE_REGADDR(RT_INTEGER, Rb);
            }

            if (Rc == 0x14 /*ASR20*/)
            {
                switch (m_output.function)
                {
                case S_OPT1_FPUTS:
                case S_OPT1_FPUTG:
                    m_output.Rb = MAKE_REGADDR(RT_FLOAT, Rb);
                    break;
                }
            }
            break;

        case S_OP3_RDASR:
            // This instruction needs the Ra specifier, so we put it in the
            // displacement just like for WRASR.
            m_output.displacement = Ra;
            m_output.function = (uint16_t)((instr >> OPT_SHIFT) & OPT_MASK);
            m_output.asi = (instr >> UTASI_SHIFT) & UTASI_MASK;

            if (instr & BIT_IMMEDIATE) {
                m_output.literal = SEXT((instr >> IMM9_SHIFT) & IMM9_MASK, IMM9_SIZE);
            } else {
                m_output.Rb  = MAKE_REGADDR(RT_INTEGER, Rb);
            }

            m_output.Rc = MAKE_REGADDR(RT_INTEGER, Rc);
            if (Ra == 0x13 /*ASR19*/)
            {
                switch(m_output.function)
                {
                case S_OPT2_CREBAS:
                case S_OPT2_CREBIS:
                    // Special case, Rc is input as well
                    m_output.Ra = MAKE_REGADDR(RT_INTEGER, Rc);
                    break;
                }
            }
            else if (Ra == 0x14 /*ASR20*/)
            {
                switch (m_output.function)
                {
                case S_OPT1_ALLOCATE:
                case S_OPT1_ALLOCATES:
                case S_OPT1_ALLOCATEX:
                case S_OPT1_CREATE:
                    // Special case, Rc is input as well
                    m_output.Ra = MAKE_REGADDR(RT_INTEGER, Rc);
                    break;

                case S_OPT1_FGETS:
                case S_OPT1_FGETG:
                    // Special case, Rc is really a float
                    m_output.Rc = MAKE_REGADDR(RT_FLOAT, Rc);
                    break;
                }
            }

            break;

        default:
            // Integer operation
            m_output.Ra = MAKE_REGADDR(RT_INTEGER, Ra);
            m_output.Rc = MAKE_REGADDR(RT_INTEGER, Rc);
            if (instr & BIT_IMMEDIATE) {
                m_output.literal = SEXT((instr >> IMM13_SHIFT) & IMM13_MASK, IMM13_SIZE);
            } else {
                m_output.Rb  = MAKE_REGADDR(RT_INTEGER, Rb);
            }
            break;
        }
        break;
    }
}

/*static*/
bool Processor::Pipeline::ExecuteStage::BranchTakenInt(int cond, uint32_t psr)
{
    const bool n = (psr & PSR_ICC_N) != 0; // Negative
    const bool z = (psr & PSR_ICC_Z) != 0; // Zero
    const bool v = (psr & PSR_ICC_V) != 0; // Overflow
    const bool c = (psr & PSR_ICC_C) != 0; // Carry
    bool b;
    switch (cond & 7)
    {
        default:
        case 0: b = false; break;        // BA, BN
        case 1: b = z; break;            // BNE, BE
        case 2: b = z || (n ^ v); break; // BG, BLE
        case 3: b = n ^ v; break;        // BGE, BL
        case 4: b = c || z; break;       // BGU, BLEU
        case 5: b = c; break;            // BCC, BCS
        case 6: b = n; break;            // BPOS, BNEG
        case 7: b = v; break;            // BVC, BVS
    }
    return (cond & 8) ? !b : b;
}

/*static*/
bool Processor::Pipeline::ExecuteStage::BranchTakenFlt(int cond, uint32_t fsr)
{
    const bool e = (fsr & FSR_FCC) == FSR_FCC_EQ; // Equal
    const bool l = (fsr & FSR_FCC) == FSR_FCC_LT; // Less than
    const bool g = (fsr & FSR_FCC) == FSR_FCC_GT; // Greater than
    const bool u = (fsr & FSR_FCC) == FSR_FCC_UO; // Unordered
    bool b = false;

    if (~cond & 7) return (cond & 8) != 0;   // FBA, FBN
    cond--;
    if (cond & 8) { b |= e; cond = ~cond; }
    if (cond & 4) b |= l;
    if (cond & 2) b |= g;
    if (cond & 1) b |= u;
    return b;
}

uint32_t Processor::Pipeline::ExecuteStage::ExecBasicInteger(int opcode, uint32_t Rav, uint32_t Rbv, uint32_t& Y, PSR& psr)
{
    uint64_t Rcv = 0;
    switch (opcode & 0xF)
    {
        // Logical operators
        case S_OP3_AND:     Rcv = Rav &  Rbv; break;
        case S_OP3_ANDN:    Rcv = Rav & ~Rbv; break;
        case S_OP3_OR:      Rcv = Rav |  Rbv; break;
        case S_OP3_ORN:     Rcv = Rav | ~Rbv; break;
        case S_OP3_XOR:     Rcv = Rav ^  Rbv; break;
        case S_OP3_XNOR:    Rcv = Rav ^ ~Rbv; break;

        // Addition & Subtraction
        case S_OP3_ADD:     Rcv = (int64_t)(int32_t)Rav + (int64_t)(int32_t)Rbv; break;
        case S_OP3_ADDX:    Rcv = (int64_t)(int32_t)Rav + (int64_t)(int32_t)Rbv + (psr & PSR_ICC_C ? 1 : 0); break;
        case S_OP3_SUB:     Rcv = (int64_t)(int32_t)Rav - (int64_t)(int32_t)Rbv; break;
        case S_OP3_SUBX:    Rcv = (int64_t)(int32_t)Rav - (int64_t)(int32_t)Rbv - (psr & PSR_ICC_C ? 1 : 0); break;

        // Multiplication & Division
        case S_OP3_UMUL:    Rcv =                   Rav *                   Rbv; Y = (uint32_t)(Rcv >> 32); break;
        case S_OP3_SMUL:    Rcv = (int64_t)(int32_t)Rav * (int64_t)(int32_t)Rbv; Y = (uint32_t)(Rcv >> 32); break;

#define CHECKDIV0(X) if ((X) == 0) ThrowIllegalInstructionException(*this, m_input.pc, "Division by zero")
        case S_OP3_UDIV:    CHECKDIV0(Rbv);                   Rcv =          (((uint64_t)Y << 32) | Rav) /                   Rbv; break;
        case S_OP3_SDIV:    CHECKDIV0((int64_t)(int32_t)Rbv); Rcv = (int64_t)(((uint64_t)Y << 32) | Rav) / (int64_t)(int32_t)Rbv; break;
    }

    if (opcode & 0x10)
    {
        // Set icc
        const bool A31 = Rav & 0x80000000;
        const bool B31 = Rbv & 0x80000000;
        const bool C31 = Rcv & 0x80000000;

        // Clear the icc flags
        psr &= ~PSR_ICC;

        switch (opcode & 0xF)
        {
            case S_OP3_ADD: case S_OP3_ADDX:
                // ``Overflow occurs on addition if both operands have the same sign and the sign of the sum is different.''
                if (!(A31 ^ B31) && (A31 ^ C31))            psr |= PSR_ICC_V;
                if ((A31 && B31) || (!C31 && (A31 || B31))) psr |= PSR_ICC_C;
                if (Rcv == 0) psr |= PSR_ICC_Z;
                if (C31)      psr |= PSR_ICC_N;
                break;

            case S_OP3_SUB: case S_OP3_SUBX:
                // ``Overflow occurs on subtraction if the operands have different signs and the sign of the difference differs from the sign of r[rs1].''
                if ((A31 ^ B31) && (A31 ^ C31))              psr |= PSR_ICC_V;
                if ((!A31 && B31) || (C31 && (!A31 || B31))) psr |= PSR_ICC_C;
                if (Rcv == 0) psr |= PSR_ICC_Z;
                if (C31)      psr |= PSR_ICC_N;
                break;

            case S_OP3_AND:  case S_OP3_OR:  case S_OP3_XOR:
            case S_OP3_ANDN: case S_OP3_ORN: case S_OP3_XNOR:
            case S_OP3_UMUL: case S_OP3_SMUL:
                if (Rcv == 0) psr |= PSR_ICC_Z;
                if (C31)      psr |= PSR_ICC_N;
                break;

            case S_OP3_UDIV:
                if (Rcv > 0xFFFFFFFFULL) { psr |= PSR_ICC_V; Rcv = 0xFFFFFFFF; }
                if (Rcv == 0)              psr |= PSR_ICC_Z;
                if (C31)                   psr |= PSR_ICC_N;
                break;

            case S_OP3_SDIV:
                if ((int64_t)Rcv < -0x80000000LL) { psr |= PSR_ICC_V; Rcv = (uint64_t)(int64_t)-0x80000000LL; }
                if ((int64_t)Rcv >  0x7FFFFFFFLL) { psr |= PSR_ICC_V; Rcv = (uint64_t)(int64_t) 0x7FFFFFFFLL; }
                if (Rcv == 0) psr |= PSR_ICC_Z;
                if (C31)      psr |= PSR_ICC_N;
                break;
        }
    }
    return (uint32_t)Rcv;
}

uint32_t Processor::Pipeline::ExecuteStage::ExecOtherInteger(int opcode, uint32_t Rav, uint32_t Rbv, uint32_t& Y, PSR& psr)
{
    switch (opcode)
    {
    case S_OP3_SLL:    return          Rav << (Rbv & 0x1F);
    case S_OP3_SRL:    return          Rav >> (Rbv & 0x1F);
    case S_OP3_SRA:    return (int32_t)Rav >> (Rbv & 0x1F);
    case S_OP3_MULScc:
    {
        // Multiply Step, see Sparc v8 manual, B.17
        // Step 1 was implicitely done in the Read Stage

        // Remember Rav's LSB for step 6
        const uint32_t Rav_LSB = (Rav & 1);

        // Step 2: Shift Rav, put (N xor V) in MSB gap
        const bool N = (psr & PSR_ICC_N) != 0;
        const bool V = (psr & PSR_ICC_V) != 0;
        Rav = (Rav >> 1) | ((N ^ V) ? 0x80000000 : 0);

        // Step 3: Add to multiplier depending on Y's LSB, and
        // Step 5: Update icc according to step 3's addition
        Rbv = ExecBasicInteger(S_OP3_ADDcc, Rbv, (Y & 1) ? Rav : 0, Y, psr);

        // Step 6: Shift Y, put Rav's LSB in MSB gap
        Y = (Y >> 1) | (Rav_LSB << 31);

        // Step 4: Step 3's result is the result
        return Rbv;
    }
    }
    return 0;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecReadASR19(uint8_t func)
{
    switch (func)
    {
        case S_OPT2_LDBP:
            COMMIT {
                // TLS base pointer: base address of TLS
                m_output.Rcv.m_integer = m_parent.GetProcessor().GetTLSAddress(m_input.fid, m_input.tid);
                m_output.Rcv.m_state   = RST_FULL;
            }
            break;

        case S_OPT2_LDFP:
            COMMIT {
                /// TLS frame (stack) pointer: top of TLS
                const MemAddr tls_base = m_parent.GetProcessor().GetTLSAddress(m_input.fid, m_input.tid);
                const MemAddr tls_size = m_parent.GetProcessor().GetTLSSize();
                m_output.Rcv.m_integer = tls_base + tls_size;
                m_output.Rcv.m_state   = RST_FULL;
            }
            break;

        case S_OPT2_CREBAS:
        case S_OPT2_CREBIS:
        {
            assert(m_input.Rav.m_size == sizeof(Integer));
            assert(m_input.Rbv.m_size == sizeof(Integer));
            Integer Rav = m_input.Rav.m_integer.get(m_input.Rav.m_size);
            Integer Rbv = m_input.Rbv.m_integer.get(m_input.Rbv.m_size);
            return ExecBundle(Rbv, (func == S_OPT2_CREBIS), Rav, m_input.Rc.index);
        }

        default:
            ThrowIllegalInstructionException(*this, m_input.pc, "Invalid read from ASR19: func = %d", (int)func);
            break;
    }
    return PIPE_CONTINUE;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecReadASR20(uint8_t func)
{
    assert(m_input.Rav.m_size == sizeof(Integer));
    assert(m_input.Rbv.m_size == sizeof(Integer));
    Integer Rav = m_input.Rav.m_integer.get(m_input.Rav.m_size);
    Integer Rbv = m_input.Rbv.m_integer.get(m_input.Rbv.m_size);

    switch (func)
    {
        case S_OPT1_ALLOCATE:
        case S_OPT1_ALLOCATES:
        case S_OPT1_ALLOCATEX:
        {
            PlaceID place = m_parent.GetProcessor().UnpackPlace(Rav);
            return ExecAllocate(place, m_input.Rc.index, func != S_OPT1_ALLOCATE, func == S_OPT1_ALLOCATEX, Rbv);
        }

        case S_OPT1_CREATE:
        {
            FID     fid  = m_parent.GetProcessor().UnpackFID(Rav);
            MemAddr addr = Rbv;
            return ExecCreate(fid, addr, m_input.Rc.index);
        }

        case S_OPT1_SYNC:
            return ExecSync(m_parent.GetProcessor().UnpackFID(Rbv));

        case S_OPT1_GETTID:
        case S_OPT1_GETFID:
        case S_OPT1_GETPID:
        case S_OPT1_GETCID:
            COMMIT {
                m_output.Rcv.m_state   = RST_FULL;
                switch (m_input.function)
                {
                case S_OPT1_GETFID: m_output.Rcv.m_integer = m_input.fid; break;
                case S_OPT1_GETTID: m_output.Rcv.m_integer = m_input.tid; break;
                case S_OPT1_GETCID: m_output.Rcv.m_integer = m_parent.GetProcessor().GetPID(); break;
                case S_OPT1_GETPID:
                {
                    PlaceID place;
                    place.size = m_input.placeSize;
                    place.pid  = m_parent.GetProcessor().GetPID() & -place.size;
                    place.capability = 0x1337;
                    m_output.Rcv.m_integer = m_parent.GetProcessor().PackPlace(place);
                    break;
                }
                }
            }
            break;

        case S_OPT1_GETS:
            return ReadFamilyRegister(RRT_LAST_SHARED, RT_INTEGER, m_parent.GetProcessor().UnpackFID(Rbv), m_input.asi);

        case S_OPT1_FGETS:
            return ReadFamilyRegister(RRT_LAST_SHARED, RT_FLOAT, m_parent.GetProcessor().UnpackFID(Rbv), m_input.asi);

        case S_OPT1_GETG:
            return ReadFamilyRegister(RRT_GLOBAL, RT_INTEGER, m_parent.GetProcessor().UnpackFID(Rbv), m_input.asi);

        case S_OPT1_FGETG:
            return ReadFamilyRegister(RRT_GLOBAL, RT_FLOAT, m_parent.GetProcessor().UnpackFID(Rbv), m_input.asi);

        default:
            ThrowIllegalInstructionException(*this, m_input.pc, "Invalid read from ASR20: func = %d", (int)func);
            break;
    }
    return PIPE_CONTINUE;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecWriteASR19(uint8_t func)
{
    assert(m_input.Rav.m_size == sizeof(Integer));
    assert(m_input.Rbv.m_size == sizeof(Integer));
    Integer Rav = m_input.Rav.m_integer.get(m_input.Rav.m_size);
    Integer Rbv = m_input.Rbv.m_integer.get(m_input.Rbv.m_size);

    switch (func)
    {
        case S_OPT2_CREBA:
        case S_OPT2_CREBI:
            return ExecBundle(Rav, (m_input.function == S_OPT2_CREBI), Rbv, INVALID_REG_INDEX);

        case S_OPT2_PRINT:
            COMMIT {
                ExecDebug(Rav, Rbv);
                m_output.Rc = INVALID_REG;
            }
            break;

        default:
            ThrowIllegalInstructionException(*this, m_input.pc, "Invalid write to ASR19: func = %d", (int)func);
            break;
    }
    return PIPE_CONTINUE;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecWriteASR20(uint8_t func)
{
    assert(m_input.Rav.m_size == sizeof(Integer));
    assert(m_input.Rbv.m_size == sizeof(Integer));
    Integer Rav = m_input.Rav.m_integer.get(m_input.Rav.m_size);
    Integer Rbv = m_input.Rbv.m_integer.get(m_input.Rbv.m_size);

    switch (func)
    {
    case S_OPT1_SETSTART:
    case S_OPT1_SETLIMIT:
    case S_OPT1_SETSTEP:
    case S_OPT1_SETBLOCK:
        {
            FamilyProperty prop;
            switch (func)
            {
            default:
            case S_OPT1_SETSTART: prop = FAMPROP_START; break;
            case S_OPT1_SETLIMIT: prop = FAMPROP_LIMIT; break;
            case S_OPT1_SETSTEP:  prop = FAMPROP_STEP;  break;
            case S_OPT1_SETBLOCK: prop = FAMPROP_BLOCK; break;
            }
            FID fid = m_parent.GetProcessor().UnpackFID(Rav);
            return SetFamilyProperty(fid, prop, Rbv);
        }

        case S_OPT1_BREAK:
            return ExecBreak();

        case S_OPT1_PUTG:
            return WriteFamilyRegister(RRT_GLOBAL, RT_INTEGER, m_parent.GetProcessor().UnpackFID(Rav), m_input.asi);

        case S_OPT1_PUTS:
            return WriteFamilyRegister(RRT_FIRST_DEPENDENT, RT_INTEGER, m_parent.GetProcessor().UnpackFID(Rav), m_input.asi);

        case S_OPT1_FPUTG:
            return WriteFamilyRegister(RRT_GLOBAL, RT_FLOAT, m_parent.GetProcessor().UnpackFID(Rav), m_input.asi);

        case S_OPT1_FPUTS:
            return WriteFamilyRegister(RRT_FIRST_DEPENDENT, RT_FLOAT, m_parent.GetProcessor().UnpackFID(Rav), m_input.asi);

        case S_OPT1_DETACH:
            return ExecDetach(m_parent.GetProcessor().UnpackFID(Rav));

        default:
            ThrowIllegalInstructionException(*this, m_input.pc, "Invalid write to ASR20: func = %d", (int)func);
            break;
    }
    return PIPE_CONTINUE;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecuteInstruction()
{
    switch (m_input.op1)
    {
    case S_OP1_CALL:
        COMMIT
        {
            m_output.pc = m_input.pc + m_input.displacement * sizeof(Instruction);
            m_output.Rcv.m_integer = m_input.pc;
            m_output.Rcv.m_state   = RST_FULL;
            m_output.Rcv.m_size    = sizeof(Integer);
            DebugFlowWrite("F%u/T%u(%llu) %s call %s",
                           (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                           m_parent.GetProcessor().GetSymbolTable()[m_output.pc].c_str());
        }
        return PIPE_FLUSH;

    case S_OP1_BRANCH:
        switch (m_input.op2)
        {
        case S_OP2_SETHI:
            COMMIT {
                m_output.Rcv.m_integer = m_input.Rbv.m_integer.get(m_input.Rbv.m_size) << 10;
                m_output.Rcv.m_size    = m_input.Rbv.m_size;
                m_output.Rcv.m_state   = RST_FULL;
            }
            break;

        case S_OP2_BRANCH_INT: // Bicc
        case S_OP2_BRANCH_FLT: // FBfcc
        case S_OP2_BRANCH_COP: // CBccc
        {
            const Thread& thread = m_threadTable[m_input.tid];
            bool taken;
            switch (m_input.op2)
            {
                default:
                case S_OP2_BRANCH_COP: taken = false; break; // We don't have a co-processor
                case S_OP2_BRANCH_INT: taken = BranchTakenInt(m_input.function, thread.psr); break;
                case S_OP2_BRANCH_FLT: taken = BranchTakenFlt(m_input.function, thread.psr); break;
            }

            if (taken)
            {
                // Branch was taken; note that we don't annul
                COMMIT {
                    m_output.pc = m_input.pc + m_input.displacement * sizeof(Instruction);
                    DebugFlowWrite("F%u/T%u(%llu) %s branch %s",
                                   (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                                   m_parent.GetProcessor().GetSymbolTable()[m_output.pc].c_str());
                }
                return PIPE_FLUSH;
            }
            break;
        }

        case S_OP2_UNIMPL:
        default:
            ThrowIllegalInstructionException(*this, m_input.pc, "Unimplemented branch instruction");
        }
        break;

    case S_OP1_MEMORY:
    {
        MemAddr address     = (MemAddr)(m_input.Rav.m_integer.get(m_input.Rav.m_size) + (int32_t)m_input.Rbv.m_integer.get(m_input.Rbv.m_size));
        MemSize size        = 0;
        bool    sign_extend = false;
        switch (m_input.op3)
        {
                             case S_OP3_LDSB: size = 1; sign_extend = true; break;
                             case S_OP3_LDSH: size = 2; sign_extend = true; break;
            case S_OP3_STB:  case S_OP3_LDUB: size = 1; break;
            case S_OP3_STH:  case S_OP3_LDUH: size = 2; break;
            case S_OP3_LDF:  case S_OP3_STF:
            case S_OP3_ST:   case S_OP3_LD:   size = 4; break;
            case S_OP3_LDDF: case S_OP3_STDF:
            case S_OP3_STD:  case S_OP3_LDD:  size = 8; break;

            // Load/Store Alternate address space.
            // These are privileged instructions and we don't support them yet.
            case S_OP3_STBA:  case S_OP3_STHA:  case S_OP3_STA:   case S_OP3_STDA:
            case S_OP3_LDSBA: case S_OP3_LDSHA: case S_OP3_LDUBA:
            case S_OP3_LDUHA: case S_OP3_LDA:   case S_OP3_LDDA:
            default:
                ThrowIllegalInstructionException(*this, m_input.pc, "Unimplemented load/store with ASI");
                break;
        }

        if ((address & (size - 1)) != 0)
        {
            ThrowIllegalInstructionException(*this, m_input.pc, "Misaligned address for size %u: %#llx", 
                                             (unsigned)size, (unsigned long long)address);
        }

        COMMIT
        {
            switch (m_input.op3)
            {
                case S_OP3_STB:
                case S_OP3_STH:
                case S_OP3_ST:
                case S_OP3_STD:
                case S_OP3_STF:
                case S_OP3_STDF:
                    m_output.Rcv = m_input.Rsv;
                    m_output.Ra = m_input.Rs; // for debugging memory only
                    break;
            }

            m_output.address     = address;
            m_output.size        = size;
            m_output.sign_extend = sign_extend;
        }
        break;
    }

    case S_OP1_OTHER:
    {
        Thread& thread = m_threadTable[m_input.tid];

        switch (m_input.op3)
        {
        case S_OP3_FPOP1:
        case S_OP3_FPOP2:
        {
            FPUOperation fpuop = FPU_OP_NONE;

            COMMIT {
                m_output.Rcv.m_size = m_input.RcSize;
            }
            switch (m_input.function)
            {
            // Convert Int to FP
            case S_OPF_FITOS:
            case S_OPF_FITOD:
            case S_OPF_FITOQ:
                COMMIT {
                    m_output.Rcv.m_state = RST_FULL;
                    m_output.Rcv.m_float.fromfloat(
                        (double)m_input.Rbv.m_float.toint(m_input.Rbv.m_size),
                        m_output.Rcv.m_size);
                }
                break;

            // Convert FP to Int
            case S_OPF_FSTOI:
            case S_OPF_FDTOI:
            case S_OPF_FQTOI:
                COMMIT {
                    m_output.Rcv.m_state = RST_FULL;
                    m_output.Rcv.m_float.fromint(
                        (uint64_t)m_input.Rbv.m_float.tofloat(m_input.Rbv.m_size),
                        m_output.Rcv.m_size);
                }
                break;

            // Convert FP to FP
            case S_OPF_FSTOD:
            case S_OPF_FSTOQ:
            case S_OPF_FDTOS:
            case S_OPF_FDTOQ:
            case S_OPF_FQTOS:
            case S_OPF_FQTOD:
                COMMIT {
                    m_output.Rcv.m_state = RST_FULL;
                    m_output.Rcv.m_float.fromfloat(
                        m_input.Rbv.m_float.tofloat(m_input.Rbv.m_size),
                        m_output.Rcv.m_size);
                }
                break;

            case S_OPF_FPRINTS:
            case S_OPF_FPRINTD:
            case S_OPF_FPRINTQ:
                COMMIT {
                    ExecDebug(m_input.Rav.m_float.tofloat(m_input.Rav.m_size), (Integer)m_input.Rbv.m_integer.get(m_input.Rbv.m_size));
                    m_output.Rc = INVALID_REG;
                }
                break;

            case S_OPF_FMOV:
                COMMIT{
                    m_output.Rcv.m_float.fromfloat(m_input.Rbv.m_float.tofloat(m_input.Rbv.m_size), m_output.Rcv.m_size);
                    m_output.Rcv.m_state = RST_FULL;
                }
                break;

            case S_OPF_FNEG:
                COMMIT{
                    m_output.Rcv.m_float.fromfloat(-m_input.Rbv.m_float.tofloat(m_input.Rbv.m_size), m_output.Rcv.m_size);
                    m_output.Rcv.m_state = RST_FULL;
                }
                break;

            case S_OPF_FABS:
                COMMIT{
                    m_output.Rcv.m_float.fromfloat(abs(m_input.Rbv.m_float.tofloat(m_input.Rbv.m_size)), m_output.Rcv.m_size);
                    m_output.Rcv.m_state = RST_FULL;
                }
                break;

            // FP Compare
            case S_OPF_FCMPS:  case S_OPF_FCMPD:  case S_OPF_FCMPQ:
            case S_OPF_FCMPES: case S_OPF_FCMPED: case S_OPF_FCMPEQ:
                COMMIT {
                    const double Ra = m_input.Rav.m_float.tofloat(m_input.Rav.m_size);
                    const double Rb = m_input.Rbv.m_float.tofloat(m_input.Rbv.m_size);
                    thread.fsr = (thread.fsr & ~FSR_FCC) |
                        isunordered(Ra, Rb) ? +FSR_FCC_UO :
                        isgreater  (Ra, Rb) ? +FSR_FCC_GT :
                        isless     (Ra, Rb) ? +FSR_FCC_LT : +FSR_FCC_EQ;
                }
                break;

            case S_OPF_FSQRTS: case S_OPF_FSQRTD: case S_OPF_FSQRTQ: fpuop = FPU_OP_SQRT; break;
            case S_OPF_FADDS:  case S_OPF_FADDD:  case S_OPF_FADDQ:  fpuop = FPU_OP_ADD;  break;
            case S_OPF_FSUBS:  case S_OPF_FSUBD:  case S_OPF_FSUBQ:  fpuop = FPU_OP_SUB;  break;
            case S_OPF_FMULS:  case S_OPF_FMULD:  case S_OPF_FMULQ:  fpuop = FPU_OP_MUL;  break;
            case S_OPF_FDIVS:  case S_OPF_FDIVD:  case S_OPF_FDIVQ:  fpuop = FPU_OP_DIV;  break;

            case S_OPF_FSMULD: fpuop = FPU_OP_MUL; break;
            case S_OPF_FDMULQ: fpuop = FPU_OP_MUL; break;
            }

            if (fpuop != FPU_OP_NONE)
            {
                // Dispatch long-latency operation to FPU
                if (!m_fpu.QueueOperation(m_fpuSource, fpuop, m_input.RcSize,
                    m_input.Rav.m_float.tofloat(m_input.Rav.m_size),
                    m_input.Rbv.m_float.tofloat(m_input.Rbv.m_size), m_input.Rc))
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
            break;
        }

        case S_OP3_SLL:
        case S_OP3_SRL:
        case S_OP3_SRA:
        case S_OP3_MULScc:
            COMMIT {
                m_output.Rcv.m_integer = ExecOtherInteger(
                    m_input.op3,
                    (uint32_t)m_input.Rav.m_integer.get(m_input.Rav.m_size),
                    (uint32_t)m_input.Rbv.m_integer.get(m_input.Rbv.m_size),
                    thread.Y, thread.psr);
                m_output.Rcv.m_state   = RST_FULL;
            }
            break;

        case S_OP3_RDASR:
            // The displacement field holds the original Ra specifier
            switch (m_input.displacement)
            {
            case 0:
                // RDY: Read Y Register
                COMMIT {
                    m_output.Rcv.m_integer = thread.Y;
                    m_output.Rcv.m_state   = RST_FULL;
                }
                break;

            case 4:
                // RDTICK: read processor cycle counter
                COMMIT {
                    m_output.Rcv.m_integer = GetCycleNo() & 0xffffffffUL;
                    m_output.Rcv.m_state = RST_FULL;
                }
                break;

            case 5:
                // RDPC: read program counter
                COMMIT {
                    m_output.Rcv.m_integer = m_input.pc;
                    m_output.Rcv.m_state = RST_FULL;
                }
                break;

            case 15:
                // STBAR: Store Barrier
                // Rc has to be %g0 (invalid)
                if (m_input.Rc.valid()) {
                    ThrowIllegalInstructionException(*this, m_input.pc, "Store barrier with valid output register %s", m_input.Rc.str().c_str());
                }

                if (!MemoryWriteBarrier(m_input.tid))
                {
                    // Suspend thread at out PC
                    COMMIT {
                        m_output.pc      = m_input.pc;
                        m_output.suspend = SUSPEND_MEMORY_BARRIER;
                        m_output.swch    = true;
                        m_output.kill    = false;
                        m_output.Rc      = INVALID_REG;
                    }
                }
                return PIPE_FLUSH;

            case 19:
                return ExecReadASR19(m_input.function);

            case 20:
                return ExecReadASR20(m_input.function);

            default:
                ThrowIllegalInstructionException(*this, m_input.pc, "Unsupported read from ASR%d", (int)m_input.displacement);
                break;
            }
            break;

        case S_OP3_WRASR:
            switch (m_input.displacement)
            {
            case 0:
                // WRY: Write Y Register
                COMMIT {
                    thread.Y = (uint32_t)(m_input.Rav.m_integer.get(m_input.Rav.m_size) ^ m_input.Rbv.m_integer.get(m_input.Rbv.m_size));
                }
                break;

            case 20:
                return ExecWriteASR20(m_input.function);

            case 19:
                return ExecWriteASR19(m_input.function);

            default:
                ThrowIllegalInstructionException(*this, m_input.pc, "Unsupported write to ASR%d", (int)m_input.displacement);
                break;
            }
            break;

        case S_OP3_SAVE:
        case S_OP3_RESTORE:
        {
            ThrowIllegalInstructionException(*this, m_input.pc, "SAVE/RESTORE not supported on D-RISC");
            break;
        }

        case S_OP3_JMPL:
        {
            MemAddr target = (MemAddr)(m_input.Rav.m_integer.get(m_input.Rav.m_size) + m_input.Rbv.m_integer.get(m_input.Rbv.m_size));
            if ((target & (sizeof(Instruction) - 1)) != 0)
            {
                ThrowIllegalInstructionException(*this, m_input.pc, "Misaligned jump to %#llx", (unsigned long long)target);
            }

            COMMIT
            {
                // Note that we don't annul
                m_output.pc   = target;
                m_output.Rcv.m_integer = m_input.pc;
                m_output.Rcv.m_state   = RST_FULL;
                DebugFlowWrite("F%u/T%u(%llu) %s branch %s",
                               (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                               m_parent.GetProcessor().GetSymbolTable()[m_output.pc].c_str());
            }
            return PIPE_FLUSH;
        }

        case S_OP3_RDPSR:
        case S_OP3_RDWIM:
        case S_OP3_RDTBR:
        case S_OP3_TADDcc:
        case S_OP3_TSUBcc:
        case S_OP3_TADDccTV:
        case S_OP3_TSUBccTV:
        case S_OP3_WRPSR:
        case S_OP3_WRWIM:
        case S_OP3_WRTBR:
        case S_OP3_RETT:
        case S_OP3_Ticc:
        case S_OP3_FLUSH:
        case S_OP3_CPOP1:
        case S_OP3_CPOP2:
            // We don't support these instructions (yet?)
            ThrowIllegalInstructionException(*this, m_input.pc, "Unsupported opcode: %#x", (unsigned)m_input.op3);
            break;

        default:
            if (m_input.op3 < 0x20)
            {
                COMMIT {
                    m_output.Rcv.m_state   = RST_FULL;
                    m_output.Rcv.m_size    = m_input.Rav.m_size;
                    m_output.Rcv.m_integer = ExecBasicInteger(
                        m_input.op3,
                        (uint32_t)m_input.Rav.m_integer.get(m_input.Rav.m_size),
                        (uint32_t)m_input.Rbv.m_integer.get(m_input.Rbv.m_size),
                        thread.Y, thread.psr);
                }
            }
            else
            {
                // Invalid instruction
                ThrowIllegalInstructionException(*this, m_input.pc, "Unsupported opcode: %#x", (unsigned)m_input.op3);
            }
            break;
        }
        break;
    }
    }
    return PIPE_CONTINUE;
}

}
