#include <cassert>
#include <iostream>
#include <iomanip>
#include "Pipeline.h"
#include "ISA.h"
#include "Processor.h"
using namespace Simulator;
using namespace std;

bool Pipeline::ExecuteStage::MemoryWriteBarrier(TID tid) const
{
    Thread& thread = m_threadTable[tid];
    if (thread.dependencies.numPendingWrites != 0)
    {
        // There are pending writes, we need to wait for them
        assert(!thread.waitingForWrites);
        COMMIT{ thread.waitingForWrites = true; }
        return false;
    }
    return true;
}

Pipeline::PipeAction Pipeline::ExecuteStage::read()
{
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::ExecuteStage::write()
{
    ExecuteMemoryLatch output;
    
    // Copy common latch data
    (CommonLatch&)output = m_input;

    output.Rrc     = m_input.Rrc;
    output.address = 0;
    output.size    = 0;
    output.Rc      = m_input.Rc;
    output.suspend = false;
    output.Rcv.m_state = RST_INVALID;

    // Check if both registers are available.
    // If not, we must write them back, because they actually contain the
    // suspend information.
    if (m_input.Rav.m_state != RST_FULL)
    {
        COMMIT
        {
            m_output = output;
            m_output.Rcv     = m_input.Rav;
            m_output.swch    = true;
            m_output.suspend = true;
            m_output.kill    = false;
        }
        return PIPE_FLUSH;
    }
    
    if (m_input.Rbv.m_state != RST_FULL)
    {
        COMMIT
        {
            m_output = output;
            m_output.Rcv     = m_input.Rbv;
            m_output.swch    = true;
            m_output.suspend = true;
            m_output.kill    = false;
        }
        return PIPE_FLUSH;
    }
    
    // Adjust PC to point to next instruction
    output.pc += sizeof(Instruction);
    
    switch (m_input.format)
    {
    case IFORMAT_BRA:
		if (m_input.opcode == A_OP_CREATE_D)
		{
			// Direct create
			if (!MemoryWriteBarrier(m_input.tid))
			{
			    // Suspend thread at our PC
			    output.pc      = m_input.pc;
			    output.suspend = true;
    			output.swch    = true;
    			output.kill    = false;
    			output.Rc      = INVALID_REG;
			}
			else
			{
			    LFID    fid     = LFID((size_t)m_input.Rav.m_integer);
    			MemAddr address = output.pc + m_input.displacement * sizeof(Instruction);
    		    if (!m_allocator.queueCreate(fid, address, m_input.tid, m_input.Rc))
    			{
    				return PIPE_STALL;
    			}
	    		output.Rcv.m_state     = RST_PENDING;
		    	output.Rcv.m_component = &m_allocator;
		    }
		}
        // Conditional and unconditional branches
        else if (branchTaken(m_input.opcode, m_input.Rav))
        {
            if (m_input.opcode == A_OP_BR || m_input.opcode == A_OP_BSR)
            {
                // Store the address of the next instruction for BR and BSR
                output.Rcv.m_integer = output.pc;
                output.Rcv.m_state   = RST_FULL;
            }
            output.pc  += m_input.displacement * sizeof(Instruction);
			output.swch = true;
			output.kill = false;
        }
        break;

    case IFORMAT_JUMP:
		if (m_input.opcode == A_OP_CREATE_I)
		{
			// Indirect create
			if (!MemoryWriteBarrier(m_input.tid))
			{
			    // Suspend thread at our PC
			    output.pc      = m_input.pc;
			    output.suspend = true;
    			output.swch    = true;
    			output.kill    = false;
    			output.Rc      = INVALID_REG;
			}
			else
			{
    			LFID    fid     = LFID((size_t)m_input.Rav.m_integer);
    			MemAddr address = m_input.Rbv.m_integer & -3;
    		    if (!m_allocator.queueCreate(fid, address, m_input.tid, m_input.Rc))
    			{
    				return PIPE_STALL;
    			}
    			output.Rcv.m_state     = RST_PENDING;
	    		output.Rcv.m_component = &m_allocator;
		    }
		}
		else
		{
			// Jumps
			output.Rcv.m_integer = output.pc;
			output.Rcv.m_state   = RST_FULL;

			output.pc   = m_input.Rbv.m_integer & ~3;
			output.swch = true;
			output.kill = false;
		}
        break;
        
    case IFORMAT_MEM_LOAD:
    case IFORMAT_MEM_STORE:
        // Create, LDA, LDAH and Memory reads and writes
        switch (m_input.opcode)
        {
        case A_OP_LDA:
        case A_OP_LDAH:
            output.Rcv.m_integer = m_input.Rbv.m_integer + m_input.displacement;
            output.Rcv.m_state   = RST_FULL;
            break;

        default:
            output.address = (MemAddr)(m_input.Rbv.m_integer + m_input.displacement);
            switch (m_input.opcode)
            {
                case A_OP_STB:   case A_OP_LDBU:  output.size = 1; break;
                case A_OP_STW:   case A_OP_LDWU:  output.size = 2; break;
				case A_OP_STS:	 case A_OP_LDS:
				case A_OP_STF:	 case A_OP_LDF:
                case A_OP_STL:   case A_OP_LDL:   output.size = 4; break;
                case A_OP_STQ_U: case A_OP_LDQ_U: output.address &= ~7;
				case A_OP_STT:	 case A_OP_LDT:
				case A_OP_STG:	 case A_OP_LDG:
                case A_OP_STQ:   case A_OP_LDQ:   output.size = 8; break;
            }

			if (output.size > 0)
			{
				if (m_input.format == IFORMAT_MEM_LOAD)
				{
					// We don't produce a value for loads
					output.Rcv.m_state = RST_INVALID;
				}
				else
				{
					// Put the value of Ra into Rcv for storage by memory stage
					output.Rcv = m_input.Rav;
				}
			}
            break;
        }
        break;

	case IFORMAT_SPECIAL:
        switch (m_input.opcode)
        {
			case A_OP_ALLOCATE: {
			    // Get the base for the shareds and globals in the parent thread
			    Allocator::RegisterBases bases[NUM_REG_TYPES];
			    
			    uint64_t literal = m_input.literal;
			    for (RegType i = 0; i < NUM_REG_TYPES; i++, literal >>= 10)
			    {
                    const RegIndex locals = m_input.threadRegs[i].base + m_input.familyRegs[i].count.shareds;
    		    	bases[i].globals = locals + ((literal >> 0) & 0x1F);
    	    		bases[i].shareds = locals + ((literal >> 5) & 0x1F);
       		    }

                // Allocate the entry
				LFID fid;
				Result res = m_allocator.AllocateFamily(m_input.tid, output.Rc.index, &fid, bases);
				if (res == FAILED)
				{
					return PIPE_STALL;
				}
					
				if (res == SUCCESS) {
					// The entry was allocated, store it
					output.Rcv.m_state   = RST_FULL;
					output.Rcv.m_integer = fid;
				} else {
					// The request was buffered and will be written back
					output.Rcv.m_state     = RST_PENDING;
					output.Rcv.m_component = &m_allocator;
				}
				break;
   			}
   			
   			case A_OP_SETREGS: {
                // Get the base for the shareds and globals in the parent thread
				Family& family = m_allocator.GetWritableFamilyEntry(LFID((size_t)m_input.Rav.m_integer), m_input.tid);

                uint64_t literal = m_input.literal;
                for (RegType i = 0; i < NUM_REG_TYPES; i++, literal >>= 10)
                {
                    const RegIndex locals = m_input.threadRegs[i].base + m_input.familyRegs[i].count.shareds;
                    family.regs[i].globals = locals + ((literal >> 0) & 0x1F);
                    family.regs[i].shareds = locals + ((literal >> 5) & 0x1F);
                }
   			    break;
   			}
		}
		break;

    case IFORMAT_OP:
    case IFORMAT_FPOP:
        COMMIT
        {
            if (m_input.opcode == A_OP_UTHREAD)
            {
				switch (m_input.function)
				{
					case A_UTHREAD_GETPROCS:
						output.Rcv.m_state   = RST_FULL;
						output.Rcv.m_integer = m_parent.m_parent.getNumProcs();
						break;

					case A_UTHREAD_SETSTART:
					case A_UTHREAD_SETLIMIT:
					case A_UTHREAD_SETSTEP:
					case A_UTHREAD_SETBLOCK:
					case A_UTHREAD_SETPLACE:
					{
						Family& family = m_allocator.GetWritableFamilyEntry(LFID((size_t)m_input.Rav.m_integer), m_input.tid);
						switch (m_input.function)
						{
						case A_UTHREAD_SETSTART: family.start         = m_input.Rbv.m_integer; break;
						case A_UTHREAD_SETLIMIT: family.limit         = m_input.Rbv.m_integer; break;
						case A_UTHREAD_SETSTEP:  family.step          = m_input.Rbv.m_integer; break;
						case A_UTHREAD_SETBLOCK: family.virtBlockSize = (TSize)m_input.Rbv.m_integer; break;
						case A_UTHREAD_SETPLACE: family.gfid          = (m_input.Rbv.m_integer == 0) ? INVALID_GFID : 0; break;
						}
						break;
					}

					case A_UTHREAD_BREAK:    break;
					case A_UTHREAD_KILL:     break;

					case A_UTHREAD_PRINT:
					{
					    unsigned int stream = m_input.Rbv.m_integer & 0x7F;
					    if (stream == 0) {
					        if (m_input.Rbv.m_integer & 0x80) {
    						    DebugProgWrite("PRINT by T%u at %016llx: %016lld\n", m_input.tid, m_input.pc, m_input.Rav.m_integer);
    						} else {
    						    DebugProgWrite("PRINT by T%u at %016llx: %016llu\n", m_input.tid, m_input.pc, m_input.Rav.m_integer);
    						}
    					} else {
	    				    ostream& out = (stream != 1) ? cerr : cout;
		    			    out << (char)m_input.Rav.m_integer;
		    			}
			    		output.Rc = INVALID_REG;
				        break;
				    }
				}
            }
            else if (m_input.opcode == A_OP_UTHREADF)
            {
				switch (m_input.function)
				{
					case A_UTHREADF_PRINT:
				    {
					    unsigned int stream = m_input.Rbv.m_integer & 0x7F;
					    if (stream == 0) {
    						DebugProgWrite("DEBUG by T%u at %016llx: %.12lf\n", m_input.tid, m_input.pc, m_input.Rav.m_float.todouble());
    				    } else {
	    				    ostream& out = (stream != 1) ? cerr : cout;
		    			    out << setprecision(12) << fixed << m_input.Rav.m_float.todouble();
    				    }
						output.Rc = INVALID_REG;
						break;
					}
					
					case A_UTHREADF_GETINVPROCS:
						output.Rcv.m_state = RST_FULL;
						output.Rcv.m_float.fromdouble(1.0 / m_parent.m_parent.getNumProcs());
						break;
				}
			}
            else
            {
    			bool (*execfunc)(RegValue&, const RegValue&, const RegValue&, int) = NULL;

                switch (m_input.opcode)
                {
                    case A_OP_INTA: execfunc = execINTA; break; 
                    case A_OP_INTL: execfunc = execINTL; break; 
                    case A_OP_INTS: execfunc = execINTS; break; 
                    case A_OP_INTM: execfunc = execINTM; break; 
                    case A_OP_FLTV: execfunc = execFLTV; break; 
    				case A_OP_ITFP: execfunc = execITFP; break;
                    case A_OP_FLTI: execfunc = execFLTI; break;
                    case A_OP_FLTL: execfunc = execFLTL; break;
    				case A_OP_FPTI: execfunc = execFPTI; break; 
                }
			
				if (!(*execfunc)(output.Rcv, m_input.Rav, m_input.Rbv, m_input.function))
				{
					// Dispatch long-latency operation to FPU
					if (!m_fpu.queueOperation(m_input.opcode, m_input.function, m_input.Rav.m_float, m_input.Rbv.m_float, output.Rc))
					{
						return PIPE_STALL;
					}
					output.Rcv.m_state     = RST_PENDING;
					output.Rcv.m_component = &m_fpu;
                    
                    // We've executed a floating point operation
                    m_flop++;
				}
			}
        }
        break;
        
    case IFORMAT_MISC:
        COMMIT
        {
            switch (m_input.function)
            {
            case A_MISCFUNC_MB:
            case A_MISCFUNC_WMB:
                // Memory barrier
                if (!MemoryWriteBarrier(m_input.tid))
                {
			        // Suspend thread at our PC
			        output.pc      = m_input.pc;
			        output.suspend = true;
    	    		output.swch    = true;
        			output.kill    = false;
        			output.Rc      = INVALID_REG;
                }
                break;
    
            case A_MISCFUNC_RPCC:
                // Read processor cycle count
                output.Rcv.m_state   = RST_FULL;
                output.Rcv.m_integer = getKernel()->getCycleNo() & 0xFFFFFFFF;
                break;
            }
        }        
        break;
    
        default: break;
    }

    COMMIT
    {
        m_output = output;

        // We've executed an instruction
        m_op++;
    }
    return (output.swch) ? PIPE_FLUSH : PIPE_CONTINUE;
}

Pipeline::ExecuteStage::ExecuteStage(Pipeline& parent, ReadExecuteLatch& input, ExecuteMemoryLatch& output, Allocator& alloc, ThreadTable& threadTable, FPU& fpu)
  : Stage(parent, "execute", &input, &output),
    m_input(input),
    m_output(output),
    m_allocator(alloc),
    m_threadTable(threadTable),
	m_fpu(fpu)
{
    m_flop = 0;
    m_op   = 0;
}
