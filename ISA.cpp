#include "Pipeline.h"
#include "ISA.h"
using namespace Simulator;
using namespace std;

bool Pipeline::ExecuteStage::branchTaken(uint8_t opcode, const RegValue& value)
{
    switch (opcode)
    {
		case A_OP_BR  : return true;
		case A_OP_BSR : return true;
		case A_OP_BEQ : return ((int64_t)value.m_integer == 0);
		case A_OP_BGE : return ((int64_t)value.m_integer >= 0);
		case A_OP_BGT : return ((int64_t)value.m_integer >  0);
		case A_OP_BLE : return ((int64_t)value.m_integer <= 0);
		case A_OP_BLT : return ((int64_t)value.m_integer <  0);
		case A_OP_BNE : return ((int64_t)value.m_integer != 0);
		case A_OP_BLBC: return ((value.m_integer & 0x1) == 0);
		case A_OP_BLBS: return ((value.m_integer & 0x1) == 1);
		
		// As described in Alpha Handbook, section 4.9, FP branches are tested bitwise
		case A_OP_FBEQ: return (value.m_float.fraction == 0 && value.m_float.exponent == 0);
		case A_OP_FBGE: return ((value.m_float.fraction == 0 && value.m_float.exponent == 0) || value.m_float.sign == 0);
		case A_OP_FBLE: return ((value.m_float.fraction == 0 && value.m_float.exponent == 0) || value.m_float.sign == 1);
		case A_OP_FBGT: return ((value.m_float.fraction != 0 || value.m_float.exponent != 0) && value.m_float.sign == 0);
		case A_OP_FBLT: return ((value.m_float.fraction != 0 || value.m_float.exponent != 0) && value.m_float.sign == 1);
		case A_OP_FBNE: return (value.m_float.fraction != 0 || value.m_float.exponent != 0);

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

//
// mul128b()
//

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

    *resultL = z;
    *resultH = op1H * op2H + ((op1L * op2H) >> 32) + ((op1H * op2L) >> 32) + carry;
}

//
// execINTA()
//

bool Pipeline::ExecuteStage::execINTA(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func)
{
    Rcv.m_state  = RST_FULL;
    uint64_t  Ra = Rav.m_integer;
    uint64_t  Rb = Rbv.m_integer;
    uint64_t& Rc = Rcv.m_integer;

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
            Rc = 0;
            for (int i = 0; i < 8; i++) {
                uint8_t a = (uint8_t)((Ra >> (i * 8)) & 0xFF);
                uint8_t b = (uint8_t)((Rb >> (i * 8)) & 0xFF);
                if (a >= b) Rc |= (uint64_t)(1 << i);
            }
            break;
    }
	return true;
}

