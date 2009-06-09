#include "Pipeline.h"
#include "Processor.h"
#include "FPU.h"
#include <cassert>
#include <cmath>
#include <sstream>
#include <iomanip>
using namespace Simulator;
using namespace std;

static const int RA_SHIFT       = 14;
static const int RB_SHIFT       = 0;
static const int RC_SHIFT       = 25;
static const int REG_MASK       = 0x1F;
static const int OP1_SHIFT      = 30;
static const int OP1_MASK       = 0x3;
static const int OP1_DISP_SHIFT = 0;
static const int OP1_DISP_SIZE  = 30;
static const int OP1_DISP_MASK  = (1 << OP1_DISP_SIZE) - 1;
static const int OP2_SHIFT      = 22;
static const int OP2_MASK       = 0x7;
static const int OP2_DISP_SHIFT = 0;
static const int OP2_DISP_SIZE  = 22;
static const int OP2_DISP_MASK  = (1 << OP2_DISP_SIZE) - 1;
static const int IMM_SHIFT      = 0;
static const int IMM_MASK       = 0x3FFFFF;
static const int COND_SHIFT     = 25;
static const int COND_MASK      = 0xF;
static const int OP3_SHIFT      = 19;
static const int OP3_MASK       = 0x3F;
static const int ASI_SHIFT      = 5;
static const int ASI_MASK       = 0xFF;
static const int IMMEDIATE      = (1 << 13);
static const int SIMM_SHIFT     = 0;
static const int SIMM_SIZE      = 13;
static const int SIMM_MASK      = (1 << SIMM_SIZE) - 1;
static const int OPF_SHIFT      = 5;
static const int OPF_MASK       = (1 << 9) - 1;

// Function for getting a register's type and index within that type
unsigned char Simulator::GetRegisterClass(unsigned char addr, const RegsNo& regs, RegClass* rc)
{
    // SPARC has r0 as RAZ, so we flip everything around.
    addr = (unsigned char)(31 - addr);
    
    if (addr < regs.globals)
    {
        *rc = RC_GLOBAL;
        return (unsigned char)(regs.globals - 1 - addr);
    }
    addr -= regs.globals;
    if (addr < regs.shareds)
    {
        *rc = RC_SHARED;
        return (unsigned char)(regs.shareds - 1 - addr);
    }
    addr -= regs.shareds;
    if (addr < regs.locals)
    {
        *rc = RC_LOCAL;
        return (unsigned char)(regs.locals - 1 - addr);
    }
    addr -= regs.locals;
    if (addr < regs.shareds)
    {
        *rc = RC_DEPENDENT;
        return (unsigned char)(regs.shareds - 1 - addr);
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

void Pipeline::DecodeStage::DecodeInstruction(const Instruction& instr)
{
    m_output.op1 = (uint8_t)((instr >> OP1_SHIFT) & OP1_MASK);
    RegIndex Ra  = (instr >> RA_SHIFT) & REG_MASK;
    RegIndex Rb  = (instr >> RB_SHIFT) & REG_MASK;
    RegIndex Rc  = (instr >> RC_SHIFT) & REG_MASK;

    m_output.Ra = INVALID_REG;
    m_output.Rb = INVALID_REG;
    m_output.Rc = INVALID_REG;
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
            m_output.literal = (instr >> IMM_SHIFT) & IMM_MASK;
            m_output.Rc      = MAKE_REGADDR(RT_INTEGER, Rc);
            break;

        case S_OP2_CRED:
            // Create reads and writes Rc
            m_output.Ra = MAKE_REGADDR(RT_INTEGER, Rc);
            m_output.Rc = MAKE_REGADDR(RT_INTEGER, Rc);
            m_output.displacement = SEXT((instr >> OP2_DISP_SHIFT) & OP2_DISP_MASK, OP2_DISP_SIZE);
            break;

        case S_OP2_ALLOCATE:
            // Allocate writes Rc
            m_output.literal = (instr >> IMM_SHIFT) & IMM_MASK;
            m_output.Rc      = MAKE_REGADDR(RT_INTEGER, Rc);
            break;

        case S_OP2_SETREGS:
            // Setregs reads Ra
            m_output.literal = (instr >> IMM_SHIFT) & IMM_MASK;
            m_output.Ra      = MAKE_REGADDR(RT_INTEGER, Rc);
            break;
            
        default:
            // We don't care about the annul bit (not supported; obviously this presents a problem with legacy code later)
            m_output.displacement = SEXT((instr >> OP2_DISP_SHIFT) & OP2_DISP_MASK, OP2_DISP_SIZE);
            m_output.function     = (uint16_t)((instr >> COND_SHIFT) & COND_MASK);
            break;
        }
        break;
        
    case S_OP1_CALL:
        m_output.displacement = SEXT((instr >> OP1_DISP_SHIFT) & OP1_DISP_MASK, OP1_DISP_SIZE);
        m_output.Rc = MAKE_REGADDR(RT_INTEGER, 15);
        break;

    case S_OP1_MEMORY:
        m_output.op3 = (uint8_t)((instr >> OP3_SHIFT) & OP3_MASK);
        m_output.Ra  = MAKE_REGADDR(RT_INTEGER, Ra);
        if (instr & IMMEDIATE) {
            m_output.literal = SEXT((instr >> SIMM_SHIFT) & SIMM_MASK, SIMM_SIZE);
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
        break;
            
    case S_OP1_OTHER:
        m_output.op3 = (uint8_t)((instr >> OP3_SHIFT) & OP3_MASK);
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
            
        case S_OP3_WRSR:
            // This instruction needs the Rc specifier, but we can't put it in the literal,
            // so we abuse the displacement field so the EX stage can use it
            m_output.Ra = MAKE_REGADDR(RT_INTEGER, Ra);
            m_output.Rb = MAKE_REGADDR(RT_INTEGER, Rb);
            m_output.displacement = Rc;
            break;

        case S_OP3_RDSR:
            // This instruction needs the Ra specifier, so we put it in the
            // displacement just like for WRSR.
            m_output.Rc = MAKE_REGADDR(RT_INTEGER, Rc);
            m_output.displacement = Ra;
            break;
            
        default:
            // Integer operation
            m_output.Ra = MAKE_REGADDR(RT_INTEGER, Ra);
            m_output.Rc = MAKE_REGADDR(RT_INTEGER, Rc);
            if (instr & IMMEDIATE) {
                m_output.literal = SEXT((instr >> SIMM_SHIFT) & SIMM_MASK, SIMM_SIZE);
            } else {
                m_output.Rb  = MAKE_REGADDR(RT_INTEGER, Rb);
            }
            break;
        }
        break;
    }
}

