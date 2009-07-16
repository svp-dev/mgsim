#include "Pipeline.h"
#include "Processor.h"
#include "FPU.h"
#include <limits>
#include <cassert>
#include <iomanip>
#include <sstream>
using namespace std;

namespace Simulator
{

Pipeline::Stage::Stage(Pipeline& parent, const std::string& name)
:   Object(&parent, parent.GetKernel(), name),
    m_parent(parent)
{
}

Pipeline::Pipeline(
    Processor&          parent,
    const std::string&  name,
    LPID                lpid,
    RegisterFile&       regFile,
    Network&            network,
    Allocator&          alloc,
    FamilyTable&        familyTable,
    ThreadTable&        threadTable,
    ICache&             icache,
    DCache&             dcache,
	FPU&                fpu,
    const Config&       config)
:
    IComponent(&parent, parent.GetKernel(), name),
    m_parent(parent),
    
    m_fetch    (*this,            m_fdLatch, alloc, familyTable, threadTable, icache, lpid, config),
    m_decode   (*this, m_fdLatch, m_drLatch, config),
    m_read     (*this, m_drLatch, m_reLatch, regFile, m_emLatch, m_mwLatch, m_mwBypass, config),
    m_execute  (*this, m_reLatch, m_emLatch, alloc, network, threadTable, fpu, fpu.RegisterSource(regFile), config),
    m_memory   (*this, m_emLatch, m_mwLatch, dcache, alloc, config),
    m_writeback(*this, m_mwLatch,            regFile, network, alloc, threadTable, config),
    
    m_active(parent.GetKernel(), *this, 0),

