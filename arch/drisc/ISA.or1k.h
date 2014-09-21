// -*- c++ -*-
#ifndef ISA_OR1K_H
#define ISA_OR1K_H

#ifndef PIPELINE_H
#error This file should be included in Pipeline.h
#endif

// FIXME: define ISA constants here.

// Latch information for Pipeline
struct ArchDecodeReadLatch
{
    // FIXME: FILL ADDITIONAL LATCH FIELDS HERE.

    // examples:
    uint16_t function; /* opcode to run in execute */
    int32_t  displacement; /* displacement field for relative memory operations  */

    ArchDecodeReadLatch() :
    // MB: all latch fields should be initialized here.
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
