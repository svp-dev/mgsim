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
    case RST_PENDING:
    case RST_EMPTY:
        dest_value.m_waiting = src_value.m_waiting;
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

/**
 * Prepares to read a (possibly multi-register) operand by reading the register file.
 * @param[in] addr register address of the base of the operand
 * @param[in] size size of the operand, in registers
 * @param[in,out] operand persistent information about the reading of the operand for multi-cycle reads
 * @return true if the operation succeeded. This does not have to indicate the entire register has been read.
 */
bool Pipeline::ReadStage::ReadRegister(OperandInfo& operand, uint32_t literal)
{
    if (operand.offset == -2)
    {
        // This is a new operand read; initialize operand value
        operand.value.m_state = RST_FULL;
        if (operand.addr.valid()) {
            // Valid new read; initialize value to zero.
            // We start reading at the highest sub-register, so we can decrement
            // and when we hit -1 we're done.
            literal = 0;
            operand.offset = operand.value.m_size / sizeof(Integer) - 1;
        } else {
            // Invalid read -- use the literal, and everything's been read
            operand.offset = -1;
        }
        
        switch (operand.addr.type)
        {
            case RT_INTEGER: operand.value.m_integer.set  (literal, operand.value.m_size); break;
            case RT_FLOAT:   operand.value.m_float.fromint(literal, operand.value.m_size); break;
        }
    }
    
    if (operand.offset >= 0)
    {
        // Part of the operand still needs to be read from the register file
        if (!operand.port->Read())
        {
            return false;
        }

        operand.addr_reg = MAKE_REGADDR(operand.addr.type, operand.addr.index + operand.offset);
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
    if (!operand.addr.valid() || operand.offset < 0)
    {
        // No data to read? Then we're done
        return true;
    }

    //
    // We have a valid address and data to read; read the bypasses.
    //
    // We iterate over all inputs from back to front. This in effect 'replays'
    // the last instructions and gets the correct final value.
    //
    
    // Set the read register at the end of the bypass list to make
    // the code simple. This is removed at the end.
    static const bool g_false = false;
    m_bypasses.push_back(BypassInfo(g_false, operand.addr_reg, operand.value_reg));

    // This variable keeps the value of the register as we replay the instructions
    RegValue value = MAKE_EMPTY_REG();
    value.m_state = RST_EMPTY;

    typedef vector<BypassInfo>::const_reverse_iterator it_t;
    for (it_t p = m_bypasses.rbegin(); p != (it_t)m_bypasses.rend(); ++p)
    {
        if (*p->empty || !p->addr->valid() ||                        // Empty latch or no result
            p->addr->type != operand.addr.type ||                    // Value type mismatch
            operand.addr.index + operand.offset <  p->addr->index || // Register not in operand's register range
            operand.addr.index + operand.offset >= p->addr->index + p->value->m_size / sizeof(Integer))
        {
            // This bypass does not hold the register that we want
            continue;
        }
    
        // The wanted register is present in the source.
        // Check the state of the source.
        switch (p->value->m_state)
        {
        case RST_INVALID:
        case RST_WAITING:
        case RST_PENDING:
        case RST_EMPTY:
            // The source was not FULL
            if (p->value->m_state == RST_WAITING && value.m_state == RST_FULL)
            {
                // If the source value is WAITING, but the desired value turns out to have
                // been written earlier, ignore the source.
            }
            // The operand value was not touched, remember the operand
            // for the empty state (memory/remote request, waiting queue)
            else if (p->value->m_state == RST_PENDING && value.m_state == RST_WAITING)
            {
                // This bypass resets a waiting register. Ignore the new value and
                // use the waiting value.
            }
            else
            {
                // Set this as the 'current' value: copy the information
                value.m_state   = p->value->m_state;
                value.m_waiting = p->value->m_waiting;
                value.m_memory  = p->value->m_memory;
            }
            break;

        case RST_FULL:
            // Full always overwrites everything else.
            // Get the register from the pipeline value.
            value.m_state = RST_FULL;
                
            // Make bit-mask and bit-offsets
            unsigned int offset = operand.addr.index + operand.offset - p->addr->index;
#ifdef ARCH_BIG_ENDIAN
            offset = p->value->m_size  / sizeof(Integer) - 1 - offset;
#endif
            const unsigned int shift = offset * sizeof(Integer) * 8;
            switch (p->addr->type)
            {
            case RT_INTEGER: value.m_integer       = (Integer)(p->value->m_integer.get(p->value->m_size) >> shift); break;
            case RT_FLOAT:   value.m_float.integer = (Integer)(p->value->m_float.toint(p->value->m_size) >> shift); break;
            }
            break;
        }
    }
    
    m_bypasses.pop_back();
    
    //
    // We're done replaying the bypasses. See what we ended up with.
    //
    
    if (value.m_state != RST_FULL)
    {
        // The register is not full
        if (value.m_state == RST_INVALID)
        {
            // Drop what we're doing -- we need to try again next cycle.
            // This is when the most recent result will actually be generated
            // later in the pipeline (e.g., a memory load is picked up before
            // the Memory Stage).
            return false;
        }

        // Wait on it
        operand.addr.index += operand.offset;
        operand.value = RegToPipeValue(operand.addr.type, value);
            
        // Pretend we've read everything since the entire operand
        // will now be waiting on the wanted register.
        operand.offset = -1;
    }
    else
    {
        // Insert the read data into the operand
        unsigned int offset = operand.offset;
#ifdef ARCH_BIG_ENDIAN
        offset = operand.value.m_size / sizeof(Integer) - 1 - offset;
#endif
        const unsigned int shift = offset * sizeof(Integer) * 8;
        switch (operand.addr.type)
        {
        case RT_INTEGER:
            operand.value.m_integer.set(
                operand.value.m_integer.get(operand.value.m_size) | (value.m_integer << shift),
                operand.value.m_size);
            break;
            
        case RT_FLOAT:
            operand.value.m_float.fromint(
                operand.value.m_float.toint(operand.value.m_size) | (value.m_float.integer << shift),
                operand.value.m_size);
            break;
        }
            
        // Proceed to the next register in the operand
        operand.offset--;
    }
    return true;
}

/*
 Checks if the operand is FULL and if not, writes the output (Rav) to suspend on the missing register.
 @param [in]  operand The operand to check
 @param [in]  addr    The base address of the operand
 */
bool Pipeline::ReadStage::CheckOperandForSuspension(const OperandInfo& operand, const RegAddr& addr)
{
    if (operand.value.m_state != RST_FULL)
    {
        COMMIT
        {
            // Register wasn't full, write back the suspend information
            
            // Put the output value in the waiting state
            m_output.Rc                 = operand.addr;
            m_output.shared.offset      = -1;
            m_output.Rav                = operand.value;
            m_output.Rav.m_state        = RST_WAITING;
            m_output.Rav.m_waiting.head = m_input.tid;
            m_output.Rav.m_waiting.tail = (operand.value.m_state == RST_WAITING)
                ? operand.value.m_waiting.tail     // The register was already waiting, append thread to list
                : m_input.tid;                     // First thread waiting on the register
        }
        return true;
    }
    return false;
}

Pipeline::PipeAction Pipeline::ReadStage::OnCycle()
{
    OperandInfo operand1( m_operand1 );
    OperandInfo operand2( m_operand2 );

    if (operand1.offset == -2 && operand2.offset == -2)
    {
        // This is a new read; initialize stuff
        
        // Initialize the operand data
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
                // Stores on the Sparc require at least two cycles. First cycle we
                // read the value to store. After that the two registers for
                // the address.
                m_isMemoryStore = true;
                if (m_rsv.m_state == RST_INVALID)
                {
                    // Store value hasn't been read yet, so read it first
                    operand1.addr         = m_input.Rs;
                    operand1.value.m_size = m_input.RsSize;
                    operand2.addr         = INVALID_REG;
                }
                break;
            }
        }
#endif
    }

    //
    // Read the registers for both operands.
    // This reads the values into operand<n>.value_reg
    //
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
    
    if (!IsAcquiring())
    {
        //
        // Now read the bypasses and construct the final value.
        //
        // We don't call this in the acquire phase because the bypasses will have
        // wrong values which could mess things up.
        //
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
    }
    
    if (operand1.offset >= 0 || operand2.offset >= 0)
    {
        // Both operands haven't been fully read yet -- delay
        COMMIT
        {
            m_operand1 = operand1;
            m_operand2 = operand2;
        }
        return PIPE_DELAY;
    }
    
    //
    // Both operands are now fully read
    //
    COMMIT
    {
        // Copy common latch data
        (CommonData&)m_output          = m_input;
        (ArchDecodeReadLatch&)m_output = m_input;
        
        m_output.Ra = operand1.addr;
        m_output.Rb = operand2.addr;
        
        // We're done with these operands -- reset for new operands next cycle
        m_operand1.offset = -2;
        m_operand2.offset = -2;
    }

    if (!CheckOperandForSuspension(operand1, m_input.Ra))  // Suspending on operand #1?
    if (!CheckOperandForSuspension(operand2, m_input.Rb))  // Suspending on operand #2?
    {
        // Not suspending, output the normal stuff
        COMMIT
        {
            m_output.Rc     = m_input.Rc;
            m_output.regofs = m_input.regofs;
            m_output.shared = m_input.shared;
            m_output.RcSize = m_input.RcSize;
            m_output.Rav    = operand1.value;
            m_output.Rbv    = operand2.value;
            m_output.regs   = m_input.regs;
            m_output.regofs = m_input.regofs;
            m_output.place  = m_input.place;
        }
        
#if TARGET_ARCH == ARCH_SPARC
        // On the Sparc, memory stores take longer because three registers
        // need to be read. We do this by first reading the value to store
        // and then the two address registers in the next cycle.
        if (m_isMemoryStore)
        {
            if (m_rsv.m_state != RST_FULL)
            {
                // First phase of the store has completed,
                // copy the read value.
                COMMIT{ m_rsv = operand1.value; }
                
                // We need to delay this cycle
                return PIPE_DELAY;
            }
        
            COMMIT
            {
                // Final cycle of the store
                m_output.Rsv  = m_rsv;
                m_rsv.m_state = RST_INVALID;
            }
        }
#endif
    }

    return PIPE_CONTINUE;
}

void Pipeline::ReadStage::Clear(TID tid)
{
    if (m_input.tid == tid)
    {
        m_operand1.offset = -2;
        m_operand2.offset = -2;
    }
}

Pipeline::ReadStage::ReadStage(Pipeline& parent, Clock& clock, const DecodeReadLatch& input, ReadExecuteLatch& output, RegisterFile& regFile,
    const vector<BypassInfo>& bypasses,
    const Config& /*config*/
  )
  : Stage("read", parent, clock),
    m_regFile(regFile),
    m_input(input),
    m_output(output),
    m_bypasses(bypasses)
{
#if TARGET_ARCH == ARCH_SPARC
    m_isMemoryStore = false;
    m_rsv.m_state = RST_INVALID;
#endif
    m_operand1.port = &m_regFile.p_pipelineR1;
    m_operand2.port = &m_regFile.p_pipelineR2;
    Clear(input.tid);
}

}
