#ifndef ISA_MIPS_H
#define ISA_MIPS_H

#ifndef PIPELINE_H
#error This file should be included in Pipeline.h
#endif

// Latch information for Pipeline
struct ArchDecodeReadLatch
{
    /* the fields in this structure become buffers in the pipeline latch. */

    // examples:
    uint16_t function; /* opcode to run in execute */
    int32_t  displacement; /* displacement field for relative memory operations  */

    // FIXME: FILL ADDITIONAL LATCH FIELDS HERE.

    ArchDecodeReadLatch() : 
    /* NB: all latch fields should be initialized here. */
         function(0), 
         displacement(0) 
    {}
};

struct ArchReadExecuteLatch : public ArchDecodeReadLatch
{
    // FIXME: FILL ADDITIONAL READ-EXECUTE LATCHES HERE (IF NECESSARY)

    /* NB: the buffers in ArchDecodeReadLatch are automatically
       propagated to the read-execute latch. */
};


#endif 