static bool BranchTakenInt(int cond, uint32_t psr)
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

static bool BranchTakenFlt(int cond, uint32_t fsr)
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

static uint32_t ExecBasicInteger(int opcode, uint32_t Rav, uint32_t Rbv, uint32_t& Y, PSR& psr)
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
        case S_OP3_ADD:     Rcv = (int64_t)Rav + (int64_t)Rbv; break;
        case S_OP3_ADDX:    Rcv = (int64_t)Rav + (int64_t)Rbv + (psr & PSR_ICC_C ? 1 : 0); break;
        case S_OP3_SUB:     Rcv = (int64_t)Rav - (int64_t)Rbv; break;
        case S_OP3_SUBX:    Rcv = (int64_t)Rav - (int64_t)Rbv - (psr & PSR_ICC_C ? 1 : 0); break;

        // Multiplication & Division
        case S_OP3_UMUL:    Rcv =          Rav *          Rbv; Y = (uint32_t)(Rcv >> 32); break;
        case S_OP3_SMUL:    Rcv = (int64_t)Rav * (int64_t)Rbv; Y = (uint32_t)(Rcv >> 32); break;
        case S_OP3_UDIV:    Rcv =          (((uint64_t)Y << 32) | Rav) /          Rbv; break;
        case S_OP3_SDIV:    Rcv = (int64_t)(((uint64_t)Y << 32) | Rav) / (int64_t)Rbv; break;
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

static uint32_t ExecOtherInteger(int opcode, uint32_t Rav, uint32_t Rbv, uint32_t& Y, PSR& psr)
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

static void ThrowIllegalInstructionException(Object& obj, MemAddr pc)
{
    stringstream error;
    error << "Illegal instruction at " 
          << hex << setw(sizeof(MemAddr) * 2) << setfill('0') << pc;
    throw IllegalInstructionException(obj, error.str());
}

