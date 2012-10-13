#ifndef ISA_MIPS_H
#define ISA_MIPS_H

#ifndef PIPELINE_H
#error This file should be included in Pipeline.h
#endif

enum InstrFormat {
    IFORMAT_ITYPE,
    IFORMAT_REGIMM,
    IFORMAT_JTYPE,
    IFORMAT_RTYPE,
};

enum {
    M_OP_SPECIAL = 0x0, /* R-type */
    M_OP_REGIMM = 0x1,
    M_OP_J = 0x2,
    M_OP_JAL = 0x3,
    M_OP_BEQ = 0x4,
    M_OP_BNE = 0x5,
    M_OP_BLEZ = 0x6,
    M_OP_BGTZ = 0x7,
    M_OP_ADDI = 0x8,
    M_OP_ADDIU = 0x9,
    M_OP_SLTI = 0xa,
    M_OP_SLTIU = 0xb,
    M_OP_ANDI = 0xc,
    M_OP_ORI = 0xd,
    M_OP_XORI = 0xe,
    M_OP_LUI = 0xf,
    // (COPx)
    M_OP_LB = 0x20,
    M_OP_LH = 0x21,
    //M_OP_LWL = 0x22,
    M_OP_LW = 0x23,
    M_OP_LBU = 0x24,
    M_OP_LHU = 0x25,
    //M_OP_LWR = 0x26,
    M_OP_SB = 0x28,
    M_OP_SH = 0x29,
    //M_OP_SWL = 0x2a,
    M_OP_SW = 0x2b
    //M_OP_SWR = 0x2e
    // (LWCx, SWCx)
};

enum {
    M_ROP_SLL = 0x0,
    M_ROP_SRL = 0x2,
    M_ROP_SRA = 0x3,
    M_ROP_SLLV = 0x4,
    M_ROP_SRLV = 0x6,
    M_ROP_SRAV = 0x7,
    M_ROP_JR = 0x8,
    M_ROP_JALR = 0x9,
    M_ROP_SYSCALL = 0xc,
    M_ROP_BREAK = 0xd,
    M_ROP_MFHI = 0x10,
    M_ROP_MTHI = 0x11,
    M_ROP_MFLO = 0x12,
    M_ROP_MTLO = 0x13,
    M_ROP_MULT = 0x18,
    M_ROP_MULTU = 0x19,
    M_ROP_DIV = 0x1a,
    M_ROP_DIVU = 0x1b,
    M_ROP_ADD = 0x20,
    M_ROP_ADDU = 0x21,
    M_ROP_SUB = 0x22,
    M_ROP_SUBU = 0x23,
    M_ROP_AND = 0x24,
    M_ROP_OR = 0x25,
    M_ROP_XOR = 0x26,
    M_ROP_NOR = 0x27,
    M_ROP_SLT = 0x2a,
    M_ROP_SLTU = 0x2b
};

enum {
    M_REGIMMOP_BLTZ = 0x0,
    M_REGIMMOP_BGEZ = 0x1,
    //M_REGIMMOP_BGEZL = 0x3,
    M_REGIMMOP_BLTZAL = 0x10,
    M_REGIMMOP_BGEZAL = 0x11,
    //M_REGIMMOP_BGEZALL = 0x13
};


// Latch information for Pipeline
struct ArchDecodeReadLatch
{
    /* the fields in this structure become buffers in the pipeline latch. */

     InstrFormat format;
     uint16_t opcode; /* opcode for non-R-type instructions */
     uint16_t function; /* opcode for R-type instructions */
     uint16_t regimm; /* opcode for REGIMM instructions */
     uint16_t shift; /* shift amount for R-type instructions */
     uint16_t immediate; /* immediate for I-type/REGIMM instructions */
     int32_t displacement; /* jump target for J-type instructions */
 
     ArchDecodeReadLatch() : 
          format(IFORMAT_RTYPE),
          opcode(0),
          function(0),
          regimm(0),
          shift(0),
          immediate(0),
          displacement(0)
    {}
};

struct ArchReadExecuteLatch : public ArchDecodeReadLatch
{
};


#endif
