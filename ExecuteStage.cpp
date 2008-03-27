#include <cassert>
#include "Pipeline.h"
#include "ISA.h"
#include "Processor.h"
using namespace Simulator;
using namespace std;

Pipeline::PipeAction Pipeline::ExecuteStage::read()
{
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::ExecuteStage::write()
{
    ExecuteMemoryLatch output;
    output.isLastThreadInFamily  = m_input.isLastThreadInFamily;
	output.isFirstThreadInFamily = m_input.isFirstThreadInFamily;
    output.fid     = m_input.fid;
    output.tid     = m_input.tid;
    output.Rrc     = m_input.Rrc;
    output.swch    = m_input.swch;
    output.kill    = m_input.kill;
    output.address = 0;
    output.size    = 0;
    output.Rc      = m_input.Rc;
    output.Rcv.m_state = RST_INVALID;

    MemAddr pc   = m_input.pc + sizeof(Instruction);
    bool resched = true;

    // Check if both registers are available.
    // If not, we must write them back, because they actually contain the
    // suspend information.
    if (m_input.Rav.m_state != RST_FULL)
    {
        output.Rcv  = m_input.Rav;
        output.swch = true;
        output.kill = false;
        pc     -= sizeof(Instruction);
        resched = false;
    }
    else if (m_input.Rbv.m_state != RST_FULL)
    {
        output.Rcv  = m_input.Rbv;
        output.swch = true;
        output.kill = false;
        pc     -= sizeof(Instruction);
        resched = false;
    }
    else switch (m_input.format)
    {
    case IFORMAT_BRA:
		if (m_input.opcode == A_OP_CREATE_D)
		{
			// Direct create
			LFID    fid     = LFID((size_t)m_input.Rav.m_integer);
			MemAddr address = pc + m_input.displacement * sizeof(Instruction);
		    if (!m_allocator.queueCreate(fid, address, m_input.tid, m_input.Rc))
			{
				return PIPE_STALL;
			}
			output.Rcv.m_state     = RST_PENDING;
			output.Rcv.m_component = &m_allocator;
		}
        // Conditional and unconditional branches
        else if (branchTaken(m_input.opcode, m_input.Rav))
        {
            if (m_input.opcode == A_OP_BR || m_input.opcode == A_OP_BSR)
            {
                // Store the address of the next instruction for BR and BSR
                output.Rcv.m_integer = pc;
                output.Rcv.m_state   = RST_FULL;
            }
            pc += m_input.displacement * sizeof(Instruction);
			output.swch = true;
			output.kill = false;
        }
        break;

    case IFORMAT_JUMP:
		if (m_input.opcode == A_OP_CREATE_I)
		{
			// Indirect create
			LFID    fid     = LFID((size_t)m_input.Rav.m_integer);
			MemAddr address = m_input.Rbv.m_integer & -3;
		    if (!m_allocator.queueCreate(fid, address, m_input.tid, m_input.Rc))
			{
				return PIPE_STALL;
			}
			output.Rcv.m_state     = RST_PENDING;
			output.Rcv.m_component = &m_allocator;
		}
		else
		{
			// Jumps
			output.Rcv.m_integer = pc;
			output.Rcv.m_state   = RST_FULL;

			pc   = m_input.Rbv.m_integer & ~3;
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
                case A_OP_STQ_U: case A_OP_LDQ_U: output.address &= ~3;
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
		COMMIT
		(
			Family& family = m_allocator.GetWritableFamilyEntry(LFID((size_t)m_input.Rav.m_integer), m_input.tid);
            switch (m_input.opcode)
            {
			case A_OP_SETREGS:
				// Set the base for the shareds and globals in the parent thread
				RegIndex gr = (RegIndex)((m_input.literal >>  0) & 0x1F);
				RegIndex sr = (RegIndex)((m_input.literal >> 16) & 0x1F);
				RegIndex gf = (RegIndex)((m_input.literal >> 32) & 0x1F);
				RegIndex sf = (RegIndex)((m_input.literal >> 48) & 0x1F);
				if (gr != 31) family.regs[RT_INTEGER].globals = m_input.threadRegs[RT_INTEGER].base + gr;
				if (sr != 31) family.regs[RT_INTEGER].shareds = m_input.threadRegs[RT_INTEGER].base + sr;
				if (gf != 31) family.regs[RT_FLOAT].globals   = m_input.threadRegs[RT_FLOAT].base + gf;
				if (sf != 31) family.regs[RT_FLOAT].shareds   = m_input.threadRegs[RT_FLOAT].base + sf;
				break;
			}
		)
		break;

    case IFORMAT_OP:
        COMMIT
        (
			bool (*execfunc)(RegValue&, const RegValue&, const RegValue&, int) = NULL;

            switch (m_input.opcode)
            {
                case A_OP_UTHREAD:
				{
					switch (m_input.function)
					{
						case A_UTHREAD_ALLOCATE: {
							LFID fid;
							Result res = m_allocator.AllocateFamily(m_input.tid, output.Rc.index, &fid);
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
							case A_UTHREAD_SETLIMIT: family.end           = m_input.Rbv.m_integer; break;
							case A_UTHREAD_SETSTEP:  family.step          = m_input.Rbv.m_integer; break;
							case A_UTHREAD_SETBLOCK: family.virtBlockSize = (TSize)m_input.Rbv.m_integer; break;
							case A_UTHREAD_SETPLACE: family.gfid          = (m_input.Rbv.m_integer == 0) ? INVALID_GFID : 0; break;
							}
							break;
						}

						case A_UTHREAD_BREAK:    break;
						case A_UTHREAD_KILL:     break;
						case A_UTHREAD_SQUEEZE:  break;

						case A_UTHREAD_DEBUG:
							DebugProgWrite("DEBUG by T%u at %016llx: %016llx\n", m_input.tid, m_input.pc, m_input.Rbv.m_integer);
							output.Rc = INVALID_REG;
							break;
					}
                    break;
				}

                case A_OP_UTHREADF:
				{
					switch (m_input.function)
					{
						case A_UTHREADF_DEBUG:
							DebugProgWrite("DEBUG by T%u at %016llx: %.12lf\n", m_input.tid, m_input.pc, m_input.Rav.m_float);
							output.Rc = INVALID_REG;
							break;
							
						case A_UTHREADF_GETINVPROCS:
							output.Rcv.m_state = RST_FULL;
							output.Rcv.m_float.fromdouble(1.0 / m_parent.m_parent.getNumProcs());
							break;
					}
                    break;
				}

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
			
			if (execfunc != NULL)
			{
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
                    COMMIT( m_flop++; )
				}
			}
        )
        break;
    
        default: break;
    }

    if (output.swch)
    {
        // We've switched threads
        Thread& thread = m_threadTable[m_input.tid];
        if (!m_icache.releaseCacheLine(thread.cid))
        {
            return PIPE_STALL;
        }

		if (!output.kill)
        {
            // We're not killing, update the thread table and possibly reschedule
            if (!resched)
            {
                // Suspend the thread
                assert(thread.state == TST_RUNNING);
                COMMIT
                (
                    thread.state = TST_SUSPENDED;
                    thread.cid   = INVALID_CID;
                    thread.pc    = pc;
                )
            }
            // Reschedule thread
            else if (!m_allocator.activateThread(m_input.tid, *this, &pc))
            {
                // We cannot reschedule, stall pipeline
                return PIPE_STALL;
            }
		}
        else if (!m_allocator.killThread(m_input.tid))
        {
            // Can't kill, stall!
            return PIPE_STALL;
        }

        if (output.kill || !resched)
        {
            COMMIT
            (
                Family& family = m_familyTable[m_input.fid];
                if (family.numThreadsQueued == 0)
                {
                    // Mark family as idle or killed
                    family.state = (family.allocationDone == 0 && family.nRunning == 0) ? FST_KILLED : FST_IDLE;
                }
            )
        }
    }

    if (resched)
    {
        // We've executed an instruction
        COMMIT( m_op++; )
    }

    COMMIT( m_output = output; )
    return (output.swch) ? PIPE_FLUSH : PIPE_CONTINUE;
}

Pipeline::ExecuteStage::ExecuteStage(Pipeline& parent, ReadExecuteLatch& input, ExecuteMemoryLatch& output, Allocator& alloc, FamilyTable& familyTable, ThreadTable& threadTable, ICache& icache, FPU& fpu)
  : Stage(parent, "execute", &input, &output),
    m_input(input),
    m_output(output),
    m_allocator(alloc),
    m_familyTable(familyTable),
    m_threadTable(threadTable),
    m_icache(icache),
	m_fpu(fpu)
{
    m_flop = 0;
    m_op   = 0;
}
