#ifndef ISA_ALPHA_H
#define ISA_ALPHA_H

#ifndef PIPELINE_H
#error This file should be included in Pipeline.h
#endif

enum InstrFormat
{
    IFORMAT_MEM_LOAD,
    IFORMAT_MEM_STORE,
    IFORMAT_JUMP,
    IFORMAT_OP,
    IFORMAT_FPOP,
    IFORMAT_BRA,
    IFORMAT_PAL,
    IFORMAT_MISC,
    IFORMAT_INVALID
};

enum {
// PAL Instructions
    A_OP_CALL_PAL = 0x00,

// uThread Instructions
    A_OP_UTHREAD    = 0x01,    // Generic instructions (Operate format)
    A_OP_CREATE_I   = 0x03,    // Create Indirect (Memory format)
    A_OP_CREATE_D   = 0x04,    // Create Direct (Branch format)
    A_OP_UTHREADF   = 0x05,    // FP uThread instructions (FP Operate format)

// Branch Instructions
     A_OP_BR    = 0x30,
     A_OP_FBEQ  = 0x31,
     A_OP_FBLT  = 0x32,
     A_OP_FBLE  = 0x33,
     A_OP_BSR   = 0x34,
     A_OP_FBNE  = 0x35,
     A_OP_FBGE  = 0x36,
     A_OP_FBGT  = 0x37,
     A_OP_BLBC  = 0x38,
     A_OP_BEQ   = 0x39,
     A_OP_BLT   = 0x3A,
     A_OP_BLE   = 0x3B,
     A_OP_BLBS  = 0x3C,
     A_OP_BNE   = 0x3D,
     A_OP_BGE   = 0x3E,
     A_OP_BGT   = 0x3F,

// Memory Instructions
    A_OP_LDA    = 0x08,
    A_OP_LDAH   = 0x09,
    A_OP_LDBU   = 0x0A,
    A_OP_LDQ_U  = 0x0B,
    A_OP_LDWU   = 0x0C,
    A_OP_STW    = 0x0D,
    A_OP_STB    = 0x0E,
    A_OP_STQ_U  = 0x0F,
    A_OP_MISC   = 0x18,
    A_OP_JUMP   = 0x1A,
    A_OP_LDF    = 0x20,
    A_OP_LDG    = 0x21,
    A_OP_LDS    = 0x22,
    A_OP_LDT    = 0x23,
    A_OP_STF    = 0x24,
    A_OP_STG    = 0x25,
    A_OP_STS    = 0x26,
    A_OP_STT    = 0x27,
    A_OP_LDL    = 0x28,
    A_OP_LDQ    = 0x29,
    A_OP_LDL_L  = 0x2A,
    A_OP_LDQ_L  = 0x2B,
    A_OP_STL    = 0x2C,
    A_OP_STQ    = 0x2D,
    A_OP_STL_C  = 0x2E,
    A_OP_STQ_C  = 0x2F,

// Operate Instructions
    A_OP_INTA   = 0x10,
    A_OP_INTL   = 0x11,
    A_OP_INTS   = 0x12,
    A_OP_INTM   = 0x13,
    A_OP_ITFP   = 0x14,
    A_OP_FLTV   = 0x15,
    A_OP_FLTI   = 0x16,
    A_OP_FLTL   = 0x17,
    A_OP_FPTI   = 0x1C,
};

/*----------------------------------------------------------------------------*/
/*--------------------- Instructions' Function Codes -------------------------*/
/*----------------------------------------------------------------------------*/

/*------------------ Memory Instructions Function Codes ----------------------*/

// Jump Instructions
enum {
    A_JUMPFUNC_JMP     = 0x00,
    A_JUMPFUNC_JSR     = 0x01,
    A_JUMPFUNC_RET     = 0x02,
    A_JUMPFUNC_JSR_CO  = 0x03,
};

// Miscellaneous Instructions
enum {
    A_MISCFUNC_TRAPB    = 0x0000,
    A_MISCFUNC_EXCB     = 0x0400,
    A_MISCFUNC_MB       = 0x4000,
    A_MISCFUNC_WMB      = 0x4400,
    A_MISCFUNC_FETCH    = 0x8000,
    A_MISCFUNC_FETCH_M  = 0xA000,
    A_MISCFUNC_RPCC     = 0xC000,
    A_MISCFUNC_RC       = 0xE000,
    A_MISCFUNC_RS       = 0xF000,
};

/*------------------ Operate Instructions Function Codes----------------------*/

// UTHREAD Instructions
enum {

    /* local operations */
    A_UTHREAD_DC_MASK    = 0x78,
    A_UTHREAD_DC_VALUE   = 0x00,

    A_UTHREAD_LDBP       = 0x00,
    A_UTHREAD_LDFP       = 0x01,
    A_UTHREAD_GETTID     = 0x02,
    A_UTHREAD_GETFID     = 0x03,
    A_UTHREAD_GETPID     = 0x04,
    A_UTHREAD_GETCID     = 0x05,
    A_UTHREAD_GETASR     = 0x06,
    A_UTHREAD_GETAPR     = 0x07,

