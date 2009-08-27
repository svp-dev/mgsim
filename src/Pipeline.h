#ifndef PIPELINE_H
#define PIPELINE_H

#include "FamilyTable.h"
#include "ThreadTable.h"
#include "Allocator.h"
#include "ICache.h"

#if TARGET_ARCH == ARCH_ALPHA
#include "ISA.alpha.h"
#elif TARGET_ARCH == ARCH_SPARC
#include "ISA.sparc.h"
#endif

namespace Simulator
{

class Processor;
class Allocator;
class DCache;
class FamilyTable;
class ThreadTable;
class RegisterFile;
class Network;
class FPU;

/// A (possibly multi-) register value in the pipeline
struct PipeValue
{
    RegState     m_state;   ///< State of the register.
    unsigned int m_size;    ///< Size of the value, in bytes
    union
    {
        MultiFloat   m_float;    ///< Value of the register, if it is an FP register.
        MultiInteger m_integer;  ///< Value of the register, if it is an integer register.

        struct
        {
            ThreadQueue   m_waiting;    ///< List of the threads that are waiting on the register.
            MemoryRequest m_memory;     ///< Memory request information for pending registers.
            RemoteRegAddr m_remote;     ///< Remote request information for shareds and globals.
        };
    };
};

static inline PipeValue MAKE_EMPTY_PIPEVALUE(unsigned int size)
{
    PipeValue value;
    value.m_state        = RST_EMPTY;
    value.m_size         = size;
    value.m_waiting.head = INVALID_TID;
    value.m_memory.size  = 0;
    value.m_remote.fid   = INVALID_LFID;
    return value;
}

static inline PipeValue MAKE_PENDING_PIPEVALUE(unsigned int size)
{
    PipeValue value;
    value.m_state        = RST_PENDING;
    value.m_size         = size;
    value.m_waiting.head = INVALID_TID;
    value.m_memory.size  = 0;
    value.m_remote.fid   = INVALID_LFID;
    return value;
}

class Pipeline : public IComponent
{
#if TARGET_ARCH == ARCH_ALPHA
    struct ArchDecodeReadLatch
    {
        InstrFormat format;
        uint8_t     opcode;
        uint16_t    function;
        int32_t     displacement;
    };

    struct ArchReadExecuteLatch : public ArchDecodeReadLatch
    {
    };
#elif TARGET_ARCH == ARCH_SPARC
    struct ArchDecodeReadLatch
    {
        uint8_t  op1, op2, op3;
        uint16_t function;
        uint8_t  asi;
        int32_t  displacement;
        
        // Memory store data source
        RemoteRegAddr Rrs;
        RegAddr       Rs;
        unsigned int  RsSize;
    };

    struct ArchReadExecuteLatch : public ArchDecodeReadLatch
    {
        PipeValue Rsv;
    };
#endif

    /// Return code from the various pipeline stages, indicates the action for the pipeline.
    enum PipeAction
    {
        PIPE_CONTINUE,  ///< Stage completed succesfully, continue.
        PIPE_FLUSH,     ///< Stage completed, but the rest of the thread must be flushed.
        PIPE_STALL,     ///< Stage cannot complete, stall pipeline.
        PIPE_DELAY,     ///< Stage completed, but must be run again; delay rest of the pipeline.
        PIPE_IDLE,      ///< Stage has nothing to do
    };
    
    /// Type of thread suspension
    enum SuspendType
    {
        SUSPEND_NONE,           ///< Don't suspend
        SUSPEND_MEMORY_BARRIER, ///< Memory barrier
        SUSPEND_MISSING_DATA,   ///< We're missing data
    };

    struct RegInfo
    {
        struct
        {
            Family::RegInfo family;
            Thread::RegInfo thread;
        } types[NUM_REG_TYPES];
    };
    
    //
    // Latches
    //
    struct BypassInfo
    {
        const bool*      empty;
        const RegAddr*   addr;
        const PipeValue* value;
        
        BypassInfo(const bool& empty, const RegAddr& addr, const PipeValue& value)
            : empty(&empty), addr(&addr), value(&value) {}
    };
    
    struct CommonData
    {
        TID     tid;
        MemAddr pc;
        MemAddr pc_dbg; // Original, unmodified PC for debugging
        LFID    fid;
        bool    swch;
        bool    kill;
    };

    struct Latch : public CommonData
    {
        bool empty;
        
        Latch() : empty(true) {}
    };

    struct FetchDecodeLatch : public Latch
    {
        LFID            link_prev;
        LFID            link_next;
        Instruction     instr;
        RegInfo         regs;
        bool            legacy;
		bool            onParent;
		GPID            parent_gpid;
		LPID            parent_lpid;
		LFID            parent_fid;
        bool            isLastThreadInBlock;
		bool            isFirstThreadInFamily;
        bool            isLastThreadInFamily;
    };

    struct DecodeReadLatch : public Latch, public ArchDecodeReadLatch
    {
        uint32_t        literal;
        RegInfo         regs;
        
