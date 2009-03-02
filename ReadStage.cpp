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
using namespace Simulator;
using namespace std;

// Uncomment this define if the Read Stage doesn't read properly.
// #define DEBUG_READ_STAGE

static uint64_t MAKE_MASK(int num_bits)
{
    uint64_t mask = 1;
    mask <<= num_bits / 2;
    mask <<= (num_bits - num_bits / 2);
    return mask - 1;
}

static bool CopyRegister(RegType type, const PipeValue& src_value, RegSize src_offset, PipeValue& dest_value, RegSize dest_offset)
{
    dest_value.m_state = src_value.m_state;
    switch (src_value.m_state)
    {
    case RST_INVALID: break;
    case RST_EMPTY:   break;
        
    case RST_WAITING:
        dest_value.m_tid = src_value.m_tid;
        // Fall-through
    case RST_PENDING:
        dest_value.m_request   = src_value.m_request;
        dest_value.m_component = src_value.m_component;
        break;
        
    case RST_FULL:
        {
            // Make bit-mask and bit-offsets
#ifdef ARCH_BIG_ENDIAN
            src_offset  = src_value.m_size  / sizeof(Integer) - 1 - src_offset;
            dest_offset = dest_value.m_size / sizeof(Integer) - 1 - dest_offset;
#endif
            uint64_t     mask       = MAKE_MASK(sizeof(Integer) * 8);
            unsigned int src_shift  = src_offset  * sizeof(Integer) * 8;
            unsigned int dest_shift = dest_offset * sizeof(Integer) * 8;
            switch (type)
            {
            case RT_INTEGER:
                dest_value.m_integer.set(
                    dest_value.m_integer.get(dest_value.m_size) |
                    (((src_value.m_integer.get(src_value.m_size) >> src_shift) & mask) << dest_shift),
                    dest_value.m_size);
                break;
            
            case RT_FLOAT:
                dest_value.m_float.fromint(
                    dest_value.m_float.toint(dest_value.m_size) |
                    (((src_value.m_float.toint(src_value.m_size) >> src_shift) & mask) << dest_shift),
                    dest_value.m_size);
                break;
            }
        }
        return true;
    }
    return false;
}

static bool CopyRegister(RegType type, const RegValue& src_value, PipeValue& dest_value, RegSize dest_offset)
{
    dest_value.m_state = src_value.m_state;
    switch (src_value.m_state)
    {
    case RST_INVALID: assert(0);
    case RST_EMPTY:   break;
    case RST_WAITING:
        dest_value.m_tid = src_value.m_tid;
        // Fall-through
    case RST_PENDING:
        dest_value.m_request   = src_value.m_request;
        dest_value.m_component = src_value.m_component;
        break;
        
    case RST_FULL:
        {
            // Make bit-mask and bit-offsets
#ifdef ARCH_BIG_ENDIAN
            dest_offset = dest_value.m_size / sizeof(Integer) - 1 - dest_offset;
#endif
            unsigned int dest_shift = dest_offset * sizeof(Integer) * 8;
            switch (type)
            {
            case RT_INTEGER:
                dest_value.m_integer.set(
                    dest_value.m_integer.get(dest_value.m_size) | ((uint64_t)src_value.m_integer << dest_shift),
                    dest_value.m_size);
                break;
            
            case RT_FLOAT:
                dest_value.m_float.fromint(
                    dest_value.m_float.toint(dest_value.m_size) | ((uint64_t)src_value.m_float.integer << dest_shift),
                    dest_value.m_size);
                break;
            }
        }
        return true;
    }
    return false;
}