    A_UTHREAD_DZ_MASK    = 0x78,
    A_UTHREAD_DZ_VALUE   = 0x08,

    A_UTHREAD_BREAK      = 0x08,
    A_UTHREAD_PRINT      = 0x0F,
    A_UTHREADF_PRINT     = 0x00F,

    /* remote, asynchronous */
    A_UTHREAD_REMOTE_MASK  = 0x60,
    A_UTHREAD_REMOTE_VALUE = 0x20,

    A_UTHREAD_SETSTART   = 0x20,
    A_UTHREAD_SETLIMIT   = 0x21,
    A_UTHREAD_SETSTEP	 = 0x22,
    A_UTHREAD_SETBLOCK   = 0x23,
    A_UTHREAD_PUTG       = 0x24,
    A_UTHREADF_PUTG      = 0x024,
    A_UTHREAD_PUTS       = 0x25,
    A_UTHREADF_PUTS      = 0x025,
    A_UTHREAD_DETACH     = 0x28,

    /* remote, synchronous */
    A_UTHREAD_SYNC       = 0x30,
    A_UTHREAD_GETG       = 0x32,
    A_UTHREADF_GETG      = 0x032,
    A_UTHREAD_GETS       = 0x33,
    A_UTHREADF_GETS      = 0x033,

    /* remote, res mgt. */
    A_UTHREAD_ALLOC_MASK  = 0x60,
    A_UTHREAD_ALLOC_VALUE = 0x40,
    A_UTHREAD_ALLOC_S_MASK = 0x01,
    A_UTHREAD_ALLOC_X_MASK = 0x02,

    A_UTHREAD_ALLOCATE   = 0x40,
    A_UTHREAD_ALLOCATE_S = 0x41,   // Suspend
    A_UTHREAD_ALLOCATE_E = 0x43,   // Exclusive

    A_UTHREAD_CREB_MASK  = 0x60,
    A_UTHREAD_CREB_VALUE = 0x60,
    A_CREATE_B_A         = 0x61,
    A_CREATE_B_I         = 0x71,
};


// INTA Instructions.
enum {
    A_INTAFUNC_ADDL     = 0x00,
    A_INTAFUNC_S4ADDL   = 0x02,
    A_INTAFUNC_SUBL     = 0x09,
    A_INTAFUNC_S4SUBL   = 0x0B,
    A_INTAFUNC_CMPBGE   = 0x0F,
    A_INTAFUNC_S8ADDL   = 0x12,
    A_INTAFUNC_S8SUBL   = 0x1B,
    A_INTAFUNC_CMPULT   = 0x1D,
    A_INTAFUNC_ADDQ     = 0x20,
    A_INTAFUNC_S4ADDQ   = 0x22,
    A_INTAFUNC_SUBQ     = 0x29,
    A_INTAFUNC_S4SUBQ   = 0x2B,
    A_INTAFUNC_CMPEQ    = 0x2D,
    A_INTAFUNC_S8ADDQ   = 0x32,
    A_INTAFUNC_S8SUBQ   = 0x3B,
    A_INTAFUNC_CMPULE   = 0x3D,
    A_INTAFUNC_ADDL_V   = 0x40,
    A_INTAFUNC_SUBL_V   = 0x49,
    A_INTAFUNC_CMPLT    = 0x4D,
    A_INTAFUNC_ADDQ_V   = 0x60,
    A_INTAFUNC_SUBQ_V   = 0x69,
    A_INTAFUNC_CMPLE    = 0x6D,
};

// INTL Instructions.
enum {
    A_INTLFUNC_AND      = 0x00,
    A_INTLFUNC_BIC      = 0x08,
    A_INTLFUNC_CMOVLBS  = 0x14,
    A_INTLFUNC_CMOVLBC  = 0x16,
    A_INTLFUNC_BIS      = 0x20,
    A_INTLFUNC_CMOVEQ   = 0x24,
    A_INTLFUNC_CMOVNE   = 0x26,
    A_INTLFUNC_ORNOT    = 0x28,
    A_INTLFUNC_XOR      = 0x40,
    A_INTLFUNC_CMOVLT   = 0x44,
    A_INTLFUNC_CMOVGE   = 0x46,
    A_INTLFUNC_EQV      = 0x48,
    A_INTLFUNC_AMASK    = 0x61,
    A_INTLFUNC_CMOVLE   = 0x64,
    A_INTLFUNC_CMOVGT   = 0x66,
    A_INTLFUNC_IMPLVER  = 0x6C,
};