        // Registers addresses, types and sizes
        RemoteRegAddr   Rra, Rrb, Rrc;
        RegAddr         Ra,  Rb,  Rc;
        unsigned int    RaSize, RbSize, RcSize;
    };

    struct ReadExecuteLatch : public Latch, public ArchReadExecuteLatch
    {
        // Registers addresses, values and types
        RemoteRegAddr   Rra, Rrb, Rrc;
        RegAddr         Rc;
        PipeValue       Rav, Rbv;
        PipeValue       Rcv; // Used for m_size only
        RegInfo         regs;

        // For debugging only
        RegAddr         Ra, Rb;
    };

    struct ExecuteMemoryLatch : public Latch
    {
        SuspendType suspend;
        
        // Memory operation information
        MemAddr address;
        MemSize size;           // 0 when no memory operation
        bool    sign_extend;    // Sign extend sub-register loads?

        // To be written address and value
        RemoteRegAddr   Rrc;
        RegAddr         Rc;
        PipeValue       Rcv;    // On loads, m_state = RST_INVALID and m_size is reg. size
    };

    struct MemoryWritebackLatch : public Latch
    {
        SuspendType suspend;

        RemoteRegAddr   Rrc;
        RegAddr         Rc;
        PipeValue       Rcv;
    };
    
    //
    // Stages
    //
    class Stage : public Object
    {
    public:
        virtual PipeAction OnCycle() = 0;
        virtual void       Clear(TID /*tid*/) {}
        Stage(Pipeline& parent, const std::string& name);

    protected:
        Pipeline& m_parent;
    };

    class FetchStage : public Stage
    {
        FetchDecodeLatch& m_output;
        Allocator&        m_allocator;
        FamilyTable&      m_familyTable;
        ThreadTable&      m_threadTable;
        ICache&           m_icache;
        LPID              m_lpid;
        size_t            m_controlBlockSize;
        char*             m_buffer;
        bool              m_switched;
        MemAddr           m_pc;

        void Clear(TID tid);    
        PipeAction OnCycle();
    public:
        FetchStage(Pipeline& parent, FetchDecodeLatch& output, Allocator& allocator, FamilyTable& familyTable, ThreadTable& threadTable, ICache &icache, LPID lpid, const Config& config);
        ~FetchStage();
    };

    class DecodeStage : public Stage
    {
        const FetchDecodeLatch& m_input;
        DecodeReadLatch&        m_output;

        PipeAction OnCycle();
        RegAddr TranslateRegister(uint8_t reg, RegType type, unsigned int size, RemoteRegAddr* remoteReg, bool writing) const;
        void    DecodeInstruction(const Instruction& instr);

    public:
        DecodeStage(Pipeline& parent, const FetchDecodeLatch& input, DecodeReadLatch& output, const Config& config);
    };

    class ReadStage : public Stage
    {
        struct OperandInfo
        {
            DedicatedReadPort* port;      ///< Port on the RegFile to use for reading this operand
            RegAddr            addr;      ///< (Base) address of the operand
            RemoteRegAddr      remote;    ///< (Base) remote address of the operand
            PipeValue          value;     ///< Final value
            int                offset;    ///< Sub-register of the operand we are currently reading
            
            // Address and value as read from the register file
            // The PipeValue actually contains a RegValue, but this way the code can remain generic
            RegAddr            addr_reg;  ///< Address of the value read from register
            PipeValue          value_reg; ///< Value as read from the register file
        };
        
        bool ReadRegister(OperandInfo& operand, uint32_t literal);
        bool ReadBypasses(OperandInfo& operand);
        bool CheckOperandForSuspension(const OperandInfo& operand, const RegAddr& addr);
        void Clear(TID tid);
        PipeAction OnCycle();

        RegisterFile&               m_regFile;
        const DecodeReadLatch&      m_input;
        ReadExecuteLatch&           m_output;
        std::vector<BypassInfo>     m_bypasses;
        OperandInfo                 m_operand1, m_operand2;
        
#if TARGET_ARCH == ARCH_SPARC
        // Sparc memory stores require three registers so takes two cycles.
        // First cycle calculates the address and stores it here.
        bool      m_isMemoryStore;
        PipeValue m_rsv;
#endif

    public:
        ReadStage(Pipeline& parent, const DecodeReadLatch& input, ReadExecuteLatch& output, RegisterFile& regFile,
            const std::vector<BypassInfo>& bypasses,
            const Config& config);
    };
    
    class ExecuteStage : public Stage
    {
        const ReadExecuteLatch& m_input;
        ExecuteMemoryLatch&     m_output;
        Allocator&              m_allocator;
        Network&                m_network;
        ThreadTable&            m_threadTable;
		FPU&                    m_fpu;
		size_t                  m_fpuSource;    // Which input are we to the FPU?
        uint64_t                m_flop;         // FP operations
        uint64_t                m_op;           // Instructions
        
