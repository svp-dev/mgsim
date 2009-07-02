/*
  Note about the Read Stage:
 
  An operand can be several registers big, so data for an operand can
  come from different bypasses. We keep track of which register
  sub-values we've read, and keep checking bypasses until we got all
  data. Afterwards, we may still need to read parts from the Register
  File.
*/
#include "Pipeline.h"
#include "Processor.h"
#include "Network.h"
#if TARGET_ARCH == ARCH_SPARC
#include "ISA.sparc.h"
#endif
#include <cassert>
#include <sstream>
#include <iomanip>
using namespace std;

namespace Simulator
{

static uint64_t MAKE_MASK(int num_bits)
{
    // Shift in two steps so shifting
    // by the entire register width works.
    uint64_t mask = 1;
    mask <<= num_bits / 2;
    mask <<= (num_bits - num_bits / 2);
    return mask - 1;
}

// Convert a RegValue into a PipeValue
static PipeValue RegToPipeValue(RegType type, const RegValue& src_value)
{
    PipeValue dest_value;
    dest_value.m_state = src_value.m_state;
    dest_value.m_size  = sizeof(Integer);
    switch (src_value.m_state)
    {
    case RST_INVALID: assert(0); break;
    case RST_WAITING:
    case RST_EMPTY:
        dest_value.m_waiting = src_value.m_waiting;
        dest_value.m_remote  = src_value.m_remote;
        dest_value.m_memory  = src_value.m_memory;
        break;
        
    case RST_FULL:
        {
            // Make bit-mask and bit-offsets
            switch (type)
            {
            case RT_INTEGER: dest_value.m_integer.set(src_value.m_integer, dest_value.m_size);
            case RT_FLOAT:   dest_value.m_float.fromint(src_value.m_float.integer, dest_value.m_size);
            }
        }
        break;
    }
    return dest_value;
}

static void CopyRegister(RegType type, const PipeValue& src_value, RegSize src_offset, PipeValue& dest_value, RegSize dest_offset)
{
    assert(src_value.m_state == RST_FULL);

    // Make bit-mask and bit-offsets
#ifdef ARCH_BIG_ENDIAN
    src_offset  = src_value.m_size  / sizeof(Integer) - 1 - src_offset;
    dest_offset = dest_value.m_size / sizeof(Integer) - 1 - dest_offset;
#endif
    const uint64_t     mask       = MAKE_MASK(sizeof(Integer) * 8);
    const unsigned int src_shift  = src_offset  * sizeof(Integer) * 8;
    const unsigned int dest_shift = dest_offset * sizeof(Integer) * 8;
    switch (type)
    {
        case RT_INTEGER:
            dest_value.m_integer.set(
                (dest_value.m_integer.get(dest_value.m_size) & ~(mask << dest_shift)) |
                (((src_value.m_integer.get(src_value.m_size) >> src_shift) & mask) << dest_shift),
                dest_value.m_size);
            break;
            
        case RT_FLOAT:
            dest_value.m_float.fromint(
                (dest_value.m_float.toint(dest_value.m_size) & ~(mask << dest_shift)) |
                (((src_value.m_float.toint(src_value.m_size) >> src_shift) & mask) << dest_shift),
                dest_value.m_size);
            break;
    }
}

/*
 This function figures out what part of the desired operand is in the source
 PipeValue and copies the relevant data.
 
 The return value is:
  = 0 if nothing was read from the source operand.
  < 0 if at least one register is required from the source, but it's not full.
  > 0 if at least one register has been copied from the source.
 
 In the latter two cases, the return value is a register mask. In the first of the two cases, the register mask
 identifies the registers in the destination operand that were wanted. In the second case, the register mask
 identifies the registers in the destination operand that were written.
*/
static int ReadBypass(const RegAddr& src_addr, const PipeValue& src_value, const RegAddr& dest_addr, PipeValue& dest_value, unsigned int to_read_mask)
{
    assert( src_addr.valid());
    assert(dest_addr.valid());
    assert(src_addr.type == dest_addr.type);
    assert( src_value.m_size % sizeof(Integer) == 0);
    assert(dest_value.m_size % sizeof(Integer) == 0);
        
    // See what parts of the source and destination we're interested in
    const RegSize  src_size   =  src_value.m_size / sizeof(Integer);
    const RegSize dest_size   = dest_value.m_size / sizeof(Integer);
    const RegSize  src_offset = (dest_addr.index > src_addr.index) ? dest_addr.index -  src_addr.index : 0;
    const RegSize dest_offset = (dest_addr.index < src_addr.index) ?  src_addr.index - dest_addr.index : 0;
    
    if (src_offset < src_size && dest_offset < dest_size)
    {
        // There is overlap between source and destination, get its size
        const RegSize size = min(src_size - src_offset, dest_size - dest_offset);
        
        // All checks are relative to the destination offset.
        // Also mask the read_mask to get the registers we can actually read.
        to_read_mask = (to_read_mask >> dest_offset) & ((1 << size) - 1);

        // Check if there's any part of the overlap that we need
        if (to_read_mask != 0)
        {
            // Yes, check the state of the source and fill the destination
            switch (src_value.m_state)
            {
                case RST_INVALID:
                case RST_WAITING:
                case RST_EMPTY:
                    // The operand was not full.
                    // In the return value, specify which registers in the destination operand we wanted.
                    return -(to_read_mask << dest_offset);

                case RST_FULL:
                {
                    // Copy all registers in the operand that we want
                    dest_value.m_state = RST_FULL;
                    for (RegSize i = 0; i < size; ++i)
                    {
                        if (to_read_mask & (1 << i))
                        {
                            CopyRegister(
                                src_addr.type,
                                src_value,  src_offset  + i,
                                dest_value, dest_offset + i
                            );
                        }
                    }
                    
                    // Return the registers that we've copied
                    return to_read_mask << dest_offset;
                }
            }
            assert(0);
        }
    }
    // We've read nothing
    return 0;
}

/**
 * Prepares to read a (possibly multi-register) operand by reading the register file.
 * @param[in] addr register address of the base of the operand
 * @param[in] size size of the operand, in registers
 * @param[in,out] operand persistent information about the reading of the operand for multi-cycle reads
 * @return true if the operation succeeded. This does not have to indicate the entire register has been read.
 */
bool Pipeline::ReadStage::ReadRegister(OperandInfo& operand, uint32_t literal)
{
    if (operand.to_read_mask == -1)
    {
        // This is a new operand read; initialize operand value
        operand.value.m_state = RST_FULL;
        if (operand.addr.valid()) {
            // Valid new read; mark entire operand as unread and initialize to zero
            literal = 0;
            operand.to_read_mask = (1 << (operand.value.m_size / sizeof(Integer))) - 1;
        } else {
            // Invalid read -- use the literal, and everything's been read
            operand.to_read_mask = 0;
        }
        
        switch (operand.addr.type)
        {
            case RT_INTEGER: operand.value.m_integer.set  (literal, operand.value.m_size); break;
            case RT_FLOAT:   operand.value.m_float.fromint(literal, operand.value.m_size); break;
        }
    }
    
    if (operand.to_read_mask != 0)
    {
        // Part of the operand still needs to be read from the register file
        if (!operand.port->Read())
        {
            return false;
        }

        // Find the first register to read
        unsigned int offset, mask = operand.to_read_mask;
        for (offset = 0; ~mask & 1; mask >>= 1, offset++) {}

        operand.addr_reg = MAKE_REGADDR(operand.addr.type, operand.addr.index + offset);
        RegValue value;
        if (!m_regFile.ReadRegister(operand.addr_reg, value))
        {
            return false;
        }

        // Convert the register value to a PipeValue.
        // That way, all ReadStage inputs are PipeValues.
        operand.value_reg = RegToPipeValue(operand.addr_reg.type, value);
    }
    
    // Data was read
    return true;
}

bool Pipeline::ReadStage::ReadBypasses(OperandInfo& operand)
{
    // We don't perform this function in the ACQUIRE phase because
    // we don't have to acquire any ports, but it can still 'fail' in
    // the VERIFY phase, when the EX stage's output indicates a memory
    // load, and we have to wait a cycle to get the actual value.
    // Of course, in the VERIFY phase, the other stages will have already
    // been run, so all bypasses have the correct values.
    if (operand.addr.valid())
    {
        // We have a valid address; read the operand
        static const unsigned int N_INPUTS = 4;
        const struct InputInfo
        {
            bool       empty;
            RegAddr&   addr;
            PipeValue& value;
        } bypasses[N_INPUTS] = {
            {false,           operand.addr_reg, operand.value_reg},
            {m_wblatch.empty, m_wblatch.Rc,     m_wblatch.Rcv},
            {m_bypass2.empty, m_bypass2.Rc,     m_bypass2.Rcv},
            {m_bypass1.empty, m_bypass1.Rc,     m_bypass1.Rcv},
        };

        // If an operand is not full, we need to keep track of its contents,
        // and address of first register.
        unsigned int nonfull_mask  = 0;
        RegAddr      nonfull_addr  = INVALID_REG;
        PipeValue    nonfull_value = MAKE_EMPTY_PIPEVALUE(sizeof(Integer));

        // Iterate over all inputs from back to front. This in effect 'replays'
        // the last instructions and gets the correct final value.
        unsigned int combined_read_mask = 0;
        for (unsigned int i = 0; i < N_INPUTS; ++i)
        {
            const InputInfo& ii = bypasses[i];
            if (!ii.empty && ii.addr.valid() && ii.addr.type == operand.addr.type)
            {
                // Non-empty bypass of the same type; read it
                int read_mask = ReadBypass(ii.addr, ii.value, operand.addr, operand.value, operand.to_read_mask);
                if (read_mask < 0)
                {
                    // A part of the operand was not full. read_mask contains
                    // which registers (negated).
                    read_mask = -read_mask;

                    // If the input value is waiting, but the desired value turns out to have
                    // been written earlier, don't wait.
                    if (ii.value.m_state != RST_WAITING || (combined_read_mask & read_mask) != (unsigned int)read_mask)
                    {
                        // The operand value was not touched, remember the operand
                        // for the empty state (memory/remote request, waiting queue)
                        if (ii.value.m_state == RST_EMPTY && nonfull_value.m_state == RST_WAITING)
                        {
                            // This bypass resets a waiting register. Ignore the new value and
                            // use the waiting value.
                        }
                        else
                        {
                            nonfull_value = ii.value;
                        }
                        nonfull_mask |= read_mask;

                        // Get the address of the first non-full register to be able to wait on it.
                        nonfull_addr = operand.addr;
                        while (~read_mask & 1) {
                            ++nonfull_addr.index;
                            read_mask >>= 1;
                        }
                        read_mask = 0;
                    }
                }
                
                if (read_mask != 0)
                {
                    // We've read part of the operand from this bypass
                    nonfull_mask       &= ~read_mask;    // These bits are now full
                    combined_read_mask |=  read_mask;    // And have been read
                }
            }
        }
        
        if (nonfull_mask != 0)
        {
            // At least one register was not full in the end.
            if (nonfull_value.m_state == RST_INVALID)
            {
                // Drop what we're doing -- we need to try again next cycle.
                // This is when the most recent result will actually be generated
                // later in the pipeline (e.g., a memory load is picked up before
                // the Memory Stage).
                return false;
            }

            // Wait on it
            operand.addr  = nonfull_addr;
            operand.value = nonfull_value;
            
            // Pretend we've read everything since the entire operand
            // will now be waiting on the wanted register.
            operand.to_read_mask = 0;
        }
        else
        {
            // We no longer have to read the registers that we read
            operand.to_read_mask &= ~combined_read_mask;
        }
    }
    return true;
}

Pipeline::PipeAction Pipeline::ReadStage::read()
{
    OperandInfo operand1(m_operand1);
    OperandInfo operand2(m_operand2);

    // Initialize the operand states
    operand1.addr         = m_input.Ra;
    operand1.value.m_size = m_input.RaSize;
    operand2.addr         = m_input.Rb;
    operand2.value.m_size = m_input.RbSize;
    
    // Copy the writeback latch because it'll be gone in the Write phase
    m_wblatch = m_bypass2;
    
#if TARGET_ARCH == ARCH_SPARC
    m_isMemoryStore = false;
    if (m_input.op1 == S_OP1_MEMORY)
    {
        switch (m_input.op3)
        {
            case S_OP3_STB: case S_OP3_STBA:
            case S_OP3_STH: case S_OP3_STHA:
            case S_OP3_ST:  case S_OP3_STA:
            case S_OP3_STD: case S_OP3_STDA:
            case S_OP3_STF: case S_OP3_STDF:
                // Stores on the Sparc require multiple cycles. First cycle we
                // read the value to store. After that the two registers for
                // the address.
                m_isMemoryStore = true;
                break;
        }
    }

    if (m_isMemoryStore && m_storeValue.m_state == RST_INVALID)
    {
        // Initial phase of memory store; read to-be-stored value
        operand1.addr         = m_input.Rc;
        operand1.value.m_size = m_input.RcSize;
        operand2.addr         = INVALID_REG;
    }
#endif

    if (!ReadRegister(operand1, 0))
    {
        DeadlockWrite("Unable to read operand #1's register");
        return PIPE_STALL;
    }
    
    // Use the literal if the second operand is not valid
    if (!ReadRegister(operand2, m_input.literal))
    {
        DeadlockWrite("Unable to read operand #2's register");
        return PIPE_STALL;
    }
    
    COMMIT
    {
        m_operand1 = operand1;
        m_operand2 = operand2;
    }
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::ReadStage::write()
{
    if (IsAcquiring())
    {
        // We're not acquiring anything in the write phase,
        // so everything went well. This simplifies the rest of
        // the code a bit.
        return PIPE_CONTINUE;
    }
    
    OperandInfo operand1( m_operand1 );
    OperandInfo operand2( m_operand2 );

    if (!ReadBypasses(operand1))
    {
        DeadlockWrite("Unable to read bypasses for operand #1");
        return PIPE_STALL;
    }
    
    if (!ReadBypasses(operand2))
    {
        DeadlockWrite("Unable to read bypasses for operand #2");
        return PIPE_STALL;
    }
    
    if (operand1.to_read_mask != 0 || operand2.to_read_mask != 0)
    {
        // Both operands haven't been fully read yet -- delay
        COMMIT
        {
            m_operand1 = operand1;
            m_operand2 = operand2;
        }
        return PIPE_DELAY;
    }
    else
    {
        // We're done with these operands -- reset for new operands next cycle
        COMMIT
        {
            m_operand1.to_read_mask = -1;
            m_operand2.to_read_mask = -1;
        }
    }

    COMMIT
    {
        // Copy common latch data
        (Latch&)m_output               = m_input;
        (ArchDecodeReadLatch&)m_output = m_input;
        
        m_output.Ra      = operand1.addr;
        m_output.Rb      = operand2.addr;
        m_output.Rc      = m_input.Rc;
        m_output.Rra.fid = INVALID_LFID;
        m_output.Rrb.fid = INVALID_LFID;
        m_output.Rrc     = m_input.Rrc;
        m_output.Rav     = operand1.value;
        m_output.Rbv     = operand2.value;
        m_output.Rcv.m_size = m_input.RcSize;
        m_output.regs       = m_input.regs;
    }

    if (operand1.value.m_state != RST_FULL)
    {
        // Register wasn't full, write back the suspend information
        if (operand1.value.m_state != RST_WAITING && m_input.Rra.fid != INVALID_LFID)
        {
            // Send a remote request unless a thread is already
            // waiting on it (that thread has already sent the request).
            RemoteRegAddr rra(m_input.Rra);
            rra.reg.index += (operand1.addr.index - m_input.Ra.index);
            COMMIT{ m_output.Rra = rra; }
        }
        
        COMMIT
        {
            m_output.Rc                 = operand1.addr;
            m_output.Rrc.fid            = INVALID_LFID;
            m_output.Rav.m_state        = RST_WAITING;
            m_output.Rav.m_waiting.head = m_input.tid;
            m_output.Rav.m_waiting.tail = (operand1.value.m_state == RST_WAITING)
                ? operand1.value.m_waiting.tail     // The register was already waiting, append thread to list
                : m_input.tid;                      // First thread waiting on the register
        }
    }
    else if (operand2.value.m_state != RST_FULL)
    {
        // Register wasn't full, write back the suspend information
        if (operand2.value.m_state != RST_WAITING && m_input.Rrb.fid != INVALID_LFID)
        {
            // Send a remote request unless a thread is already
            // waiting on it (that thread has already sent the request).
            RemoteRegAddr rrb(m_input.Rrb);
            rrb.reg.index += (operand2.addr.index - m_input.Rb.index);
            COMMIT{ m_output.Rrb = rrb; }
        }

        COMMIT
        {
            m_output.Rc                 = operand2.addr;
            m_output.Rrc.fid            = INVALID_LFID;
            m_output.Rbv.m_state        = RST_WAITING;
            m_output.Rbv.m_waiting.head = m_input.tid;
            m_output.Rbv.m_waiting.tail = (operand2.value.m_state == RST_WAITING)
                ? operand2.value.m_waiting.tail     // The register was already waiting, append thread to list
                : m_input.tid;                      // First thread waiting on the register
        }
    }
    else
    {
#if TARGET_ARCH == ARCH_SPARC
        // On the Sparc, memory stores take longer because three registers
        // need to be read. We do this by first reading the value to store
        // and then the two address registers in the next cycle.
        if (m_isMemoryStore)
        {
            if (m_storeValue.m_state != RST_FULL)
            {
                // Store value hasn't been read yet; read it
                COMMIT {
                    if (operand1.to_read_mask <= 0) {
                        // First phase of the store has completed,
                        // copy the read value.
                        m_storeValue = operand1.value;
                    }
                }
                // We need to delay (stall) this cycle
                return PIPE_DELAY;
            }
        
            COMMIT {
                // Final cycle of the store
                m_output.storeValue  = m_storeValue;
                m_storeValue.m_state = RST_INVALID;
            }
        }
#endif
    }
    return PIPE_CONTINUE;
}

void Pipeline::ReadStage::clear(TID tid)
{
    if (m_input.tid == tid)
    {
        m_operand1.to_read_mask = -1;
        m_operand2.to_read_mask = -1;
    }
}

Pipeline::ReadStage::ReadStage(Pipeline& parent, DecodeReadLatch& input, ReadExecuteLatch& output, RegisterFile& regFile, ExecuteMemoryLatch& bypass1, MemoryWritebackLatch& bypass2, const Config& /*config*/)
  : Stage(parent, "read", &input, &output),
    m_regFile(regFile),
    m_input(input),
    m_output(output),
    m_bypass1(bypass1),
    m_bypass2(bypass2)
{
#if TARGET_ARCH == ARCH_SPARC
    m_isMemoryStore = false;
    m_storeValue.m_state = RST_INVALID;
#endif
    m_operand1.port = &m_regFile.p_pipelineR1;
    m_operand2.port = &m_regFile.p_pipelineR2;
    clear(input.tid);
}

}