// INTS Instructions.
enum {
    A_INTSFUNC_MSKBL   = 0x02,
    A_INTSFUNC_EXTBL   = 0x06,
    A_INTSFUNC_INSBL   = 0x0B,
    A_INTSFUNC_MSKWL   = 0x12,
    A_INTSFUNC_EXTWL   = 0x16,
    A_INTSFUNC_INSWL   = 0x1B,
    A_INTSFUNC_MSKLL   = 0x22,
    A_INTSFUNC_EXTLL   = 0x26,
    A_INTSFUNC_INSLL   = 0x2B,
    A_INTSFUNC_ZAP     = 0x30,
    A_INTSFUNC_ZAPNOT  = 0x31,
    A_INTSFUNC_MSKQL   = 0x32,
    A_INTSFUNC_SRL     = 0x34,
    A_INTSFUNC_EXTQL   = 0x36,
    A_INTSFUNC_SLL     = 0x39,
    A_INTSFUNC_INSQL   = 0x3B,
    A_INTSFUNC_SRA     = 0x3C,
    A_INTSFUNC_MSKWH   = 0x52,
    A_INTSFUNC_INSWH   = 0x57,
    A_INTSFUNC_EXTWH   = 0x5A,
    A_INTSFUNC_MSKLH   = 0x62,
    A_INTSFUNC_INSLH   = 0x67,
    A_INTSFUNC_EXTLH   = 0x6A,
    A_INTSFUNC_MSKQH   = 0x72,
    A_INTSFUNC_INSQH   = 0x77,
    A_INTSFUNC_EXTQH   = 0x7A,
};

// INTM Instructions.
enum {
    A_INTMFUNC_MULL    = 0x00,
    A_INTMFUNC_DIVL    = 0x08,
    A_INTMFUNC_MULQ    = 0x20,
    A_INTMFUNC_DIVQ    = 0x28,
    A_INTMFUNC_UMULH   = 0x30,
    A_INTMFUNC_MULL_V  = 0x40,
    A_INTMFUNC_UDIVL   = 0x48,
    A_INTMFUNC_MULQ_V  = 0x60,
    A_INTMFUNC_UDIVQ   = 0x68,
};