    m_maxPipelineIdleTime(0), m_minPipelineIdleTime(numeric_limits<uint64_t>::max()),
    m_totalPipelineIdleTime(0), m_pipelineIdleEvents(0), m_pipelineIdleTime(0), m_pipelineBusyTime(0)
{
    m_stages[0] = &m_fetch;
    m_stages[1] = &m_decode;
    m_stages[2] = &m_read;
    m_stages[3] = &m_execute;
    m_stages[4] = &m_memory;
    m_stages[5] = &m_writeback;
    
    m_latches[0] = &m_fdLatch;
    m_latches[1] = &m_drLatch;
    m_latches[2] = &m_reLatch;
    m_latches[3] = &m_emLatch;
    m_latches[4] = &m_mwLatch;
}

Result Pipeline::OnCycle(unsigned int /*stateIndex*/)
{
    static const char* StageNames[NUM_STAGES] = {
        "fetch", "decode", "read", "execute", "memory", "writeback"
    };
    
    if (IsAcquiring())
    {
        // Begin of the cycle, initialize
        m_runnable[0] = SUCCESS;
        for (int i = 0; i < NUM_STAGES - 1; ++i)
        {
            m_runnable[i + 1] = (m_latches[i]->empty ? DELAYED : SUCCESS);
        }
    
        /*
         Make a copy of the WB latch before doing anything. This will be used as
         the source for the bypass to the Read Stage. This can be justified by
         noting that the stages *should* happen in parallel, so the read stage
         will read the WB latch before it's been updated.
        */
        m_mwBypass = m_mwLatch;
    }
    
    Result result = FAILED;
    m_nStagesRunnable = 0;
    for (int stage = NUM_STAGES - 1; stage >= 0; --stage)
    {
        if (m_runnable[stage] == FAILED) 
        {
            // The pipeline stalled at this point
            break;
        }
        
        if (m_runnable[stage] == SUCCESS)
        try
        {
            m_nStagesRunnable++;

            const PipeAction action = m_stages[stage]->Write();
            if (!IsAcquiring())
            {
   	            // If this stage has stalled or is delayed, abort pipeline.
  	            // Note that the stages before this one in the pipeline
  	            // will never get executed now.
                if (action == PIPE_STALL)
                {
                    m_runnable[stage] = FAILED;
                    DeadlockWrite("%s stage stalled", StageNames[stage]);
                    break;
                }
                
                if (action == PIPE_DELAY)
                {
                    result = SUCCESS;
                    break;
                }
                
                if (action == PIPE_IDLE)
    		    {
    		        m_nStagesRunnable--;
    		    }
    		    else
    		    {
    		        if (action == PIPE_FLUSH && stage > 0)
   			        {
       		            // Clear all previous stages with the same TID
   		    	        const Latch* input = m_latches[stage - 1];
     					m_stages[0]->Clear(input->tid);
   				        for (int j = 0; j < stage - 1; ++j)
   				        {
   					        if (m_latches[j]->tid == input->tid)
  					        {
       					        m_latches[j]->empty = true;
   						        m_runnable[j + 1] = DELAYED;
   					        }
       					    m_stages[j + 1]->Clear(input->tid);
    			        }
  			        }
  			        
	    		    COMMIT
    			    {
    			        // Clear input and set output
                        if (stage > 0)              m_latches[stage-1]->empty = true;
                        if (stage < NUM_STAGES - 1) m_latches[stage  ]->empty = false;
    		        }
    		        result = SUCCESS;
    		    }
            }
            else
            {
                result = SUCCESS;
            }
        }
        catch (SimulationException& e)
        {
            if (stage > 0)
            {
                // Add details about thread, family and PC
                const Latch* input = m_latches[stage - 1];
                stringstream details;
                details << "While executing instruction at 0x" << setw(sizeof(MemAddr) * 2) << setfill('0') << hex << input->pc_dbg
                        << " in T" << dec << input->tid << " in F" << input->fid;
                e.AddDetails(details.str());
            }
            throw;
        }
    }
    
    if (m_nStagesRunnable == 0) {
        // Nothing to do anymore
        m_active.Clear();
        return SUCCESS;
    }
    
    COMMIT{ m_nStagesRun += m_nStagesRunnable; }
    
    m_active.Write(true);
    return result;
}

void Pipeline::UpdateStatistics()
{
    if (m_nStagesRunnable == 0)
    {
        m_pipelineIdleTime++;
    }
    else
    {
        m_pipelineBusyTime++;
        
        if (m_pipelineIdleTime > 0)
        {
            // Process this pipeline idle streak
            m_maxPipelineIdleTime    = max(m_maxPipelineIdleTime, m_pipelineIdleTime);
            m_minPipelineIdleTime    = min(m_minPipelineIdleTime, m_pipelineIdleTime);
            m_totalPipelineIdleTime += m_pipelineIdleTime;
            m_pipelineIdleEvents++;
            m_pipelineIdleTime = 0;
        }
    }
}

void Pipeline::Cmd_Help(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The pipeline reads instructions, loads operands, computes their results and/or\n"
    "dispatches asynchronous operations such as memory loads or FPU operations and\n"
    "finally writes back the result.\n\n"
    "Supported operations:\n"
    "- read <component>\n"
    "  Reads and displays the stages and latches.\n";
}

/*static*/ void Pipeline::PrintLatchCommon(std::ostream& out, const CommonData& latch)
{
    out << " | LFID: F"  << dec << latch.fid
        << "    TID: T"  << dec << latch.tid << right
        << "    PC: 0x" << hex << setw(sizeof(MemAddr) * 2) << setfill('0') << latch.pc
        << "    Annotation: " << ((latch.kill) ? "End" : (latch.swch ? "Switch" : "None")) << endl
        << " |" << endl;
}

// Construct a string representation of a pipeline register value
/*static*/ std::string Pipeline::MakePipeValue(const RegType& type, const PipeValue& value)
{
    std::stringstream ss;

    switch (value.m_state)
    {
        case RST_INVALID: ss << "N/A";   break;
        case RST_EMPTY:   ss << "Empty"; break;
        case RST_WAITING: ss << "Waiting (T" << dec << value.m_waiting.head << ")"; break;
        case RST_FULL:
            if (type == RT_INTEGER) {
                ss << "0x" << setw(value.m_size * 2)
                   << setfill('0') << hex << value.m_integer.get(value.m_size);
            } else {
                ss << setprecision(16) << fixed << value.m_float.tofloat(value.m_size);
            }
            break;
    }

    std::string ret = ss.str();
    if (ret.length() > 18) {
        ret = ret.substr(0,18);
    }
    return ret;
}

static std::ostream& operator << (std::ostream& out, const RemoteRegAddr& rreg) {
    if (rreg.fid != INVALID_LFID) {
        out << rreg.reg.str() << ", F" << dec << rreg.fid;
        if (rreg.pid != INVALID_GPID) {
            out << "@P" << rreg.pid;
        }
    } else {
        out << "N/A";
    }
    return out;
}

void Pipeline::Cmd_Read(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    // Fetch stage
    out << "Fetch stage" << endl
        << " |" << endl;
    if (m_fdLatch.empty)
    {
        out << " | (Empty)" << endl;
    }
    else
    {
        PrintLatchCommon(out, m_fdLatch);
        out << " | Instr: 0x" << hex << setw(sizeof(Instruction) * 2) << setfill('0') << m_fdLatch.instr << endl;
    }
    out << " v" << endl;

    // Decode stage
    out << "Decode stage" << endl
        << " |" << endl;
    if (m_drLatch.empty)
    {
        out << " | (Empty)" << endl;
    }
    else
    {
        PrintLatchCommon(out, m_drLatch);
#if TARGET_ARCH == ARCH_ALPHA
        out  << " | Opcode:       0x" << setw(2) << (unsigned)m_drLatch.opcode << endl
             << " | Function:     0x" << setw(4) << m_drLatch.function << endl
             << " | Displacement: 0x" << setw(8) << m_drLatch.displacement << endl
#elif TARGET_ARCH == ARCH_SPARC
        out  << " | Op1:          0x" << setw(2) << (unsigned)m_drLatch.op1
             << "    Op2: 0x" << setw(2) << (unsigned)m_drLatch.op2
             << "    Op3: 0x" << setw(2) << (unsigned)m_drLatch.op3 << endl
             << " | Function:     0x" << setw(4) << m_drLatch.function << endl
             << " | Displacement: 0x" << setw(8) << m_drLatch.displacement << endl
#endif
             << " | Literal:      0x" << setw(8) << m_drLatch.literal << endl
             << dec
             << " | Ra:           " << m_drLatch.Ra << "/" << m_drLatch.RaSize << "    Rra: " << m_drLatch.Rra << endl
             << " | Rb:           " << m_drLatch.Rb << "/" << m_drLatch.RbSize << "    Rrb: " << m_drLatch.Rrb << endl
             << " | Rc:           " << m_drLatch.Rc << "/" << m_drLatch.RcSize << "    Rrc: " << m_drLatch.Rrc << endl;
    }
    out << " v" << endl;

    // Read stage
    out << "Read stage" << endl
        << " |" << endl;
    if (m_reLatch.empty)
    {
        out << " | (Empty)" << endl;
    }
    else
    {
        PrintLatchCommon(out, m_reLatch);
        out  << hex << setfill('0')
#if TARGET_ARCH == ARCH_ALPHA
             << " | Opcode:       0x" << setw(2) << (unsigned)m_reLatch.opcode << endl
             << " | Function:     0x" << setw(4) << m_reLatch.function << endl
             << " | Displacement: 0x" << setw(8) << m_reLatch.displacement << endl
#elif TARGET_ARCH == ARCH_SPARC
             << " | Op1:          0x" << setw(2) << (unsigned)m_drLatch.op1
             << "    Op2: 0x" << setw(2) << (unsigned)m_drLatch.op2 
             << "    Op3: 0x" << setw(2) << (unsigned)m_drLatch.op3 << endl
             << " | Function:     0x" << setw(4) << m_reLatch.function << endl
             << " | Displacement: 0x" << setw(8) << m_reLatch.displacement << endl
#endif
             << " | Rav:          " << MakePipeValue(m_reLatch.Ra.type, m_reLatch.Rav) << "/" << m_reLatch.Rav.m_size << endl
             << " | Rbv:          " << MakePipeValue(m_reLatch.Rb.type, m_reLatch.Rbv) << "/" << m_reLatch.Rbv.m_size << endl
             << " | Rra:          " << m_reLatch.Rra << endl
             << " | Rrb:          " << m_reLatch.Rrb << endl
             << " | Rc:           " << m_reLatch.Rc << "/" << m_reLatch.Rcv.m_size << "    Rrc: " << m_reLatch.Rrc << endl;
    }
    out << " v" << endl;

    // Execute stage
    out << "Execute stage" << endl
        << " |" << endl;
    if (m_emLatch.empty)
    {
        out << " | (Empty)" << endl;
    }
    else
    {
        PrintLatchCommon(out, m_emLatch);
        out << " | Rc:        " << m_emLatch.Rc << "/" << m_emLatch.Rcv.m_size << "    Rrc: " << m_emLatch.Rrc << endl
            << " | Rcv:       " << MakePipeValue(m_emLatch.Rc.type, m_emLatch.Rcv) << endl;
        if (m_emLatch.size == 0)
        {
            // No memory operation
            out << " | Operation: N/A" << endl
                << " | Address:   N/A" << endl
                << " | Size:      N/A" << endl;
        }
        else
        {
            out << " | Operation: " << (m_emLatch.Rcv.m_state == RST_FULL ? "Store" : "Load") << endl
                << " | Address:   0x" << hex << setw(sizeof(MemAddr) * 2) << setfill('0') << m_emLatch.address << endl
                << " | Size:      " << dec << m_emLatch.size << " bytes" << endl;
        }
    }
    out << " v" << endl;

    // Memory stage
    out << "Memory stage" << endl
        << " |" << endl;
    if (m_mwLatch.empty)
    {
        out << " | (Empty)" << endl;
    }
    else
    {
        PrintLatchCommon(out, m_mwLatch);
        out << " | Rc:  " << m_mwLatch.Rc << "/" << m_mwLatch.Rcv.m_size << "    Rrc: " << m_mwLatch.Rrc << endl
            << " | Rcv: " << MakePipeValue(m_mwLatch.Rc.type, m_mwLatch.Rcv) << endl;
    }
    out << " v" << endl;

    // Writeback stage
    out << "Writeback stage" << endl;
}

}
