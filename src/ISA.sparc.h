#ifndef ISA_SPARC_H
#define ISA_SPARC_H

namespace Simulator
{

// op1
#define S_OP1_BRANCH 0
#define S_OP1_CALL   1
#define S_OP1_OTHER  2
#define S_OP1_MEMORY 3

// op2 (op1 is S_OP1_BRANCH)
#define S_OP2_UNIMPL     0
#define S_OP2_CRED       1      // uT
#define S_OP2_BRANCH_INT 2
#define S_OP2_ALLOCATE   3      // uT
#define S_OP2_SETHI      4
#define S_OP2_BRANCH_FLT 6
#define S_OP2_BRANCH_COP 7

// op3 (op1 is S_OP1_OTHER)
#define S_OP3_ADD       0x00
#define S_OP3_AND       0x01
#define S_OP3_OR        0x02
#define S_OP3_XOR       0x03
#define S_OP3_SUB       0x04
#define S_OP3_ANDN      0x05
#define S_OP3_ORN       0x06
#define S_OP3_XNOR      0x07
#define S_OP3_ADDX      0x08
#define S_OP3_UMUL      0x0A
#define S_OP3_SMUL      0x0B
#define S_OP3_SUBX      0x0C
#define S_OP3_UDIV      0x0E
#define S_OP3_SDIV      0x0F
#define S_OP3_ADDcc     0x10
#define S_OP3_ANDcc     0x11
#define S_OP3_ORcc      0x12
#define S_OP3_XORcc     0x13
#define S_OP3_SUBcc     0x14
#define S_OP3_ANDNcc    0x15
#define S_OP3_ORNcc     0x16
#define S_OP3_XNORcc    0x17
#define S_OP3_ADDXcc    0x18
#define S_OP3_UMULcc    0x1A
#define S_OP3_SMULcc    0x1B
#define S_OP3_SUBXcc    0x1C
#define S_OP3_UDIVcc    0x1E
#define S_OP3_SDIVcc    0x1F
#define S_OP3_TADDcc    0x20
#define S_OP3_TSUBcc    0x21
#define S_OP3_TADDccTV  0x22
#define S_OP3_TSUBccTV  0x23
#define S_OP3_MULScc    0x24
#define S_OP3_SLL       0x25
#define S_OP3_SRL       0x26
#define S_OP3_SRA       0x27
#define S_OP3_RDSR      0x28
#define S_OP3_RDPSR     0x29
#define S_OP3_RDWIM     0x2A
#define S_OP3_RDTBR     0x2B
#define S_OP3_SETSTART  0x2C    // uT
#define S_OP3_SETLIMIT  0x2D    // uT
#define S_OP3_SETSTEP   0x2E    // uT
#define S_OP3_SETBLOCK  0x2F    // uT
#define S_OP3_WRSR      0x30
#define S_OP3_WRPSR     0x31
#define S_OP3_WRWIM     0x32
#define S_OP3_WRTBR     0x33
#define S_OP3_FPOP1     0x34
#define S_OP3_FPOP2     0x35
//#define S_OP3_CPOP1     0x36
//#define S_OP3_CPOP2     0x37
#define S_OP3_PRINT     0x37    // uT
#define S_OP3_JMPL      0x38
#define S_OP3_RETT      0x39
#define S_OP3_Ticc      0x3A
#define S_OP3_FLUSH     0x3B
#define S_OP3_SAVE      0x3C
#define S_OP3_RESTORE   0x3D
#define S_OP3_LDFP      0x3F    // uT

// op3 (op1 is S_OP1_MEMORY)
#define S_OP3_LD      0x00
#define S_OP3_LDUB    0x01
#define S_OP3_LDUH    0x02
#define S_OP3_LDD     0x03
#define S_OP3_ST      0x04
#define S_OP3_STB     0x05
#define S_OP3_STH     0x06
#define S_OP3_STD     0x07
#define S_OP3_LDSB    0x09
#define S_OP3_LDSH    0x0A
#define S_OP3_LDSTUB  0x0D
#define S_OP3_SWAP    0x0F
#define S_OP3_LDA     0x10
#define S_OP3_LDUBA   0x11
#define S_OP3_LDUHA   0x12
#define S_OP3_LDDA    0x13
#define S_OP3_STA     0x14
#define S_OP3_STBA    0x15
#define S_OP3_STHA    0x16
#define S_OP3_STDA    0x17
#define S_OP3_LDSBA   0x19
#define S_OP3_LDSHA   0x1A
#define S_OP3_LDSTUBA 0x1D
#define S_OP3_SWAPA   0x1F
#define S_OP3_LDF     0x20
#define S_OP3_LDFSR   0x21
#define S_OP3_LDDF    0x23
#define S_OP3_STF     0x24
#define S_OP3_STFSR   0x25
#define S_OP3_STDFQ   0x26
#define S_OP3_STDF    0x27
#define S_OP3_LDC     0x30
#define S_OP3_LDCSR   0x31
#define S_OP3_LDDC    0x33
#define S_OP3_STC     0x34
#define S_OP3_STCSR   0x35
#define S_OP3_STDCQ   0x36
#define S_OP3_STDC    0x37

// opf (op1 is S_OP1_OTHER, op3 is S_OP3_FPOP1/2)
#define S_OPF_FMOV    0x01
#define S_OPF_FPRINTS 0x02
#define S_OPF_FPRINTD 0x03
#define S_OPF_FPRINTQ 0x04
#define S_OPF_FNEG    0x05
#define S_OPF_FABS    0x09
#define S_OPF_FSQRTS  0x29
#define S_OPF_FSQRTD  0x2a
#define S_OPF_FSQRTQ  0x2b
#define S_OPF_FADDS   0x41
#define S_OPF_FADDD   0x42
#define S_OPF_FADDQ   0x43
#define S_OPF_FSUBS   0x45
#define S_OPF_FSUBD   0x46
#define S_OPF_FSUBQ   0x47
#define S_OPF_FMULS   0x49
#define S_OPF_FMULD   0x4a
#define S_OPF_FMULQ   0x4b
#define S_OPF_FDIVS   0x4d
#define S_OPF_FDIVD   0x4e
#define S_OPF_FDIVQ   0x4f
#define S_OPF_FCMPS   0x51
#define S_OPF_FCMPD   0x52
#define S_OPF_FCMPQ   0x53
#define S_OPF_FCMPES  0x55
#define S_OPF_FCMPED  0x56
#define S_OPF_FCMPEQ  0x57
#define S_OPF_FSMULD  0x69
#define S_OPF_FDMULQ  0x6e
#define S_OPF_FITOS   0xc4
#define S_OPF_FDTOS   0xc6
#define S_OPF_FQTOS   0xc7
#define S_OPF_FITOD   0xc8
#define S_OPF_FSTOD   0xc9
#define S_OPF_FQTOD   0xcb
#define S_OPF_FITOQ   0xcc
#define S_OPF_FSTOQ   0xcd
#define S_OPF_FDTOQ   0xce
#define S_OPF_FSTOI   0xd1
#define S_OPF_FDTOI   0xd2
#define S_OPF_FQTOI   0xd3

// Processor State Register flags
static const PSR PSR_CWP   = 0x0000001FUL; // Current Window Pointer
static const PSR PSR_ET    = 0x00000020UL; // Enable Traps
static const PSR PSR_PS    = 0x00000040UL; // Previous Supervisor
static const PSR PSR_S     = 0x00000080UL; // Supervisor
static const PSR PSR_PIL   = 0x00000F00UL; // Processor Interrupt Level
static const PSR PSR_EF    = 0x00001000UL; // Enable Floating Point
static const PSR PSR_EC    = 0x00002000UL; // Enable Coprocessor
static const PSR PSR_ICC   = 0x00F00000UL; // Integer Condition Codes
static const PSR PSR_ICC_C = 0x00100000UL; // ICC: Carry
static const PSR PSR_ICC_V = 0x00200000UL; // ICC: Overflow
static const PSR PSR_ICC_Z = 0x00400000UL; // ICC: Zero
static const PSR PSR_ICC_N = 0x00800000UL; // ICC: Negative
static const PSR PSR_VER   = 0x0F000000UL; // Version
static const PSR PSR_IMPL  = 0xF0000000UL; // Implementation

// Floating-Point State Register flags
static const FSR FSR_CEXC    = 0x0000001FUL; // Current Exception
static const FSR FSR_AEXC    = 0x000003E0UL; // Accrued Exception
static const FSR FSR_FCC     = 0x00000C00UL; // FPU condition codes
static const FSR FSR_FCC_EQ  = 0x00000000UL; // FCC: Equal
static const FSR FSR_FCC_LT  = 0x00000400UL; // FCC: Less-than
static const FSR FSR_FCC_GT  = 0x00000800UL; // FCC: Greater-than
static const FSR FSR_FCC_UO  = 0x00000C00UL; // FCC: Unordered
static const FSR FSR_QNE     = 0x00002000UL; // FQ Not Empty
static const FSR FSR_FTT     = 0x0001C000UL; // Floating-Point Trap Type
static const FSR FSR_VER     = 0x000E0000UL; // Version
static const FSR FSR_NS      = 0x00400000UL; // Nonstandard FP
static const FSR FSR_TEM     = 0x0F800000UL; // Trap Enable Mask
static const FSR FSR_RD      = 0xC0000000UL; // Rounding Direction
static const FSR FSR_RD_NEAR = 0x00000000UL; // RD: Nearest
static const FSR FSR_RD_ZERO = 0x40000000UL; // RD: Zero
static const FSR FSR_RD_PINF = 0x80000000UL; // RD: +infinity
static const FSR FSR_RD_NINF = 0xC0000000UL; // RD: -infinity


}

#endif 