// FLTV Instructions.
enum {
    A_FLTVFUNC_ADDF_C      = 0x000,
    A_FLTVFUNC_SUBF_C      = 0x001,
    A_FLTVFUNC_MULF_C      = 0x002,
    A_FLTVFUNC_DIVF_C      = 0x003,
    A_FLTVFUNC_CVTDG_C     = 0x01E,
    A_FLTVFUNC_ADDG_C      = 0x020,
    A_FLTVFUNC_SUBG_C      = 0x021,
    A_FLTVFUNC_MULG_C      = 0x022,
    A_FLTVFUNC_DIVG_C      = 0x023,
    A_FLTVFUNC_CVTGF_C     = 0x02C,
    A_FLTVFUNC_CVTGD_C     = 0x02D,
    A_FLTVFUNC_CVTGQ_C     = 0x02F,
    A_FLTVFUNC_CVTQF_C     = 0x03C,
    A_FLTVFUNC_CVTQG_C     = 0x03E,
    A_FLTVFUNC_ADDF        = 0x080,
    A_FLTVFUNC_SUBF        = 0x081,
    A_FLTVFUNC_MULF        = 0x082,
    A_FLTVFUNC_DIVF        = 0x083,
    A_FLTVFUNC_CVTDG       = 0x09E,
    A_FLTVFUNC_ADDG        = 0x0A0,
    A_FLTVFUNC_SUBG        = 0x0A1,
    A_FLTVFUNC_MULG        = 0x0A2,
    A_FLTVFUNC_DIVG        = 0x0A3,
    A_FLTVFUNC_CMPGEQ      = 0x0A5,
    A_FLTVFUNC_CMPGLT      = 0x0A6,
    A_FLTVFUNC_CMPGLE      = 0x0A7,
    A_FLTVFUNC_CVTGF       = 0x0AC,
    A_FLTVFUNC_CVTGD       = 0x0AD,
    A_FLTVFUNC_CVTGQ       = 0x0AF,
    A_FLTVFUNC_CVTQF       = 0x0BC,
    A_FLTVFUNC_CVTQG       = 0x0BE,
    A_FLTVFUNC_ADDF_UC     = 0x100,
    A_FLTVFUNC_SUBF_UC     = 0x101,
    A_FLTVFUNC_MULF_UC     = 0x102,
    A_FLTVFUNC_DIVF_UC     = 0x103,
    A_FLTVFUNC_CVTDG_UC    = 0x11E,
    A_FLTVFUNC_ADDG_UC     = 0x120,
    A_FLTVFUNC_SUBG_UC     = 0x121,
    A_FLTVFUNC_MULG_UC     = 0x122,
    A_FLTVFUNC_DIVG_UC     = 0x123,
    A_FLTVFUNC_CVTGF_UC    = 0x12C,
    A_FLTVFUNC_CVTGD_UC    = 0x12D,
    A_FLTVFUNC_CVTGQ_VC    = 0x12F,
    A_FLTVFUNC_ADDF_U      = 0x180,
    A_FLTVFUNC_SUBF_U      = 0x181,
    A_FLTVFUNC_MULF_U      = 0x182,
    A_FLTVFUNC_DIVF_U      = 0x183,
    A_FLTVFUNC_CVTDG_U     = 0x19E,
    A_FLTVFUNC_ADDG_U      = 0x1A0,
    A_FLTVFUNC_SUBG_U      = 0x1A1,
    A_FLTVFUNC_MULG_U      = 0x1A2,
    A_FLTVFUNC_DIVG_U      = 0x1A3,
    A_FLTVFUNC_CVTGF_U     = 0x1AC,
    A_FLTVFUNC_CVTGD_U     = 0x1AD,
    A_FLTVFUNC_CVTGQ_V     = 0x1AF,
    A_FLTVFUNC_ADDF_SC     = 0x400,
    A_FLTVFUNC_SUBF_SC     = 0x401,
    A_FLTVFUNC_MULF_SC     = 0x402,
    A_FLTVFUNC_DIVF_SC     = 0x403,
    A_FLTVFUNC_CVTDG_SC    = 0x41E,
    A_FLTVFUNC_ADDG_SC     = 0x420,
    A_FLTVFUNC_SUBG_SC     = 0x421,
    A_FLTVFUNC_MULG_SC     = 0x422,
    A_FLTVFUNC_DIVG_SC     = 0x423,
    A_FLTVFUNC_CVTGF_SC    = 0x42C,
    A_FLTVFUNC_CVTGD_SC    = 0x42D,
    A_FLTVFUNC_CVTGQ_SC    = 0x42F,
    A_FLTVFUNC_ADDF_S      = 0x480,
    A_FLTVFUNC_SUBF_S      = 0x481,
    A_FLTVFUNC_MULF_S      = 0x482,
    A_FLTVFUNC_DIVF_S      = 0x483,
    A_FLTVFUNC_CVTDG_S     = 0x49E,
    A_FLTVFUNC_ADDG_S      = 0x4A0,
    A_FLTVFUNC_SUBG_S      = 0x4A1,
    A_FLTVFUNC_MULG_S      = 0x4A2,
    A_FLTVFUNC_DIVG_S      = 0x4A3,
    A_FLTVFUNC_CMPGEQ_S    = 0x4A5,
    A_FLTVFUNC_CMPGLT_S    = 0x4A6,
    A_FLTVFUNC_CMPGLE_S    = 0x4A7,
    A_FLTVFUNC_CVTGF_S     = 0x4AC,
    A_FLTVFUNC_CVTGD_S     = 0x4AD,
    A_FLTVFUNC_CVTGQ_S     = 0x4AF,
    A_FLTVFUNC_ADDF_SUC    = 0x500,
    A_FLTVFUNC_SUBF_SUC    = 0x501,
    A_FLTVFUNC_MULF_SUC    = 0x502,
    A_FLTVFUNC_DIVF_SUC    = 0x503,
    A_FLTVFUNC_CVTDG_SUC   = 0x51E,
    A_FLTVFUNC_ADDG_SUC    = 0x520,
    A_FLTVFUNC_SUBG_SUC    = 0x521,
    A_FLTVFUNC_MULG_SUC    = 0x522,
    A_FLTVFUNC_DIVG_SUC    = 0x523,
    A_FLTVFUNC_CVTGF_SUC   = 0x52C,
    A_FLTVFUNC_CVTGD_SUC   = 0x52D,
    A_FLTVFUNC_CVTGQ_SVC   = 0x52F,
    A_FLTVFUNC_ADDF_SU     = 0x580,
    A_FLTVFUNC_SUBF_SU     = 0x581,
    A_FLTVFUNC_MULF_SU     = 0x582,
    A_FLTVFUNC_DIVF_SU     = 0x583,
    A_FLTVFUNC_CVTDG_SU    = 0x59E,
    A_FLTVFUNC_ADDG_SU     = 0x5A0,
    A_FLTVFUNC_SUBG_SU     = 0x5A1,
    A_FLTVFUNC_MULG_SU     = 0x5A2,
    A_FLTVFUNC_DIVG_SU     = 0x5A3,
    A_FLTVFUNC_CVTGF_SU    = 0x5AC,
    A_FLTVFUNC_CVTGD_SU    = 0x5AD,
    A_FLTVFUNC_CVTGQ_SV    = 0x5AF,
};

