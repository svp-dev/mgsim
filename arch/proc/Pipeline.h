#ifndef PIPELINE_H
#define PIPELINE_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class Pipeline : public Object, public Inspect::Interface<Inspect::Read>
{
    friend class Processor;
    
#include "ISA.h"

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
            };
        };
        PipeValue() : m_state(RST_INVALID), m_size(0) {}
        std::string str(RegType type) const;
    };

    static inline PipeValue MAKE_EMPTY_PIPEVALUE(unsigned int size)
    {
        PipeValue value;
        value.m_state        = RST_EMPTY;
        value.m_size         = size;
        value.m_waiting.head = INVALID_TID;
        value.m_memory.size  = 0;
        return value;
    }

    static inline PipeValue MAKE_PENDING_PIPEVALUE(unsigned int size)
    {
        PipeValue value;
        value.m_state        = RST_PENDING;
        value.m_size         = size;
        value.m_waiting.head = INVALID_TID;
        value.m_memory.size  = 0;
        return value;
    }

#if defined(TARGET_MTALPHA)
    struct ArchDecodeReadLatch
    {
        InstrFormat format;
        uint8_t     opcode;
        uint16_t    function;
        int32_t     displacement;

    ArchDecodeReadLatch() : format(IFORMAT_INVALID), opcode(0), function(0), displacement(0) {}
    };

    struct ArchReadExecuteLatch : public ArchDecodeReadLatch
    {
    };