        enum FamilyProperty {
            FAMPROP_START,
            FAMPROP_LIMIT,
            FAMPROP_STEP,
            FAMPROP_BLOCK,
        };
        
        bool       MemoryWriteBarrier(TID tid) const;
        PipeAction SetFamilyProperty(LFID fid, FamilyProperty property, uint64_t value);
        PipeAction ExecuteInstruction();
        PipeAction ExecCreate(LFID fid, MemAddr address, RegAddr exitCodeReg);
        PipeAction ExecBreak(Integer value);
        PipeAction ExecBreak(double value);
        PipeAction ExecKill(LFID fid);
        void       ExecDebug(Integer value, Integer stream) const;
        void       ExecDebug(double value, Integer stream) const;
        PipeAction OnCycle();
        
    public:
        ExecuteStage(Pipeline& parent, const ReadExecuteLatch& input, ExecuteMemoryLatch& output, Allocator& allocator, Network& network, ThreadTable& threadTable, FPU& fpu, size_t fpu_source, const Config& config);
        
        uint64_t getFlop() const { return m_flop; }
        uint64_t getOp()   const { return m_op; }
    };

    class MemoryStage : public Stage
    {
        const ExecuteMemoryLatch& m_input;
        MemoryWritebackLatch&     m_output;
        Allocator&                m_allocator;
        DCache&                   m_dcache;

        PipeAction OnCycle();
    public:
        MemoryStage(Pipeline& parent, const ExecuteMemoryLatch& input, MemoryWritebackLatch& output, DCache& dcache, Allocator& allocator, const Config& config);
    };
    
    class DummyStage : public Stage
    {
        const MemoryWritebackLatch& m_input;
        MemoryWritebackLatch&       m_output;

        PipeAction OnCycle();
    public:
        DummyStage(Pipeline& parent, const std::string& name, const MemoryWritebackLatch& input, MemoryWritebackLatch& output, const Config& config);
    };

    class WritebackStage : public Stage
    {
        const MemoryWritebackLatch& m_input;
        bool                        m_stall;
        RegisterFile&               m_regFile;
        Network&                    m_network;
        Allocator&                  m_allocator;
        ThreadTable&                m_threadTable;
        int                         m_writebackOffset; // For multiple-cycle writebacks

        PipeAction OnCycle();
    public:
        WritebackStage(Pipeline& parent, const MemoryWritebackLatch& input, RegisterFile& regFile, Network& network, Allocator& allocator, ThreadTable& threadTable, const Config& config);
    };

    static void PrintLatchCommon(std::ostream& out, const CommonData& latch);
    static std::string MakePipeValue(const RegType& type, const PipeValue& value);
    
public:
    Pipeline(Processor& parent, const std::string& name, LPID lpid, RegisterFile& regFile, Network& network, Allocator& allocator, FamilyTable& familyTable, ThreadTable& threadTable, ICache& icache, DCache& dcache, FPU& fpu, const Config& config);
    ~Pipeline();

    Result OnCycle(unsigned int stateIndex);
    void   UpdateStatistics();

    Processor& GetProcessor()  const { return m_parent; }
    
    uint64_t GetMaxIdleTime() const { return m_maxPipelineIdleTime; }
    uint64_t GetMinIdleTime() const { return m_minPipelineIdleTime; }
    uint64_t GetAvgIdleTime() const { return m_totalPipelineIdleTime / std::max<uint64_t>(1ULL, m_pipelineIdleEvents); }
    
    float    GetEfficiency() const { return (float)m_nStagesRun / m_stages.size() / (float)std::max<uint64_t>(1ULL, m_pipelineBusyTime); }

    uint64_t GetFlop() const { return dynamic_cast<ExecuteStage&>(*m_stages[3].stage).getFlop(); }
    uint64_t GetOp()   const { return dynamic_cast<ExecuteStage&>(*m_stages[3].stage).getOp(); }
    
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    struct StageInfo
    {
        Latch* input;
        Stage* stage;
        Latch* output;
        Result status;
    };
    
    Processor& m_parent;
    
    FetchDecodeLatch                  m_fdLatch;
    DecodeReadLatch                   m_drLatch;
    ReadExecuteLatch                  m_reLatch;
    ExecuteMemoryLatch                m_emLatch;
    MemoryWritebackLatch              m_mwLatch;
    std::vector<MemoryWritebackLatch> m_dummyLatches;
    MemoryWritebackLatch              m_mwBypass;

    std::vector<StageInfo> m_stages;
    
    Register<bool> m_active;
    
    size_t   m_nStagesRunnable;
    size_t   m_nStagesRun;
    uint64_t m_maxPipelineIdleTime;
    uint64_t m_minPipelineIdleTime;
    uint64_t m_totalPipelineIdleTime;
    uint64_t m_pipelineIdleEvents;
    uint64_t m_pipelineIdleTime;
    uint64_t m_pipelineBusyTime;
};

}
#endif