// FLTI Instructions
enum {
    A_FLTIFUNC_ADDS_C       = 0x000,
    A_FLTIFUNC_SUBS_C       = 0x001,
    A_FLTIFUNC_MULS_C       = 0x002,
    A_FLTIFUNC_DIVS_C       = 0x003,
    A_FLTIFUNC_ADDT_C       = 0x020,
    A_FLTIFUNC_SUBT_C       = 0x021,
    A_FLTIFUNC_MULT_C       = 0x022,
    A_FLTIFUNC_DIVT_C       = 0x023,
    A_FLTIFUNC_CVTTS_C      = 0x02C,
    A_FLTIFUNC_CVTTQ_C      = 0x02F,
    A_FLTIFUNC_CVTQS_C      = 0x03C,
    A_FLTIFUNC_CVTQT_C      = 0x03E,
    A_FLTIFUNC_ADDS_M       = 0x040,
    A_FLTIFUNC_SUBS_M       = 0x041,
    A_FLTIFUNC_MULS_M       = 0x042,
    A_FLTIFUNC_DIVS_M       = 0x043,
    A_FLTIFUNC_ADDT_M       = 0x060,
    A_FLTIFUNC_SUBT_M       = 0x061,
    A_FLTIFUNC_MULT_M       = 0x062,
    A_FLTIFUNC_DIVT_M       = 0x063,
    A_FLTIFUNC_CVTTS_M      = 0x06C,
    A_FLTIFUNC_CVTTQ_M      = 0x06F,
    A_FLTIFUNC_CVTQS_M      = 0x07C,
    A_FLTIFUNC_CVTQT_M      = 0x07E,
    A_FLTIFUNC_ADDS         = 0x080,
    A_FLTIFUNC_SUBS         = 0x081,
    A_FLTIFUNC_MULS         = 0x082,
    A_FLTIFUNC_DIVS         = 0x083,
    A_FLTIFUNC_ADDT         = 0x0A0,
    A_FLTIFUNC_SUBT         = 0x0A1,
    A_FLTIFUNC_MULT         = 0x0A2,
    A_FLTIFUNC_DIVT         = 0x0A3,
    A_FLTIFUNC_CMPTUN       = 0x0A4,
    A_FLTIFUNC_CMPTEQ       = 0x0A5,
    A_FLTIFUNC_CMPTLT       = 0x0A6,
    A_FLTIFUNC_CMPTLE       = 0x0A7,
    A_FLTIFUNC_CVTTS        = 0x0AC,
    A_FLTIFUNC_CVTTQ        = 0x0AF,
    A_FLTIFUNC_CVTQS        = 0x0BC,
    A_FLTIFUNC_CVTQT        = 0x0BE,
    A_FLTIFUNC_ADDS_D       = 0x0C0,
    A_FLTIFUNC_SUBS_D       = 0x0C1,
    A_FLTIFUNC_MULS_D       = 0x0C2,
    A_FLTIFUNC_DIVS_D       = 0x0C3,
    A_FLTIFUNC_ADDT_D       = 0x0E0,
    A_FLTIFUNC_SUBT_D       = 0x0E1,
    A_FLTIFUNC_MULT_D       = 0x0E2,
    A_FLTIFUNC_DIVT_D       = 0x0E3,
    A_FLTIFUNC_CVTTS_D      = 0x0EC,
    A_FLTIFUNC_CVTTQ_D      = 0x0EF,
    A_FLTIFUNC_CVTQS_D      = 0x0FC,
    A_FLTIFUNC_CVTQT_D      = 0x0FE,
    A_FLTIFUNC_ADDS_UC      = 0x100,
    A_FLTIFUNC_SUBS_UC      = 0x101,
    A_FLTIFUNC_MULS_UC      = 0x102,
    A_FLTIFUNC_DIVS_UC      = 0x103,
    A_FLTIFUNC_ADDT_UC      = 0x120,
    A_FLTIFUNC_SUBT_UC      = 0x121,
    A_FLTIFUNC_MULT_UC      = 0x122,
    A_FLTIFUNC_DIVT_UC      = 0x123,
    A_FLTIFUNC_CVTTS_UC     = 0x12C,
    A_FLTIFUNC_CVTTQ_VC     = 0x12F,
    A_FLTIFUNC_ADDS_UM      = 0x140,
    A_FLTIFUNC_SUBS_UM      = 0x141,
    A_FLTIFUNC_MULS_UM      = 0x142,
    A_FLTIFUNC_DIVS_UM      = 0x143,
    A_FLTIFUNC_ADDT_UM      = 0x160,
    A_FLTIFUNC_SUBT_UM      = 0x161,
    A_FLTIFUNC_MULT_UM      = 0x162,
    A_FLTIFUNC_DIVT_UM      = 0x163,
    A_FLTIFUNC_CVTTS_UM     = 0x16C,
    A_FLTIFUNC_CVTTQ_VM     = 0x16F,
    A_FLTIFUNC_ADDS_U       = 0x180,
    A_FLTIFUNC_SUBS_U       = 0x181,
    A_FLTIFUNC_MULS_U       = 0x182,
    A_FLTIFUNC_DIVS_U       = 0x183,
    A_FLTIFUNC_ADDT_U       = 0x1A0,
    A_FLTIFUNC_SUBT_U       = 0x1A1,
    A_FLTIFUNC_MULT_U       = 0x1A2,
    A_FLTIFUNC_DIVT_U       = 0x1A3,
    A_FLTIFUNC_CVTTS_U      = 0x1AC,
    A_FLTIFUNC_CVTTQ_V      = 0x1AF,
    A_FLTIFUNC_ADDS_UD      = 0x1C0,
    A_FLTIFUNC_SUBS_UD      = 0x1C1,
    A_FLTIFUNC_MULS_UD      = 0x1C2,
    A_FLTIFUNC_DIVS_UD      = 0x1C3,
    A_FLTIFUNC_ADDT_UD      = 0x1E0,
    A_FLTIFUNC_SUBT_UD      = 0x1E1,
    A_FLTIFUNC_MULT_UD      = 0x1E2,
    A_FLTIFUNC_DIVT_UD      = 0x1E3,
    A_FLTIFUNC_CVTTS_UD     = 0x1EC,
    A_FLTIFUNC_CVTTQ_VD     = 0x1EF,
    A_FLTIFUNC_CVTST        = 0x2AC,
    A_FLTIFUNC_ADDS_SUC     = 0x500,
    A_FLTIFUNC_SUBS_SUC     = 0x501,
    A_FLTIFUNC_MULS_SUC     = 0x502,
    A_FLTIFUNC_DIVS_SUC     = 0x503,
    A_FLTIFUNC_ADDT_SUC     = 0x520,
    A_FLTIFUNC_SUBT_SUC     = 0x521,
    A_FLTIFUNC_MULT_SUC     = 0x522,
    A_FLTIFUNC_DIVT_SUC     = 0x523,
    A_FLTIFUNC_CVTTS_SUC    = 0x52C,
    A_FLTIFUNC_CVTTQ_SVC    = 0x52F,
    A_FLTIFUNC_ADDS_SUM     = 0x540,
    A_FLTIFUNC_SUBS_SUM     = 0x541,
    A_FLTIFUNC_MULS_SUM     = 0x542,
    A_FLTIFUNC_DIVS_SUM     = 0x543,
    A_FLTIFUNC_ADDT_SUM     = 0x560,
    A_FLTIFUNC_SUBT_SUM     = 0x561,
    A_FLTIFUNC_MULT_SUM     = 0x562,
    A_FLTIFUNC_DIVT_SUM     = 0x563,
    A_FLTIFUNC_CVTTS_SUM    = 0x56C,
    A_FLTIFUNC_CVTTQ_SVM    = 0x56F,
    A_FLTIFUNC_ADDS_SU      = 0x580,
    A_FLTIFUNC_SUBS_SU      = 0x581,
    A_FLTIFUNC_MULS_SU      = 0x582,
    A_FLTIFUNC_DIVS_SU      = 0x583,
    A_FLTIFUNC_ADDT_SU      = 0x5A0,
    A_FLTIFUNC_SUBT_SU      = 0x5A1,
    A_FLTIFUNC_MULT_SU      = 0x5A2,
    A_FLTIFUNC_DIVT_SU      = 0x5A3,
    A_FLTIFUNC_CMPTUN_SU    = 0x5A4,
    A_FLTIFUNC_CMPTEQ_SU    = 0x5A5,
    A_FLTIFUNC_CMPTLT_SU    = 0x5A6,
    A_FLTIFUNC_CMPTLE_SU    = 0x5A7,
    A_FLTIFUNC_CVTTS_SU     = 0x5AC,
    A_FLTIFUNC_CVTTQ_SV     = 0x5AF,
    A_FLTIFUNC_ADDS_SUD     = 0x5C0,
    A_FLTIFUNC_SUBS_SUD     = 0x5C1,
    A_FLTIFUNC_MULS_SUD     = 0x5C2,
    A_FLTIFUNC_DIVS_SUD     = 0x5C3,
    A_FLTIFUNC_ADDT_SUD     = 0x5E0,
    A_FLTIFUNC_SUBT_SUD     = 0x5E1,
    A_FLTIFUNC_MULT_SUD     = 0x5E2,
    A_FLTIFUNC_DIVT_SUD     = 0x5E3,
    A_FLTIFUNC_CVTTS_SUD    = 0x5EC,
    A_FLTIFUNC_CVTTQ_SVD    = 0x5EF,
    A_FLTIFUNC_CVTST_S      = 0x6AC,
    A_FLTIFUNC_ADDS_SUIC    = 0x700,
    A_FLTIFUNC_SUBS_SUIC    = 0x701,
    A_FLTIFUNC_MULS_SUIC    = 0x702,
    A_FLTIFUNC_DIVS_SUIC    = 0x703,
    A_FLTIFUNC_ADDT_SUIC    = 0x720,
    A_FLTIFUNC_SUBT_SUIC    = 0x721,
    A_FLTIFUNC_MULT_SUIC    = 0x722,
    A_FLTIFUNC_DIVT_SUIC    = 0x723,
    A_FLTIFUNC_CVTTS_SUIC   = 0x72C,
    A_FLTIFUNC_CVTTQ_SVIC   = 0x72F,
    A_FLTIFUNC_CVTQS_SUIC   = 0x73C,
    A_FLTIFUNC_CVTQT_SUIC   = 0x73E,
    A_FLTIFUNC_ADDS_SUIM    = 0x740,
    A_FLTIFUNC_SUBS_SUIM    = 0x741,
    A_FLTIFUNC_MULS_SUIM    = 0x742,
    A_FLTIFUNC_DIVS_SUIM    = 0x743,
    A_FLTIFUNC_ADDT_SUIM    = 0x760,
    A_FLTIFUNC_SUBT_SUIM    = 0x761,
    A_FLTIFUNC_MULT_SUIM    = 0x762,
    A_FLTIFUNC_DIVT_SUIM    = 0x763,
    A_FLTIFUNC_CVTTS_SUIM   = 0x76C,
    A_FLTIFUNC_CVTTQ_SVIM   = 0x76F,
    A_FLTIFUNC_CVTQS_SUIM   = 0x77C,
    A_FLTIFUNC_CVTQT_SUIM   = 0x77E,
    A_FLTIFUNC_ADDS_SUI     = 0x780,
    A_FLTIFUNC_SUBS_SUI     = 0x781,
    A_FLTIFUNC_MULS_SUI     = 0x782,
    A_FLTIFUNC_DIVS_SUI     = 0x783,
    A_FLTIFUNC_ADDT_SUI     = 0x7A0,
    A_FLTIFUNC_SUBT_SUI     = 0x7A1,
    A_FLTIFUNC_MULT_SUI     = 0x7A2,
    A_FLTIFUNC_DIVT_SUI     = 0x7A3,
    A_FLTIFUNC_CVTTS_SUI    = 0x7AC,
    A_FLTIFUNC_CVTTQ_SVI    = 0x7AF,
    A_FLTIFUNC_CVTQS_SUI    = 0x7BC,
    A_FLTIFUNC_CVTQT_SUI    = 0x7BE,
    A_FLTIFUNC_ADDS_SUID    = 0x7C0,
    A_FLTIFUNC_SUBS_SUID    = 0x7C1,
    A_FLTIFUNC_MULS_SUID    = 0x7C2,
    A_FLTIFUNC_DIVS_SUID    = 0x7C3,
    A_FLTIFUNC_ADDT_SUID    = 0x7E0,
    A_FLTIFUNC_SUBT_SUID    = 0x7E1,
    A_FLTIFUNC_MULT_SUID    = 0x7E2,
    A_FLTIFUNC_DIVT_SUID    = 0x7E3,
    A_FLTIFUNC_CVTTS_SUID   = 0x7EC,
    A_FLTIFUNC_CVTTQ_SVID   = 0x7EF,
    A_FLTIFUNC_CVTQS_SUID   = 0x7FC,
    A_FLTIFUNC_CVTQT_SUID   = 0x7FE,
};

