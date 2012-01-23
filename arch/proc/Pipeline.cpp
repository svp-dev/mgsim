#include "Processor.h"
#include "FPU.h"
#include "sim/config.h"
#include "symtable.h"
#include "sim/sampling.h"

#include <limits>
#include <cassert>
#include <iomanip>
#include <sstream>
using namespace std;

namespace Simulator
{

Processor::Pipeline::Stage::Stage(const std::string& name, Pipeline& parent, Clock& clock)
:   Object(name, parent, clock),
    m_parent(parent)
{
}

Processor::Pipeline::Pipeline(
    const std::string&  name,
    Processor&          parent,
    Clock&              clock,
    RegisterFile&       regFile,
    Network&            network,
    Allocator&          alloc,
    FamilyTable&        familyTable,
    ThreadTable&        threadTable,
    ICache&             icache,
    DCache&             dcache,
    FPU&                fpu,
    Config&       config)
:
    Object(name, parent, clock),
    p_Pipeline(*this, "pipeline", delegate::create<Pipeline, &Processor::Pipeline::DoPipeline>(*this)),
    m_parent(parent),
    
    m_active("f_active", *this, clock),
    
    m_nStagesRunnable(0), m_nStagesRun(0),
    m_pipelineBusyTime(0)
{
    static const size_t NUM_FIXED_STAGES = 6;

    m_active.Sensitive(p_Pipeline);
    
    // Number of forwarding delay slots between the Memory and Writeback stage
    const size_t num_dummy_stages = config.getValue<size_t>(*this, "NumDummyStages");
    
    m_stages.resize( num_dummy_stages + NUM_FIXED_STAGES );

    RegisterSampleVariableInObject(m_nStagesRunnable, SVC_LEVEL, m_stages.size());
    RegisterSampleVariableInObject(m_nStagesRun, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_pipelineBusyTime, SVC_CUMULATIVE);
    

    // Create the Fetch stage
    m_stages[0].stage  = new FetchStage(*this, clock, m_fdLatch, alloc, familyTable, threadTable, icache, config);
    m_stages[0].input  = NULL;
    m_stages[0].output = &m_fdLatch;

    // Create the Decode stage
    m_stages[1].stage  = new DecodeStage(*this, clock, m_fdLatch, m_drLatch, config);
    m_stages[1].input  = &m_fdLatch;
    m_stages[1].output = &m_drLatch;

    // Construct the Read stage later, after all bypasses have been created
    m_stages[2].input  = &m_drLatch;
    m_stages[2].output = &m_reLatch;
    std::vector<BypassInfo> bypasses;

    // Create the Execute stage
    size_t fpu_client_id = fpu.RegisterSource(regFile, alloc.m_readyThreads2);
    m_stages[3].stage  = new ExecuteStage(*this, clock, m_reLatch, m_emLatch, alloc, familyTable, threadTable, fpu, fpu_client_id, config);
    m_stages[3].input  = &m_reLatch;
    m_stages[3].output = &m_emLatch;
    bypasses.push_back(BypassInfo(m_emLatch.empty, m_emLatch.Rc, m_emLatch.Rcv));

    // Create the Memory stage
    m_stages[4].stage  = new MemoryStage(*this, clock, m_emLatch, m_mwLatch, dcache, alloc, config);
    m_stages[4].input  = &m_emLatch;
    m_stages[4].output = &m_mwLatch;
    bypasses.push_back(BypassInfo(m_mwLatch.empty, m_mwLatch.Rc, m_mwLatch.Rcv));

    // Create the dummy stages   
    MemoryWritebackLatch* last_output = &m_mwLatch;    
    m_dummyLatches.resize(num_dummy_stages);
    for (size_t i = 0; i < num_dummy_stages; ++i)
    {
        const size_t j = i + NUM_FIXED_STAGES - 1;
        StageInfo& si = m_stages[j];
        
        MemoryWritebackLatch& output = m_dummyLatches[i];
        bypasses.push_back(BypassInfo(output.empty, output.Rc, output.Rcv));

        stringstream name;
        name << "dummy" << i;
        si.input  = last_output;
        si.output = &output;
        si.stage  = new DummyStage(name.str(), *this, clock, *last_output, output, config);
        
        last_output = &output;
    }
    
    // Create the Writeback stage
    m_stages.back().stage  = new WritebackStage(*this, clock, *last_output, regFile, alloc, threadTable, network, config);
    m_stages.back().input  = m_stages[m_stages.size() - 2].output;
    m_stages.back().output = NULL;
    bypasses.push_back(BypassInfo(m_mwBypass.empty, m_mwBypass.Rc, m_mwBypass.Rcv));

    m_stages[2].stage = new ReadStage(*this, clock, m_drLatch, m_reLatch, regFile, bypasses, config);
}

Processor::Pipeline::~Pipeline()
{
    for (vector<StageInfo>::const_iterator p = m_stages.begin(); p != m_stages.end(); ++p)
    {
        delete p->stage;
    }
}

Result Processor::Pipeline::DoPipeline()
{
    if (IsAcquiring())
    {
        // Begin of the cycle, initialize
        for (vector<StageInfo>::iterator p = m_stages.begin(); p != m_stages.end(); ++p)
        {
            p->status = (p->input != NULL && p->input->empty ? DELAYED : SUCCESS);
        }
    
        /*
         Make a copy of the WB latch before doing anything. This will be used as
         the source for the bypass to the Read Stage. This can be justified by
         noting that the stages *should* happen in parallel, so the read stage
         will read the WB latch before it's been updated.
        */
        m_mwBypass = m_dummyLatches.empty() ? m_mwLatch : m_dummyLatches.back();
    }
    
    Result result = FAILED;
    m_nStagesRunnable = 0;

    vector<StageInfo>::reverse_iterator stage;
    try
    {
    for (stage = m_stages.rbegin(); stage != m_stages.rend(); ++stage)
    {
        if (stage->status == FAILED) 
        {
            // The pipeline stalled at this point
            break;
        }
        
        if (stage->status == SUCCESS)
        {
            m_nStagesRunnable++;

            const PipeAction action = stage->stage->OnCycle();
            if (!IsAcquiring())
            {
                // If this stage has stalled or is delayed, abort pipeline.
                // Note that the stages before this one in the pipeline
                // will never get executed now.
                if (action == PIPE_STALL)
                {
                    stage->status = FAILED;
                    DeadlockWrite("%s stage stalled", stage->stage->GetName().c_str());
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
                    if (action == PIPE_FLUSH && stage->input != NULL)
                    {
                        // Clear all previous stages with the same TID
                        const TID tid = stage->input->tid;
                        for (vector<StageInfo>::reverse_iterator f = stage + 1; f != m_stages.rend(); ++f)
                        {
                            if (f->input != NULL && f->input->tid == tid)
                            {
                                f->input->empty = true;
                                f->status = DELAYED;
                            }
                            f->stage->Clear(tid);
                        }
                    }
                    
                    COMMIT
                    {
                        // Clear input and set output
                        if (stage->input  != NULL) stage->input ->empty = true;
                        if (stage->output != NULL) stage->output->empty = false;
                    }
                    result = SUCCESS;
                }
            }
            else
            {
                result = SUCCESS;
            }
        }
    }
    }
    catch (SimulationException& e)
    {
        if (stage->input != NULL)
        {
            // Add details about thread, family and PC
            stringstream details;
            details << "While executing instruction at " << GetKernel()->GetSymbolTable()[stage->input->pc_dbg] 
                        // "0x" << setw(sizeof(MemAddr) * 2) << setfill('0') << hex << stage->input->pc_dbg
                    << " in T" << dec << stage->input->tid << " in F" << stage->input->fid;
            e.AddDetails(details.str());
        }
        throw;
    }
    
    if (m_nStagesRunnable == 0) {
        // Nothing to do anymore
        m_active.Clear();
        return SUCCESS;
    }
    
    COMMIT
    {
        m_nStagesRun += m_nStagesRunnable;
        m_pipelineBusyTime++;
    }
    
    m_active.Write(true);
    return result;
}

void Processor::Pipeline::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The pipeline reads instructions, loads operands, computes their results and/or\n"
    "dispatches asynchronous operations such as memory loads or FPU operations and\n"
    "finally writes back the result.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Reads and displays the stages and latches.\n";
}