static int ReadBypass(const RegAddr& src_addr, const PipeValue& src_value, const RegAddr& dest_addr, PipeValue& dest_value, unsigned int to_read_mask, bool read)
{
    unsigned int read_mask = 0;
    
    // The bypass stage output should be of the same type
    if (src_addr.valid() && src_addr.type == dest_addr.type)
    {
        assert(src_value.m_size % sizeof(Integer) == 0);
        
        RegSize  src_size    = src_value.m_size / sizeof(Integer);
        RegSize dest_size   = dest_value.m_size / sizeof(Integer);
        RegSize  src_offset = (dest_addr.index > src_addr.index) ? dest_addr.index -  src_addr.index : 0;
        RegSize dest_offset = (dest_addr.index < src_addr.index) ?  src_addr.index - dest_addr.index : 0;
        
        for (to_read_mask >>= dest_offset;
             src_offset < src_size && to_read_mask != 0;
             to_read_mask >>= 1, dest_offset++, src_offset++)
        {
            if (dest_offset < dest_size && to_read_mask & 1)
            {
                if (read)
                {
                    if (!CopyRegister(
                        src_addr.type,
                        src_value,  src_offset,
                        dest_value, dest_offset
                        ))
                    {
                        // The data was not RST_FULL.
                        // In the return value, specify which sub-register wasn't full.
                        return -(1 + src_offset);
                    }
                }
                read_mask |= (1 << dest_offset);
            }
        }
    }
    return read_mask;
}

/**
 * Reads a (possibly multi-register) operand.
 * @param[in] addr register address of the base of the operand
 * @param[in] size size of the operand, in registers
 * @param[in,out] operand persistent information about the reading of the operand for multi-cycle reads
 * @return true if the operation succeeded. This does not have to indicate the entire register has been read.
 */
bool Pipeline::ReadStage::ReadRegister(OperandInfo& operand)
{
    if (operand.to_read_mask == -1)
    {
        // This is a new operand read; initialize operand value to zero
        operand.value.m_state = RST_FULL;
        switch (operand.addr.type)
        {
            case RT_INTEGER: operand.value.m_integer.set  (0, operand.value.m_size); break;
            case RT_FLOAT:   operand.value.m_float.fromint(0, operand.value.m_size); break;
        }

        operand.to_read_mask = operand.addr.valid()
            // Valid new read; mark entire operand as unread
            ? (1 << (operand.value.m_size / sizeof(Integer))) - 1
            // Invalid read -- everything's been read
            : 0;

#ifdef DEBUG_READ_STAGE
        printf("[CPU %u] New operand read: %d\n", m_parent.GetProcessor().GetPID(), operand.to_read_mask);
#endif
    }
    
    if (operand.addr.valid())
    {
        // We have a valid address; read the operand
        static const unsigned int N_BYPASSES = 3;
        const struct BypassInfo
        {
            const Pipeline::Latch& latch;
            RegAddr&               addr;
            PipeValue&             value;
            bool                   read;
        } bypasses[N_BYPASSES] = {
            {m_output,  m_output .Rc, m_output .Rcv, false},    // Read-Execute latch
            {m_bypass1, m_bypass1.Rc, m_bypass1.Rcv, false},    // Execute-Memory latch
            {m_bypass2, m_bypass2.Rc, m_bypass2.Rcv, true }     // Memory-Writeback latch
        };

        // We check the bypasses with a copy of the read mask, because we don't
        // actually read the bypasses here, we just need to check if they'll have
        // the data in the Write phase.
        unsigned int to_read_mask = operand.to_read_mask;

        // Iterate over all bypasses as long as we still have sub-values to read
        for (unsigned int i = 0; i < N_BYPASSES && to_read_mask != 0; i++)
        {
            const BypassInfo& bi = bypasses[i];
            if (!bi.latch.empty())
            {
                // Non-empty bypass; check it
                int read_mask = ReadBypass(bi.addr, bi.value, operand.addr, operand.value, operand.to_read_mask, bi.read);
                if (read_mask < 0)
                {
#ifdef DEBUG_READ_STAGE
                    printf("[CPU %u] Got it NON-FULL from bypass #%d\n", m_parent.GetProcessor().GetPID(), i );
#endif
                    // A part of the operand was not full. read_mask contains
                    // the register offset. Since we have to suspend on this
                    // register, pretend we've read everything to stop reading.
                    if (operand.value.m_state == RST_INVALID)
                    {
                        // Drop what we're doing -- we need to try again next cycle
                        return false;
                    }
                    operand.addr = bi.addr;
                    operand.addr.index += -read_mask - 1;
                    to_read_mask = operand.to_read_mask = 0;
                }
                else if (read_mask > 0)
                {
                    // We can read part of the operand from this bypass later
#ifdef DEBUG_READ_STAGE
                    printf("[CPU %u] Get it in write stage from bypass #%d\n", m_parent.GetProcessor().GetPID(), i );
#endif
                    to_read_mask &= ~read_mask;
                    if (bi.read)
                    {
                        operand.to_read_mask &= ~read_mask;
                    }
                }
            }
        }

        if (to_read_mask != 0)
        {
#ifdef DEBUG_READ_STAGE
            printf("[CPU %u] Reading from registers %04x: %d\n", m_parent.GetProcessor().GetPID(), operand.addr.index, to_read_mask );
#endif
            // Part of the operand still needs to be read from the register file
            if (!operand.port->Read(*this))
            {
                return false;
            }

            // Find the first register to read
            unsigned int offset, mask = to_read_mask;
            for (offset = 0; ~mask & 1; mask >>= 1, offset++);
                       
            RegValue value;
            if (!m_regFile.ReadRegister(MAKE_REGADDR(operand.addr.type, operand.addr.index + offset), value))
            {
                return false;
            }
            
            // Place the read register into the final value
            if (!CopyRegister(operand.addr.type, value, operand.value, offset))
            {
                // A part of the operand was not full.
                operand.addr.index += offset;
                to_read_mask = operand.to_read_mask = 0;
            }
            else
            {
                // We've read this register part
                operand.to_read_mask &= ~(1 << offset);
            }
        }
    }
    
    // Data was read
    return true;
}