// FLTL Instructions
enum {
    A_FLTIFUNC_CVTLQ       = 0x010,
    A_FLTIFUNC_CPYS        = 0x020,
    A_FLTIFUNC_CPYSN       = 0x021,
    A_FLTIFUNC_CPYSE       = 0x022,
    A_FLTIFUNC_MT_FPCR     = 0x024,
    A_FLTIFUNC_MF_FPCR     = 0x025,
    A_FLTIFUNC_FCMOVEQ     = 0x02A,
    A_FLTIFUNC_FCMOVNE     = 0x02B,
    A_FLTIFUNC_FCMOVLT     = 0x02C,
    A_FLTIFUNC_FCMOVGE     = 0x02D,
    A_FLTIFUNC_FCMOVLE     = 0x02E,
    A_FLTIFUNC_FCMOVGT     = 0x02F,
    A_FLTIFUNC_CVTQL       = 0x030,
    A_FLTIFUNC_CVTQL_V     = 0x130,
    A_FLTIFUNC_CVTQL_SV    = 0x530,
};

// FPTI Instructions
enum {
    A_FPTIFUNC_SEXTB	= 0x00,
    A_FPTIFUNC_SEXTW	= 0x01,
    A_FPTIFUNC_CTPOP	= 0x30,
    A_FPTIFUNC_PERR	    = 0x31,
    A_FPTIFUNC_CTLZ		= 0x32,
    A_FPTIFUNC_CTTZ		= 0x33,
    A_FPTIFUNC_UNPKBW   = 0x34,
    A_FPTIFUNC_UNPKBL   = 0x35,
    A_FPTIFUNC_PKWB     = 0x36,
    A_FPTIFUNC_PKLB     = 0x37,
    A_FPTIFUNC_MINSB8   = 0x38,
    A_FPTIFUNC_MINSW4   = 0x39,
    A_FPTIFUNC_MINUB8   = 0x3A,
    A_FPTIFUNC_MINUW4   = 0x3B,
    A_FPTIFUNC_MAXUB8   = 0x3C,
    A_FPTIFUNC_MAXUW4   = 0x3D,
    A_FPTIFUNC_MAXSB8   = 0x3E,
    A_FPTIFUNC_MAXSW4   = 0x3F,
    A_FPTIFUNC_FTOIT	= 0x70,
    A_FPTIFUNC_FTOIS	= 0x78,
};