void Processor::Pipeline::PrintLatchCommon(std::ostream& out, const CommonData& latch) const
{
    out << " | LFID: F"  << dec << latch.fid
        << "    TID: T"  << dec << latch.tid << right
        << "    PC: "    << GetKernel()->GetSymbolTable()[latch.pc_dbg] // "0x" << hex << setw(sizeof(MemAddr) * 2) << setfill('0') << latch.pc
        << "    Annotation: " << ((latch.kill) ? "End" : (latch.swch ? "Switch" : "None")) << endl
        << " |" << endl;
}

// Construct a string representation of a pipeline register value
/*static*/ std::string Processor::Pipeline::MakePipeValue(const RegType& type, const PipeValue& value)
{
    std::stringstream ss;

    switch (value.m_state)
    {
        case RST_INVALID: ss << "N/A";     break;
        case RST_PENDING: ss << "Pending"; break;
        case RST_EMPTY:   ss << "Empty";   break;
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

void Processor::Pipeline::Cmd_Read(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    // Fetch stage
    out << "Stage: fetch" << endl;
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
    out << "Stage: decode" << endl;
    if (m_drLatch.empty)
    {
        out << " | (Empty)" << endl;
    }
    else
    {
        PrintLatchCommon(out, m_drLatch);
        out  << hex << setfill('0')
#if defined(TARGET_MTALPHA)
             << " | Opcode:       0x" << setw(2) << (unsigned)m_drLatch.opcode 
#elif defined(TARGET_MTSPARC)
             << " | Op1:          0x" << setw(2) << (unsigned)m_drLatch.op1
             << "   Op2: 0x" << setw(2) << (unsigned)m_drLatch.op2
             << "   Op3: 0x" << setw(2) << (unsigned)m_drLatch.op3
#endif
             << "         Function: 0x" << setw(4) << m_drLatch.function << endl
             << " | Displacement: 0x" << setw(8) << m_drLatch.displacement
             << "   Literal:  0x" << setw(8) << m_drLatch.literal << endl
             << dec
             << " | Ra:           " << m_drLatch.Ra << "/" << m_drLatch.RaSize << endl
             << " | Rb:           " << m_drLatch.Rb << "/" << m_drLatch.RbSize << endl
             << " | Rc:           " << m_drLatch.Rc << "/" << m_drLatch.RcSize << endl
#if defined(TARGET_MTSPARC)
             << " | Rs:           " << m_drLatch.Rs << "/" << m_drLatch.RsSize << endl
#endif
            ;
    }
    out << " v" << endl;

    // Read stage
    out << "Stage: read" << endl;
    if (m_reLatch.empty)
    {
        out << " | (Empty)" << endl;
    }
    else
    {
        PrintLatchCommon(out, m_reLatch);
        out  << hex << setfill('0')
#if defined(TARGET_MTALPHA)
             << " | Opcode:       0x" << setw(2) << (unsigned)m_reLatch.opcode
             << "         Function:     0x" << setw(4) << m_reLatch.function << endl
#elif defined(TARGET_MTSPARC)
             << " | Op1:          0x" << setw(2) << (unsigned)m_reLatch.op1
             << "   Op2: 0x" << setw(2) << (unsigned)m_reLatch.op2 
             << "   Op3: 0x" << setw(2) << (unsigned)m_reLatch.op3
             << "         Function:     0x" << setw(4) << m_reLatch.function << endl
#endif
             << " | Displacement: 0x" << setw(8) << m_reLatch.displacement << endl
             << " | Rav:          " << MakePipeValue(m_reLatch.Ra.type, m_reLatch.Rav) << "/" << m_reLatch.Rav.m_size << endl
             << " | Rbv:          " << MakePipeValue(m_reLatch.Rb.type, m_reLatch.Rbv) << "/" << m_reLatch.Rbv.m_size << endl
             << " | Rc:           " << m_reLatch.Rc << "/" << m_reLatch.RcSize << endl;
    }
    out << " v" << endl;

    // Execute stage
    out << "Stage: execute" << endl;
    if (m_emLatch.empty)
    {
        out << " | (Empty)" << endl;
    }
    else
    {
        PrintLatchCommon(out, m_emLatch);
        out << " | Rc:        " << m_emLatch.Rc << "/" << m_emLatch.Rcv.m_size << endl
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
                << " | Address:   0x" << hex << setw(sizeof(MemAddr) * 2) << setfill('0') << m_emLatch.address 
                << " " << GetKernel()->GetSymbolTable()[m_emLatch.address] << endl
                << " | Size:      " << dec << m_emLatch.size << " bytes" << endl;
        }
    }
    out << " v" << endl;

    // Memory stage
    out << "Stage: memory" << endl;
    
    const MemoryWritebackLatch* latch = &m_mwLatch;
    for (size_t i = 0; i <= m_dummyLatches.size(); ++i)
    {
        if (latch->empty)
        {
            out << " | (Empty)" << endl;
        }
        else
        {
            PrintLatchCommon(out, *latch);
            out << " | Rc:  " << latch->Rc << "/" << latch->Rcv.m_size << endl
                << " | Rcv: " << MakePipeValue(latch->Rc.type, latch->Rcv) << endl;
        }
        out << " v" << endl;
        
        if (i < m_dummyLatches.size())
        {
            out << "Stage: extra" << endl
                << " |" << endl;
            latch = &m_dummyLatches[i];
        }
    }
    
    // Writeback stage
    out << "Stage: writeback" << endl;
}

string Processor::Pipeline::PipeValue::str(RegType type) const
{
    // Code similar to RegValue::str()
    ostringstream ss;
    string tc = (type == RT_FLOAT) ? "float:" : "int:" ;

    switch (m_state)
    {
    case RST_INVALID: return tc + "INVALID"; 
    case RST_EMPTY:   return tc + "[E]"; 
    case RST_PENDING: return tc + "[P:" + m_memory.str() + "]"; 
    case RST_WAITING: return tc + "[W:" + m_memory.str() + "," + m_waiting.str() + "]";
    case RST_FULL: {
        stringstream ss;
        ss << tc << "[F:" << setw(sizeof(Integer) * 2) << setfill('0') << hex;
        if (type == RT_FLOAT)
            ss << m_float.toint(m_size);
        else
            ss << m_integer.get(m_size);
        ss << ']';
        return ss.str();
    }
    default:
        assert(0); // can't be here
        return "UNKNOWN";
    }
}


}