#elif defined(TARGET_MTSPARC)
    struct ArchDecodeReadLatch
    {
        uint8_t  op1, op2, op3;
        uint16_t function;
        uint8_t  asi;
        int32_t  displacement;
        
        // Memory store data source
        RegAddr      Rs;
        unsigned int RsSize;

    ArchDecodeReadLatch() : op1(0), op2(0), op3(0), function(0), asi(0), displacement(0), RsSize(0) {}
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
    CommonData() : tid(0), pc(0), pc_dbg(0), fid(0), swch(false), kill(false) {}
    };

    struct Latch : public CommonData
    {
        bool empty;
        
        Latch() : empty(true) {}
    };

    struct FetchDecodeLatch : public Latch
    {
        Instruction instr;
        RegInfo     regs;
        bool        legacy;
        PSize       placeSize;

    FetchDecodeLatch() : instr(0), legacy(false), placeSize(0) {}
    };

    struct DecodeReadLatch : public Latch, public ArchDecodeReadLatch
    {
        uint32_t        literal;
        RegInfo         regs;
        
        // For [f]mov[gsd], the offset in the child family's register file
        unsigned char   regofs;

        // Registers addresses, types and sizes
        RegAddr         Ra,  Rb,  Rc;
        unsigned int    RaSize, RbSize, RcSize;
        bool            RaNotPending; // Ra is only used to check for Not Pending
        
        PSize           placeSize;

    DecodeReadLatch() : literal(0), regofs(0), RaSize(0), RbSize(0), RcSize(0), RaNotPending(false), placeSize(0) {}
    };

    struct ReadExecuteLatch : public Latch, public ArchReadExecuteLatch
    {
        // Registers addresses, values and types
        RegAddr         Rc;
        PipeValue       Rav, Rbv;
        unsigned int    RcSize;
        RegInfo         regs;

        // For [f]mov[gsd], the offset in the child family's register file
        unsigned char   regofs;
        
        PSize           placeSize;
        
        // For debugging only
        RegAddr         Ra, Rb;

    ReadExecuteLatch() : RcSize(0), regofs(0), placeSize(0) {}
    };

    struct ExecuteMemoryLatch : public Latch
    {
        SuspendType suspend;
        
        // Memory operation information
        MemAddr address;
        MemSize size;           // 0 when no memory operation
        bool    sign_extend;    // Sign extend sub-register loads?

        // To be written address and value
        RegAddr       Rc;
        PipeValue     Rcv;      // On loads, m_state = RST_INVALID and m_size is reg. size
        
        PSize         placeSize;
        
        RemoteMessage Rrc;

        // For debugging only
        RegAddr       Ra; // the origin of the value for a store

    ExecuteMemoryLatch() : suspend(SUSPEND_NONE), address(0), size(0), sign_extend(false), placeSize(0) {}
    };

    struct MemoryWritebackLatch : public Latch
    {
        SuspendType   suspend;
        RegAddr       Rc;
        PipeValue     Rcv;
        
        RemoteMessage Rrc;

    MemoryWritebackLatch() : suspend(SUSPEND_NONE) {}
    };
    
    //
    // Stages
    //
    class Stage : public Object
    {
    public:
        virtual PipeAction OnCycle() = 0;
        virtual void       Clear(TID /*tid*/) {}
        Stage(const std::string& name, Pipeline& parent, Clock& clock);

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
        size_t            m_controlBlockSize;
        char*             m_buffer;
        bool              m_switched;
        MemAddr           m_pc;

        void Clear(TID tid);    
        PipeAction OnCycle();
    public:
        FetchStage(Pipeline& parent, Clock& clock, FetchDecodeLatch& output, Allocator& allocator, FamilyTable& familyTable, ThreadTable& threadTable, ICache &icache, Config& config);
        ~FetchStage();
    };

    class DecodeStage : public Stage
    {
        const FetchDecodeLatch& m_input;
        DecodeReadLatch&        m_output;

        PipeAction OnCycle();
        RegAddr TranslateRegister(uint8_t reg, RegType type, unsigned int size, bool writing) const;
        void    DecodeInstruction(const Instruction& instr);

#if defined(TARGET_MTALPHA)
        static InstrFormat GetInstrFormat(uint8_t opcode);
#endif
    public:
        DecodeStage(Pipeline& parent, Clock& clock, const FetchDecodeLatch& input, DecodeReadLatch& output, Config& config);
    };

    class ReadStage : public Stage
    {
        struct OperandInfo
        {
            DedicatedReadPort* port;      ///< Port on the RegFile to use for reading this operand
            RegAddr            addr;      ///< (Base) address of the operand
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
        bool                        m_RaNotPending;
        
#if defined(TARGET_MTSPARC)
        // Sparc memory stores require three registers so takes two cycles.
        // First cycle calculates the address and stores it here.
        bool      m_isMemoryOp;
        PipeValue m_rsv;
#endif

        static PipeValue RegToPipeValue(RegType type, const RegValue& src_value);
    public:
        ReadStage(Pipeline& parent, Clock& clock, const DecodeReadLatch& input, ReadExecuteLatch& output, RegisterFile& regFile,
            const std::vector<BypassInfo>& bypasses,
            Config& config);
    };
    
    class ExecuteStage : public Stage
    {
        const ReadExecuteLatch& m_input;
        ExecuteMemoryLatch&     m_output;
        Allocator&              m_allocator;
        FamilyTable&            m_familyTable;
        ThreadTable&            m_threadTable;
		FPU&                    m_fpu;
		size_t                  m_fpuSource;    // Which input are we to the FPU?
        uint64_t                m_flop;         // FP operations
        uint64_t                m_op;           // Instructions
        
        bool       MemoryWriteBarrier(TID tid) const;
        bool       MoveFamilyRegister(RemoteRegType kind, RegType type, const FID& fid, unsigned char ofs);
        bool       ExecSync(const FID& fid);
        bool       ExecDetach(const FID& fid);
        PipeAction SetFamilyProperty(const FID& fid, FamilyProperty property, Integer value);
        PipeAction ExecuteInstruction();
        bool       ExecAllocate(PlaceID place, RegIndex reg, bool suspend, bool exclusive, Integer flags);
        PipeAction ExecCreate(const FID& fid, MemAddr address, RegIndex completion);
        PipeAction ExecBreak();
        PipeAction ExecKill(const PlaceID& place);
        void       ExecDebug(Integer value, Integer stream) const;
        void       ExecDebug(double value, Integer stream) const;
        PipeAction OnCycle();
        
        void       ExecStatusAction(Integer value, int command, int flags) const;
        void       ExecMemoryControl(Integer value, int command, int flags) const;
        void       ExecDebugOutput(Integer value, int command, int flags) const;

#if defined(TARGET_MTALPHA)
        static bool BranchTaken(uint8_t opcode, const PipeValue& value);
        static bool ExecuteINTA(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func);
        static bool ExecuteINTL(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func);
        static bool ExecuteINTS(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func);
        static bool ExecuteINTM(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func);
        static bool ExecuteFLTV(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func);
        static bool ExecuteFLTI(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func);
        static bool ExecuteFLTL(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func);
        static bool ExecuteITFP(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func);
        static bool ExecuteFPTI(PipeValue& Rcv, const PipeValue& Rav, const PipeValue& Rbv, int func);
#elif defined(TARGET_MTSPARC)
        static bool BranchTakenInt(int cond, uint32_t psr);
        static bool BranchTakenFlt(int cond, uint32_t fsr);
        static uint32_t ExecBasicInteger(int opcode, uint32_t Rav, uint32_t Rbv, uint32_t& Y, PSR& psr);
        static uint32_t ExecOtherInteger(int opcode, uint32_t Rav, uint32_t Rbv, uint32_t& Y, PSR& psr);
        PipeAction ExecReadASR20(uint8_t func);
        PipeAction ExecWriteASR20(uint8_t func);
#endif

        static RegValue PipeValueToRegValue(RegType type, const PipeValue& v);
    public:
        size_t GetFPUSource() const { return m_fpuSource; }
        
        ExecuteStage(Pipeline& parent, Clock& clock, const ReadExecuteLatch& input, ExecuteMemoryLatch& output, Allocator& allocator, FamilyTable& familyTable, ThreadTable& threadTable, FPU& fpu, size_t fpu_source, Config& config);
        
        uint64_t getFlop() const { return m_flop; }
        uint64_t getOp()   const { return m_op; }
    };

    class MemoryStage : public Stage
    {
        const ExecuteMemoryLatch& m_input;
        MemoryWritebackLatch&     m_output;
        Allocator&                m_allocator;
        DCache&                   m_dcache;
        uint64_t                  m_loads;         // nr of successful loads
        uint64_t                  m_stores;        // nr of successful stores
        uint64_t                  m_load_bytes;    // nr of successfully loaded bytes
        uint64_t                  m_store_bytes;   // nr of successfully stored bytes

        PipeAction OnCycle();
    public:
        MemoryStage(Pipeline& parent, Clock& clock, const ExecuteMemoryLatch& input, MemoryWritebackLatch& output, DCache& dcache, Allocator& allocator, Config& config);
        void addMemStatistics(uint64_t& nr, uint64_t& nw, uint64_t& nrb, uint64_t& nwb) const 
        { nr += m_loads; nw += m_stores; nrb += m_load_bytes; nwb += m_store_bytes; }
    };
    
    class DummyStage : public Stage
    {
        const MemoryWritebackLatch& m_input;
        MemoryWritebackLatch&       m_output;

        PipeAction OnCycle();
    public:
        DummyStage(const std::string& name, Pipeline& parent, Clock& clock, const MemoryWritebackLatch& input, MemoryWritebackLatch& output, Config& config);
    };

    class WritebackStage : public Stage
    {
        const MemoryWritebackLatch& m_input;
        bool                        m_stall;
        RegisterFile&               m_regFile;
        Allocator&                  m_allocator;
        ThreadTable&                m_threadTable;
        Network&                    m_network;
        int                         m_writebackOffset; // For multiple-cycle writebacks

        PipeAction OnCycle();
    public:
        WritebackStage(Pipeline& parent, Clock& clock, const MemoryWritebackLatch& input, RegisterFile& regFile, Allocator& allocator, ThreadTable& threadTable, Network& network, Config& config);
    };

    void PrintLatchCommon(std::ostream& out, const CommonData& latch) const;
    static std::string MakePipeValue(const RegType& type, const PipeValue& value);
    
public:
    Pipeline(const std::string& name, Processor& parent, Clock& clock, RegisterFile& regFile, Network& network, Allocator& allocator, FamilyTable& familyTable, ThreadTable& threadTable, ICache& icache, DCache& dcache, FPU& fpu, Config& config);
    ~Pipeline();

    Result DoPipeline();

    Processor& GetProcessor()  const { return m_parent; }
    
    uint64_t GetTotalBusyTime() const { return m_pipelineBusyTime; }
    uint64_t GetNStages() const { return m_stages.size(); }
    uint64_t GetStagesRun() const { return m_nStagesRun; }
    
    size_t GetFPUSource() const { return dynamic_cast<ExecuteStage&>(*m_stages[3].stage).GetFPUSource(); }

    float    GetEfficiency() const { return (float)m_nStagesRun / m_stages.size() / (float)std::max<uint64_t>(1ULL, m_pipelineBusyTime); }

    uint64_t GetFlop() const { return dynamic_cast<ExecuteStage&>(*m_stages[3].stage).getFlop(); }
    uint64_t GetOp()   const { return dynamic_cast<ExecuteStage&>(*m_stages[3].stage).getOp(); }
    void     CollectMemOpStatistics(uint64_t& nr, uint64_t& nw, uint64_t& nrb, uint64_t& nwb) const 
    { return dynamic_cast<MemoryStage&>(*m_stages[4].stage).addMemStatistics(nr, nw, nrb, nwb); }
    
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

    // Processes
    Process p_Pipeline;
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
    uint64_t m_pipelineBusyTime;   
};

#endif