Pipeline::PipeAction Pipeline::ExecuteStage::ExecuteInstruction()
{
    switch (m_input.op1)
    {
    case S_OP1_CALL:
        COMMIT
        {
            m_output.pc   = m_input.pc + m_input.displacement * sizeof(Instruction);
            m_output.Rcv.m_integer = m_input.pc;
            m_output.Rcv.m_state   = RST_FULL;
            m_output.Rcv.m_size    = sizeof(Integer);
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
                }
                return PIPE_FLUSH;
            }
            break;
        }

        case S_OP2_CRED:
        {
            // Direct create
            MemAddr target = m_input.pc + m_input.displacement * sizeof(Instruction);
            LFID fid = LFID((size_t)m_input.Rav.m_integer.get(m_input.Rav.m_size));
            return ExecCreate(fid, target, m_input.Rc);
        }

        case S_OP2_ALLOCATE:
        {
            // Get the base for the shareds and globals in the parent thread
            Allocator::RegisterBases bases[NUM_REG_TYPES];

            uint64_t literal = m_input.Rbv.m_integer.get(m_input.Rbv.m_size);
            for (RegType i = 0; i < NUM_REG_TYPES; i++, literal >>= 10)
            {
                const RegIndex locals = m_input.regs.types[i].thread.base + m_input.regs.types[i].family.count.shareds;
                bases[i].globals = locals + (unsigned char)((literal >> 0) & 0x1F);
                bases[i].shareds = locals + (unsigned char)((literal >> 5) & 0x1F);
            }

            LFID fid;
            Result res = m_allocator.AllocateFamily(m_input.tid, m_input.Rc.index, &fid, bases);
            if (res == FAILED)
            {
                return PIPE_STALL;
            }

            if (res == SUCCESS) {
                COMMIT {
                    // The entry was allocated, store it
                    m_output.Rcv.m_state   = RST_FULL;
                    m_output.Rcv.m_integer = fid;
                }
            } else {
                COMMIT {
                    // The request was buffered and will be written back
                    m_output.Rcv = MAKE_EMPTY_PIPEVALUE(m_output.Rcv.m_size);
                }
            }
            break;
        }

        case S_OP2_SETREGS:
        {
            // Get the base for the shareds and globals in the parent thread
            Allocator::RegisterBases bases[NUM_REG_TYPES];

            uint64_t literal = m_input.Rbv.m_integer.get(m_input.Rbv.m_size);
            for (RegType i = 0; i < NUM_REG_TYPES; i++, literal >>= 10)
            {
                const RegIndex locals = m_input.regs.types[i].thread.base + m_input.regs.types[i].family.count.shareds;
                bases[i].globals = locals + (unsigned char)((literal >> 0) & 0x1F);
                bases[i].shareds = locals + (unsigned char)((literal >> 5) & 0x1F);
            }
            
            LFID fid = (LFID)m_input.Rav.m_integer.get(m_input.Rav.m_size);
            return SetFamilyRegs(fid, bases);
        }
            
        case S_OP2_UNIMPL:
        default:
            ThrowIllegalInstructionException(*this, m_input.pc);
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
                ThrowIllegalInstructionException(*this, m_input.pc);
                break;
        }
        
        if ((address & (size - 1)) != 0)
        {
            // The address is mis-aligned
            ThrowIllegalInstructionException(*this, m_input.pc);
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
                    m_output.Rcv = m_input.storeValue;
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
        else switch (m_input.op3)
        {
        case S_OP3_FPOP1:
        case S_OP3_FPOP2:
        {
            FPUOperation fpuop = FPU_OP_NONE;
            
            COMMIT {
                m_output.Rcv.m_size = m_input.Rcv.m_size;
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
                        isunordered(Ra, Rb) ? FSR_FCC_UO :
                        isgreater  (Ra, Rb) ? FSR_FCC_GT :
                        isless     (Ra, Rb) ? FSR_FCC_LT : FSR_FCC_EQ;
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
                if (!m_fpu.QueueOperation(m_fpuSource, fpuop, m_input.Rcv.m_size,
                    m_input.Rav.m_float.tofloat(m_input.Rav.m_size),
                    m_input.Rbv.m_float.tofloat(m_input.Rbv.m_size), m_input.Rc))
                {
                    return PIPE_STALL;
                }

                COMMIT
                {
                    m_output.Rcv = MAKE_EMPTY_PIPEVALUE(m_output.Rcv.m_size);
                    m_output.Rcv.m_remote = m_input.Rrc;

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
            
        case S_OP3_RDSR:
            // The displacement field holds the original Ra specifier
            if (m_input.displacement == 0) {
                // RDY: Read Y Register
                COMMIT {
                    m_output.Rcv.m_integer = thread.Y;
                    m_output.Rcv.m_state   = RST_FULL;
                }
            } else if (m_input.displacement == 15) {
                // STBAR: Store Barrier
                // Rc has to be %g0 (invalid)
                if (m_input.Rc.valid()) {
                    ThrowIllegalInstructionException(*this, m_input.pc);
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
            } else if (m_input.displacement < 15) {
                // RDASR: Read Ancillary State Register
                // We don't support this yet
                ThrowIllegalInstructionException(*this, m_input.pc);
            } else {
                // Read implementation dependent State Register
                // We don't support this yet
                ThrowIllegalInstructionException(*this, m_input.pc);
            }
            break;

        case S_OP3_WRSR:
        {
            uint32_t value = (uint32_t)(m_input.Rav.m_integer.get(m_input.Rav.m_size) ^ m_input.Rbv.m_integer.get(m_input.Rbv.m_size));
            if (m_input.displacement == 0) {
                // WRY: Write Y Register
                COMMIT {
                    thread.Y = value;
                }
            } else if (m_input.displacement < 16) {
                // WRASR: Write Ancillary State Register
                // We don't support this yet
                ThrowIllegalInstructionException(*this, m_input.pc);
            } else {
                // Write implementation dependent State Register
                // We don't support this yet
                ThrowIllegalInstructionException(*this, m_input.pc);
            }
            break;
        }
        
        case S_OP3_SAVE:
        case S_OP3_RESTORE:
        {
            // TODO
            //const Thread& thread = m_threadTable[m_input.tid];
            ThrowIllegalInstructionException(*this, m_input.pc);
            break;
        }
            
        case S_OP3_JMPL:
        {
            MemAddr target = (MemAddr)(m_input.Rav.m_integer.get(m_input.Rav.m_size) + m_input.Rbv.m_integer.get(m_input.Rbv.m_size));
            if ((target & (sizeof(Instruction) - 1)) != 0)
            {
                // Misaligned jump
                ThrowIllegalInstructionException(*this, m_input.pc);
            }
            
            COMMIT
            {
                // Note that we don't annul
                m_output.pc   = target;
                m_output.Rcv.m_integer = m_input.pc;
                m_output.Rcv.m_state   = RST_FULL;
            }
            return PIPE_FLUSH;
        }
        
        case S_OP3_SETSTART: case S_OP3_SETLIMIT: case S_OP3_SETSTEP:
        case S_OP3_SETBLOCK: case S_OP3_SETPLACE:
        {
            FamilyProperty prop;
            switch (m_input.op3)
            {
            default:
            case S_OP3_SETSTART: prop = FAMPROP_START; break;
            case S_OP3_SETLIMIT: prop = FAMPROP_LIMIT; break;
            case S_OP3_SETSTEP:  prop = FAMPROP_STEP;  break;
            case S_OP3_SETBLOCK: prop = FAMPROP_BLOCK; break;
            case S_OP3_SETPLACE: prop = FAMPROP_PLACE; break;
            }
            return SetFamilyProperty( LFID((size_t)m_input.Rav.m_integer.get(m_input.Rav.m_size)), prop, m_input.Rbv.m_integer.get(m_input.Rbv.m_size));
        }
        
        case S_OP3_PRINT:
            assert(m_input.Rav.m_size == sizeof(Integer));
            assert(m_input.Rbv.m_size == sizeof(Integer));
            COMMIT {
                ExecDebug(
                    (Integer)m_input.Rav.m_integer.get(m_input.Rav.m_size),
                    (Integer)m_input.Rbv.m_integer.get(m_input.Rbv.m_size)
                    );
                m_output.Rc = INVALID_REG;
            }
            break;

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
        //case S_OP3_CPOP1:
        //case S_OP3_CPOP2:
        default:
            // We don't support these instructions (yet?)
            ThrowIllegalInstructionException(*this, m_input.pc);
            break;
        }
        break;
    }
    }
    return PIPE_CONTINUE;
}