bool Pipeline::ReadStage::ReadBypasses(OperandInfo& operand)
{
    if (operand.addr.valid())
    {
        // We have a valid address; read the operand
        static const unsigned int N_BYPASSES = 2;
        const struct BypassInfo
        {
            const Pipeline::Latch& latch;
            RegAddr&               addr;
            PipeValue&             value;
        } bypasses[N_BYPASSES + 1] = {
            {m_output,  m_output .Rc, m_output .Rcv},
            {m_bypass1, m_bypass1.Rc, m_bypass1.Rcv},
            {m_bypass2, m_bypass2.Rc, m_bypass2.Rcv}
        };
        
        unsigned int start = !IsAcquiring() ? 1 : 0;
        
        // Iterate over all bypasses as long as we still have sub-values to read
        for (unsigned int i = 0; i < N_BYPASSES && operand.to_read_mask != 0; i++)
        {
            const BypassInfo& bi = bypasses[start + i];
            if (!bi.latch.empty())
            {
                // Non-empty bypass; read it
                int read_mask = ReadBypass(bi.addr, bi.value, operand.addr, operand.value, operand.to_read_mask, !IsAcquiring());
                if (read_mask < 0)
                {
#ifdef DEBUG_READ_STAGE
                    printf("[CPU %u] Got it from bypass #%d: NOT FULL\n", m_parent.GetProcessor().GetPID(), i );
#endif
                    // A part of the operand was not full. read_mask contains
                    // the register offset. Since we have to suspend on this
                    // register, pretend we've read everything to stop reading.
                    if (operand.value.m_state == RST_INVALID)
                    {
                        // Drop what we're doing -- we need to try again next cycle
                        return false;
                    }
                    operand.addr = bi.addr;
                    operand.addr.index += -read_mask - 1;
                    operand.to_read_mask = 0;
                }
                else if (read_mask != 0)
                {
#ifdef DEBUG_READ_STAGE
                    printf("[CPU %u] Got it from bypass #%d\n", m_parent.GetProcessor().GetPID(), i );
#endif
                    // We've read part of the operand from this bypass
                    operand.to_read_mask &= ~read_mask;
                }
            }
        }
    }
    return true;
}