// ITFP Instructions
enum {
    A_ITFPFUNC_ITOFS       = 0x004,
    A_ITFPFUNC_ITOFF       = 0x014,
    A_ITFPFUNC_ITOFT       = 0x024,
    A_ITFPFUNC_SQRTF       = 0x08A,
    A_ITFPFUNC_SQRTF_C     = 0x00A,
    A_ITFPFUNC_SQRTF_S     = 0x48A,
    A_ITFPFUNC_SQRTF_SC    = 0x40A,
    A_ITFPFUNC_SQRTF_SU    = 0x58A,
    A_ITFPFUNC_SQRTF_SUC   = 0x50A,
    A_ITFPFUNC_SQRTF_U     = 0x18A,
    A_ITFPFUNC_SQRTF_UC    = 0x10A,
    A_ITFPFUNC_SQRTG       = 0x0AA,
    A_ITFPFUNC_SQRTG_C     = 0x02A,
    A_ITFPFUNC_SQRTG_S     = 0x4AA,
    A_ITFPFUNC_SQRTG_SC    = 0x42A,
    A_ITFPFUNC_SQRTG_SU    = 0x5AA,
    A_ITFPFUNC_SQRTG_SUC   = 0x52A,
    A_ITFPFUNC_SQRTG_U     = 0x1AA,
    A_ITFPFUNC_SQRTG_UC    = 0x12A,
    A_ITFPFUNC_SQRTS       = 0x08B,
    A_ITFPFUNC_SQRTS_C     = 0x00B,
    A_ITFPFUNC_SQRTS_D     = 0x0CB,
    A_ITFPFUNC_SQRTS_M     = 0x04B,
    A_ITFPFUNC_SQRTS_SU    = 0x58B,
    A_ITFPFUNC_SQRTS_SUC   = 0x50B,
    A_ITFPFUNC_SQRTS_SUD   = 0x5CB,
    A_ITFPFUNC_SQRTS_SUIC  = 0x70B,
    A_ITFPFUNC_SQRTS_SUID  = 0x7CB,
    A_ITFPFUNC_SQRTS_SUIM  = 0x74B,
    A_ITFPFUNC_SQRTS_SUM   = 0x54B,
    A_ITFPFUNC_SQRTS_SUU   = 0x78B,
    A_ITFPFUNC_SQRTS_U     = 0x18B,
    A_ITFPFUNC_SQRTS_UC    = 0x10B,
    A_ITFPFUNC_SQRTS_UD    = 0x1CB,
    A_ITFPFUNC_SQRTS_UM    = 0x14B,
    A_ITFPFUNC_SQRTT       = 0x0AB,
    A_ITFPFUNC_SQRTT_C     = 0x02B,
    A_ITFPFUNC_SQRTT_D     = 0x0EB,
    A_ITFPFUNC_SQRTT_M     = 0x06B,
    A_ITFPFUNC_SQRTT_SU    = 0x5AB,
    A_ITFPFUNC_SQRTT_SUC   = 0x52B,
    A_ITFPFUNC_SQRTT_SUD   = 0x5EB,
    A_ITFPFUNC_SQRTT_SUI   = 0x7AB,
    A_ITFPFUNC_SQRTT_SUIC  = 0x72B,
    A_ITFPFUNC_SQRTT_SUID  = 0x7EB,
    A_ITFPFUNC_SQRTT_SUIM  = 0x76B,
    A_ITFPFUNC_SQRTT_SUM   = 0x56B,
    A_ITFPFUNC_SQRTT_U     = 0x1AB,
    A_ITFPFUNC_SQRTT_UC    = 0x12B,
    A_ITFPFUNC_SQRTT_UD    = 0x1EB,
    A_ITFPFUNC_SQRTT_UM    = 0x16B,
};

