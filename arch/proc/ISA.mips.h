#ifndef ISA_MIPS_H
#define ISA_MIPS_H

#ifndef PIPELINE_H
#error This file should be included in Pipeline.h
#endif

// Latch information for Pipeline
struct ArchDecodeReadLatch
{
    uint16_t function;
    int32_t  displacement;

   // FIXME: FILL ADDITIONAL LATCH FIELDS HERE.

ArchDecodeReadLatch() : function(0), displacement(0) {}
};

struct ArchReadExecuteLatch : public ArchDecodeReadLatch
{
    // FIXME: FILL ADDITIONAL READ-EXECUTE LATCHES HERE (IF NECESSARY)
};


#endif 