Pipeline::PipeAction Pipeline::ReadStage::read()
{
    OperandInfo operand1(m_operand1);
    OperandInfo operand2(m_operand2);

    operand1.addr         = m_input.Ra;
    operand1.value.m_size = m_input.RaSize;
    operand2.addr         = m_input.Rb;
    operand2.value.m_size = m_input.RbSize;
    
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

#ifdef DEBUG_READ_STAGE
    printf("[CPU %u][R] Before reading: %d, %d\n", m_parent.GetProcessor().GetPID(), operand1.to_read_mask, operand2.to_read_mask );
    printf("[CPU %u][R] Reading operand 1\n", m_parent.GetProcessor().GetPID());
#endif
    if (!ReadRegister(operand1))
    {
#ifdef DEBUG_READ_STAGE
        printf("Stall\n");
#endif
        return PIPE_STALL;
    }
#ifdef DEBUG_READ_STAGE
    printf("[CPU %u][R] Operand 1: %016llx\n",
        m_parent.GetProcessor().GetPID(),
        (operand1.addr.type == RT_INTEGER)
        ? operand1.value.m_integer.get(operand1.value.m_size)
        : operand1.value.m_float.toint(operand1.value.m_size));
#endif

    if (!operand2.addr.valid())
    {
        // Use the literal if the second operand is not valid
        operand2.value.m_integer = m_input.literal;
        operand2.value.m_state   = RST_FULL;
        operand2.to_read_mask    = 0;
    }
    else
    {
#ifdef DEBUG_READ_STAGE
        printf("[CPU %u][R] Reading operand 2\n", m_parent.GetProcessor().GetPID());
#endif
        if (!ReadRegister(operand2))
        {
#ifdef DEBUG_READ_STAGE
            printf("Stall\n");
#endif
            return PIPE_STALL;
        }
#ifdef DEBUG_READ_STAGE
    printf("[CPU %u][R] Operand 2: %016llx\n",
        m_parent.GetProcessor().GetPID(),
        (operand2.addr.type == RT_INTEGER)
        ? operand2.value.m_integer.get(operand2.value.m_size)
        : operand2.value.m_float.toint(operand2.value.m_size));
#endif
    }
#ifdef DEBUG_READ_STAGE
    printf("[CPU %u][R] After reading: %d, %d\n", m_parent.GetProcessor().GetPID(), operand1.to_read_mask, operand2.to_read_mask );
#endif

    COMMIT
    {
        m_operand1 = operand1;
        m_operand2 = operand2;
    }
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::ReadStage::write()
{
    OperandInfo operand1( m_operand1 );
    OperandInfo operand2( m_operand2 );

#ifdef DEBUG_READ_STAGE
    printf("[CPU %u][W] Before reading: %d, %d\n", m_parent.GetProcessor().GetPID(), operand1.to_read_mask, operand2.to_read_mask );
    printf("[CPU %u][W] Reading operand 1\n", m_parent.GetProcessor().GetPID());
#endif
    if (!ReadBypasses(operand1))
    {
#ifdef DEBUG_READ_STAGE
        printf("Stall\n");
#endif
        return PIPE_STALL;
    }
#ifdef DEBUG_READ_STAGE
    printf("[CPU %u][W] Operand 1: %016llx\n",
        m_parent.GetProcessor().GetPID(),
        (operand1.addr.type == RT_INTEGER)
        ? operand1.value.m_integer.get(operand1.value.m_size)
        : operand1.value.m_float.toint(operand1.value.m_size));
#endif
    
#ifdef DEBUG_READ_STAGE
    printf("[CPU %u][W] Reading operand 2\n", m_parent.GetProcessor().GetPID());
#endif
    if (!ReadBypasses(operand2))
    {
#ifdef DEBUG_READ_STAGE
        printf("Stall\n");
#endif
        return PIPE_STALL;
    }
#ifdef DEBUG_READ_STAGE
    printf("[CPU %u][W] Operand 2: %016llx\n",
        m_parent.GetProcessor().GetPID(),
        (operand2.addr.type == RT_INTEGER)
        ? operand2.value.m_integer.get(operand2.value.m_size)
        : operand2.value.m_float.toint(operand2.value.m_size));
#endif
    
    COMMIT
    {
        m_operand1 = operand1;
        m_operand2 = operand2;
    }
    
#ifdef DEBUG_READ_STAGE
    printf("[CPU %u][W] After reading: %d, %d\n", m_parent.GetProcessor().GetPID(), operand1.to_read_mask, operand2.to_read_mask );
#endif
    if (operand1.to_read_mask != 0 || operand2.to_read_mask != 0)
    {
        // Both operands haven't been fully read yet -- delay
#ifdef DEBUG_READ_STAGE
        printf("Delaying\n");
#endif
        return PIPE_DELAY;
    }
    else
    {
        // We're done with these operands -- reset for new operands next cycle
#ifdef DEBUG_READ_STAGE
        printf("Done\n");
#endif
        COMMIT
        {
            m_operand1.to_read_mask = -1;
            m_operand2.to_read_mask = -1;
        }
    }

    COMMIT
    {
        // Copy common latch data
        (CommonLatch&)m_output         = m_input;
        (ArchDecodeReadLatch&)m_output = m_input;
        m_output.Ra   = m_operand1.addr;
        m_output.Rb   = m_operand2.addr;
        m_output.Rc   = m_input.Rc;
        m_output.Rrc  = m_input.Rrc;
        m_output.Rav  = m_operand1.value;
        m_output.Rbv  = m_operand2.value;
        m_output.Rcv.m_size = m_input.RcSize;
        m_output.regs = m_input.regs;
    }

    if (operand1.value.m_state != RST_FULL)
    {
        // Register wasn't full, write back the suspend information
        if (m_input.Rra.fid != INVALID_GFID)
        {
            // Send a remote request
            RegAddr rra(m_input.Rra.reg);
            rra.index += (operand1.addr.index - m_input.Ra.index);
            
			DebugSimWrite("Requesting remote shared %s for G%u", rra.str().c_str(), m_input.Rra.fid);
			if (!m_network.RequestShared(m_input.Rra.fid, rra, m_input.isFirstThreadInFamily))
            {
#ifdef DEBUG_READ_STAGE
                printf("Stall\n");
#endif
                return PIPE_STALL;
            }
        }
        
        COMMIT
        {
            m_output.Rc          = operand1.addr;
            m_output.Rrc.fid     = INVALID_GFID;
			m_output.Rav.m_tid   = m_input.tid;
            m_output.Rav.m_state = RST_WAITING;
        }
    }
    else if (operand2.value.m_state != RST_FULL)
    {
        // Register wasn't full, write back the suspend information
        if (m_input.Rrb.fid != INVALID_GFID)
        {
            // Send a remote request
            RegAddr rrb(m_input.Rrb.reg);
            rrb.index += (operand2.addr.index - m_input.Rb.index);
            
			DebugSimWrite("Requesting remote shared %s for family G%u", rrb.str().c_str(), m_input.Rrb.fid);
			if (!m_network.RequestShared(m_input.Rrb.fid, rrb, m_input.isFirstThreadInFamily))
            {
#ifdef DEBUG_READ_STAGE
                printf("Stall\n");
#endif
                return PIPE_STALL;
            }
        }

        COMMIT
        {
            m_output.Rc          = operand2.addr;
            m_output.Rrc.fid     = INVALID_GFID;
            m_output.Rbv.m_tid   = m_input.tid;
            m_output.Rbv.m_state = RST_WAITING;
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
#ifdef DEBUG_READ_STAGE
            printf("Handling memory store, value full: %d\n", m_storeValue.m_state == RST_FULL);
#endif
            if (m_storeValue.m_state != RST_FULL)
            {
                // Store value hasn't been read yet; read it
                COMMIT {
                    if (m_operand1.to_read_mask <= 0) {
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
#ifdef DEBUG_READ_STAGE
    printf("Continuing\n");
#endif
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

Pipeline::ReadStage::ReadStage(Pipeline& parent, DecodeReadLatch& input, ReadExecuteLatch& output, RegisterFile& regFile, Network& network, ExecuteMemoryLatch& bypass1, MemoryWritebackLatch& bypass2)
  : Stage(parent, "read", &input, &output),
    m_regFile(regFile),
    m_network(network),
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
    clear(0);
}