//
// execINTL()
//
bool Pipeline::ExecuteStage::execINTL(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func)
{
    Rcv.m_state  = RST_FULL;
    uint64_t  Ra = Rav.m_integer;
    uint64_t  Rb = Rbv.m_integer;
    uint64_t& Rc = Rcv.m_integer;

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

//
// execINTS()
//

bool Pipeline::ExecuteStage::execINTS(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func)
{
    Rcv.m_state  = RST_FULL;
    uint64_t  Ra = Rav.m_integer;
    uint64_t  Rb = Rbv.m_integer;
    uint64_t& Rc = Rcv.m_integer;

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

//
// execINTM()
//

bool Pipeline::ExecuteStage::execINTM(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func)
{
    Rcv.m_state  = RST_FULL;
    uint64_t  Ra = Rav.m_integer;
    uint64_t  Rb = Rbv.m_integer;
	uint64_t& Rc = Rcv.m_integer;

    switch(func)
    {
        case A_INTMFUNC_MULL_V:
        case A_INTMFUNC_MULL: Rc = (int64_t)(int32_t)((int32_t)Ra * (int32_t)Rb); break;
        case A_INTMFUNC_MULQ_V:
        case A_INTMFUNC_MULQ: uint64_t high; mul128b(Ra, Rb, &high, &Rc); break;
        case A_INTMFUNC_UMULH: uint64_t low; mul128b(Ra, Rb, &Rc, &low); break;
    }
	return true;
}

//
// execFLTV()
//

bool Pipeline::ExecuteStage::execFLTV(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func)
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

//
// execFLTI()
//

bool Pipeline::ExecuteStage::execFLTI(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func)
{
    Rcv.m_state = RST_FULL;
	const Float& Ra = Rav.m_float;
	const Float& Rb = Rbv.m_float;
	Float&       Rc = Rcv.m_float;

    switch(func) {
		default:
			// Add, Sub, Mul and Div are done in the FPU
			return false;

		// IEEE Floating Compare
		case A_FLTIFUNC_CMPTUN:
		case A_FLTIFUNC_CMPTUN_SU: Rc.fromdouble( (Ra.todouble() != Ra.todouble() || Rb.todouble() != Rb.todouble()) ? 2.0 : 0.0); break;
		case A_FLTIFUNC_CMPTEQ:
		case A_FLTIFUNC_CMPTEQ_SU: Rc.fromdouble( (Ra.todouble() == Rb.todouble()) ? 2.0 : 0.0); break;
		case A_FLTIFUNC_CMPTLT:
		case A_FLTIFUNC_CMPTLT_SU: Rc.fromdouble( (Ra.todouble() <  Rb.todouble()) ? 2.0 : 0.0); break;
		case A_FLTIFUNC_CMPTLE:
		case A_FLTIFUNC_CMPTLE_SU: Rc.fromdouble( (Ra.todouble() <= Rb.todouble()) ? 2.0 : 0.0); break;

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
			Rc.integer = (int64_t)Rb.todouble();
			break;

		// Convert Integer to IEEE Floating (S_floating)
		case A_FLTIFUNC_CVTQS_C:
		case A_FLTIFUNC_CVTQS:
		case A_FLTIFUNC_CVTQS_M:
		case A_FLTIFUNC_CVTQS_D:
		case A_FLTIFUNC_CVTQS_SUIC:
		case A_FLTIFUNC_CVTQS_SUIM:
		case A_FLTIFUNC_CVTQS_SUI:
		case A_FLTIFUNC_CVTQS_SUID:
			Rc.fromfloat( (float)Rb.integer );
			break;

		// Convert Integer to IEEE Floating (T_floating)
		case A_FLTIFUNC_CVTQT_C:
		case A_FLTIFUNC_CVTQT_M:
		case A_FLTIFUNC_CVTQT:
		case A_FLTIFUNC_CVTQT_D:
		case A_FLTIFUNC_CVTQT_SUIC:
		case A_FLTIFUNC_CVTQT_SUIM:
		case A_FLTIFUNC_CVTQT_SUI:
		case A_FLTIFUNC_CVTQT_SUID:
			Rc.fromdouble( (double)Rb.integer );
			break;

		// Convert IEEE S_Floating to IEEE T_Floating
		case A_FLTIFUNC_CVTST:
		case A_FLTIFUNC_CVTST_S:
			Rc.fromdouble( Rb.tofloat() );
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
			Rc.fromfloat( (float)Rb.todouble() );
			break;
	}
	return true;
}

//
// execFLTL()
//

bool Pipeline::ExecuteStage::execFLTL(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func)
{
    Rcv.m_state = RST_FULL;
	const Float& Ra = Rav.m_float;
	const Float& Rb = Rbv.m_float;
	Float&       Rc = Rcv.m_float;

    switch(func)
    {
        // Convert Integer to Integer
        case A_FLTIFUNC_CVTQL_V:
        case A_FLTIFUNC_CVTQL_SV:
        case A_FLTIFUNC_CVTQL: Rc.integer = ((Rb.integer & 0xC0000000) << 32) | ((Rb.integer & 0x3FFFFFFF) << 29); break;
        case A_FLTIFUNC_CVTLQ: Rc.integer = (((int64_t)Rb.integer >> 32) & 0xC0000000) | ((Rb.integer >> 29) & 0x3FFFFFFF); break;

        // Copy sign
		case A_FLTIFUNC_CPYS:  Rc.sign =  Ra.sign; Rc.exponent = Rb.exponent; Rc.fraction = Rb.fraction; break;
        case A_FLTIFUNC_CPYSN: Rc.sign = ~Ra.sign; Rc.exponent = Rb.exponent; Rc.fraction = Rb.fraction; break;
		case A_FLTIFUNC_CPYSE: Rc.sign =  Ra.sign; Rc.exponent = Ra.exponent; Rc.fraction = Rb.fraction; break;

		// Move from/to Floating-Point Control Register
        case A_FLTIFUNC_MT_FPCR:
        case A_FLTIFUNC_MF_FPCR:
			break;

		// Floating-Point Conditional Move
        case A_FLTIFUNC_FCMOVEQ: if (branchTaken(A_OP_FBEQ, Rav)) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_FLTIFUNC_FCMOVNE: if (branchTaken(A_OP_FBNE, Rav)) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
		case A_FLTIFUNC_FCMOVLT: if (branchTaken(A_OP_FBLT, Rav)) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_FLTIFUNC_FCMOVGE: if (branchTaken(A_OP_FBGE, Rav)) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_FLTIFUNC_FCMOVLE: if (branchTaken(A_OP_FBLE, Rav)) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
        case A_FLTIFUNC_FCMOVGT: if (branchTaken(A_OP_FBGT, Rav)) Rc = Rb; else Rcv.m_state = RST_INVALID; break;
			break;
    }
	return true;
}

bool Pipeline::ExecuteStage::execITFP(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func)
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
				char data[8];
				SerializeRegister(RT_INTEGER, Rav, data, size);
				Rcv = UnserializeRegister(RT_FLOAT, data, size);
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
    const T s = (sizeof(T) * 8) - size;
    return (val << (s - offset)) >> s;
}

static uint64_t MASK1(int offset, int size)
{
    return ((1ULL << size) - 1) << offset;
}

bool Pipeline::ExecuteStage::execFPTI(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func)
{
	Rcv.m_state   = RST_FULL;
    Rcv.m_integer = 0;
	switch (func)
	{
		// Count Leading Zero
		case A_FPTIFUNC_CTLZ:
			for (int i = 63; i > 0; i--) {
				if ((Rbv.m_integer >> i) & 1) {
					break;
				}
				Rcv.m_integer++;
			}
			break;

		// Count Population
		case A_FPTIFUNC_CTPOP:
			for (int i = 0; i < 63; i++) {
				if ((Rbv.m_integer >> i) & 1) {
					Rcv.m_integer++;
				}
			}
			break;

		// Count Trailing Zero
		case A_FPTIFUNC_CTTZ:
			for (int i = 0; i < 63; i++) {
				if ((Rbv.m_integer >> i) & 1) {
				    break;
    		    }
    			Rcv.m_integer++;
			}
			break;

		// Sign Extend
		case A_FPTIFUNC_SEXTB: Rcv.m_integer = (int64_t)( int8_t)Rbv.m_integer; break;
		case A_FPTIFUNC_SEXTW: Rcv.m_integer = (int64_t)(int16_t)Rbv.m_integer; break;

		// Floating-Point Register to Integer Register Move
		case A_FPTIFUNC_FTOIS:
		case A_FPTIFUNC_FTOIT:
			{
				int size = (func == A_FPTIFUNC_FTOIS) ? 4 : 8;
				char data[8];
				SerializeRegister(RT_FLOAT, Rav, data, size);
				Rcv = UnserializeRegister(RT_INTEGER, data, size);
				break;
			}
			
		// Pixel error
	    case A_FPTIFUNC_PERR:
	        for (int i = 0; i < 64; i += 8) {
	            uint8_t a = (uint8_t)BITS(Rav.m_integer, i, 8);
	            uint8_t b = (uint8_t)BITS(Rbv.m_integer, i, 8);
	            Rcv.m_integer += (a >= b ? a - b : b - a);
	        }
	        break;
	   
        // Pack Bytes
	    case A_FPTIFUNC_PKLB: Rcv.m_integer = (BITS(Rbv.m_integer,  0, 8) <<  0) | (BITS(Rbv.m_integer, 32, 8) <<  8); break;
	    case A_FPTIFUNC_PKWB: Rcv.m_integer = (BITS(Rbv.m_integer,  0, 8) <<  0) | (BITS(Rbv.m_integer, 16, 8) <<  8) |
	                                          (BITS(Rbv.m_integer, 32, 8) << 16) | (BITS(Rbv.m_integer, 48, 8) << 24); break;

        // Unpack Bytes
	    case A_FPTIFUNC_UNPKBL: Rcv.m_integer = (BITS(Rbv.m_integer,  0, 8) <<  0) | (BITS(Rbv.m_integer,  8, 8) << 32); break;
	    case A_FPTIFUNC_UNPKBW: Rcv.m_integer = (BITS(Rbv.m_integer,  0, 8) <<  0) | (BITS(Rbv.m_integer,  8, 8) << 16) |
	                                            (BITS(Rbv.m_integer, 16, 8) << 32) | (BITS(Rbv.m_integer, 24, 8) << 48); break;

	    case A_FPTIFUNC_MINSB8:
	    case A_FPTIFUNC_MINSW4:
	    case A_FPTIFUNC_MAXSB8:
	    case A_FPTIFUNC_MAXSW4:
	    {
	        int step = (func == A_FPTIFUNC_MINSB8 || func == A_FPTIFUNC_MAXSB8 ? 8 : 16);
	        const int64_t& (*cmp)(const int64_t&,const int64_t&) = std::max<int64_t>;
	        if (func == A_FPTIFUNC_MINSB8 || func == A_FPTIFUNC_MINSW4) cmp = std::min<int64_t>;
	        for (int i = 0; i < 64; i += step) {
	            Rcv.m_integer |= (cmp(BITS<int64_t>(Rav.m_integer, i, step), BITS<int64_t>(Rbv.m_integer, i, step)) & MASK1(0,step)) << i;
	        }
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
	        for (int i = 0; i < 64; i += step) {
	            Rcv.m_integer |= (cmp(BITS<uint64_t>(Rav.m_integer, i, step), BITS<uint64_t>(Rbv.m_integer, i, step)) & MASK1(0,step)) << i;
	        }
    	    break;
    	}
	}
	return true;
}