// Masks for the AMASK instruction
enum {
    AMASK_BWX   = 0x001,
    AMASK_FIX   = 0x002,
    AMASK_CIX   = 0x004,
    AMASK_MVI   = 0x100,
    AMASK_TRAP  = 0x200,
};

// Values for the IMPLVER instruction
enum {
    IMPLVER_EV4 = 0,
    IMPLVER_EV5 = 1,
    IMPLVER_EV6 = 2,
};

// Floating Point Control Register flags
static const FPCR FPCR_SUM      = 0x8000000000000000ULL;
static const FPCR FPCR_INED     = 0x4000000000000000ULL;
static const FPCR FPCR_UNFD     = 0x2000000000000000ULL;
static const FPCR FPCR_UNDZ     = 0x1000000000000000ULL;
static const FPCR FPCR_DYN_RM   = 0x0C00000000000000ULL;
static const FPCR FPCR_DYN_RM_C = 0x0000000000000000ULL; // Chopped
static const FPCR FPCR_DYN_RM_M = 0x0400000000000000ULL; // Minus
static const FPCR FPCR_DYN_RM_N = 0x0800000000000000ULL; // Normal
static const FPCR FPCR_DYN_RM_P = 0x0C00000000000000ULL; // Plus
static const FPCR FPCR_IOV      = 0x0200000000000000ULL;
static const FPCR FPCR_INE      = 0x0100000000000000ULL;
static const FPCR FPCR_UNF      = 0x0080000000000000ULL;
static const FPCR FPCR_OVF      = 0x0040000000000000ULL;
static const FPCR FPCR_DZE      = 0x0020000000000000ULL;
static const FPCR FPCR_INV      = 0x0010000000000000ULL;
static const FPCR FPCR_OVFD     = 0x0008000000000000ULL;
static const FPCR FPCR_DZED     = 0x0004000000000000ULL;
static const FPCR FPCR_INVD     = 0x0002000000000000ULL;
static const FPCR FPCR_DNZ      = 0x0001000000000000ULL;
static const FPCR FPCR_DNOD     = 0x0000800000000000ULL;

// Latch information for Pipeline
struct ArchDecodeReadLatch
{
    InstrFormat format;
    int32_t     displacement;
    uint16_t    function;
    uint8_t     opcode;

    ArchDecodeReadLatch() : format(IFORMAT_INVALID), displacement(0), function(0), opcode(0) {}
    virtual ~ArchDecodeReadLatch() {}
};

typedef ArchDecodeReadLatch ArchReadExecuteLatch;

#endif
