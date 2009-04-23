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
            RemoteRequest m_remote;     ///< Remote request information for shareds and globals.
        };
    };
};

static inline PipeValue MAKE_EMPTY_PIPEVALUE(unsigned int size)
{
    PipeValue value;
    value.m_state          = RST_EMPTY;
    value.m_size           = size;
    value.m_waiting.head   = INVALID_TID;
    value.m_memory.size    = 0;
    value.m_remote.reg.fid = INVALID_LFID;
    return value;
}

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
    };

    struct ArchReadExecuteLatch : public ArchDecodeReadLatch
    {
        PipeValue storeValue;
    };
#endif

class Processor;
class Allocator;
class DCache;
class FamilyTable;
class ThreadTable;
class RegisterFile;
class Network;
class FPU;

class Pipeline : public IComponent
{
    /// Return code from the various pipeline stages, indicates the action for the pipeline.
    enum PipeAction
    {
        PIPE_CONTINUE,  ///< Stage completed succesfully, continue.
        PIPE_FLUSH,     ///< Stage completed, but the rest of the thread must be flushed.
        PIPE_STALL,     ///< Stage cannot complete, stall pipeline.
        PIPE_DELAY,     ///< Stage completed, but must be run again; delay rest of the pipeline.
		PIPE_IDLE,      ///< Stage has no work.
    };
    
    /// Type of thread suspension
    enum SuspendType
    {
        SUSPEND_NONE,           ///< Don't suspend
        SUSPEND_MEMORY_BARRIER, ///< Memory barrier
        SUSPEND_MISSING_DATA,   ///< We're missing data
    };

public:
	struct Config
	{
		size_t controlBlockSize;
	};

    //
    // Common latch data
    //
    struct LatchState
    {
        bool empty;
       
        LatchState() : empty(true) {}
    };
    
    struct Latch
    {
        TID     tid;
        MemAddr pc;
        LFID    fid;
        bool    swch;
        bool    kill;
    };

    class Stage : public IComponent
    {
    public:
        virtual PipeAction  read()  = 0;
        virtual PipeAction  write() = 0;
        virtual void        clear(TID /* tid */) {}
        Stage(Pipeline& parent, const std::string& name, Latch* input, Latch* output);

        Latch* getInput()  const { return m_input;  }
        Latch* getOutput() const { return m_output; }

    protected:
        Pipeline& m_parent;

    private:
        Latch* m_input;
        Latch* m_output;
    };

    //
    // Latches
    //
    struct RegInfo
    {
        struct
        {
            Family::RegInfo family;
            Thread::RegInfo thread;
        } types[NUM_REG_TYPES];
    };
    
    struct FetchDecodeLatch : public Latch, public LatchState
    {
        LFID            link_prev;
        LFID            link_next;
        Instruction     instr;
        RegInfo         regs;
		bool            onParent;
        bool            isLastThreadInBlock;
		bool            isFirstThreadInFamily;
        bool            isLastThreadInFamily;
    };

    struct DecodeReadLatch : public Latch, public LatchState, public ArchDecodeReadLatch
    {
        uint32_t        literal;
        RegInfo         regs;
        
        // Registers addresses, types and sizes
        RemoteRegAddr   Rra, Rrb, Rrc;
        RegAddr         Ra,  Rb,  Rc;
        unsigned int    RaSize, RbSize, RcSize;
    };

    struct ReadExecuteLatch : public Latch, public LatchState, public ArchReadExecuteLatch
    {
        // Registers addresses, values and types
        RegAddr         Rc;
        PipeValue       Rav, Rbv;
        PipeValue       Rcv; // Used for m_size only
        RemoteRegAddr   Rrc;
        RegInfo         regs;

        // For debugging only
        RegAddr         Ra, Rb;
    };

    struct ExecuteMemoryLatch : public Latch, public LatchState
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

    struct MemoryWritebackLatch : public Latch, public LatchState
    {
        SuspendType suspend;

        RemoteRegAddr   Rrc;
        RegAddr         Rc;
        PipeValue       Rcv;
    };

    //
    // Stages
    //
    class FetchStage : public Stage
    {
    public:
        PipeAction read();
        PipeAction write();
        FetchStage(Pipeline& parent, FetchDecodeLatch& fdlatch, Allocator& allocator, FamilyTable& familyTable, ThreadTable& threadTable, ICache &icache, LPID lpid, size_t controlBlockSize);
        ~FetchStage();

        void clear(TID tid);
        LFID getFID() const { return m_fid; }
        TID getTID() const { return m_tid; }
        MemAddr getPC() const { return m_pc; }
    
    private:
        FetchDecodeLatch&   m_output;
        Allocator&          m_allocator;
        FamilyTable&        m_familyTable;
        ThreadTable&        m_threadTable;
        ICache&             m_icache;
        LPID                m_lpid;

        // Information of current executing thread
        char*           m_buffer;
        int             m_controlBlockSize;
        bool            m_isLastThreadInBlock;
        bool            m_isLastThreadInFamily;
		bool            m_isFirstThreadInFamily;
        bool            m_switched;
		bool            m_onParent;
        LFID            m_fid;
        LFID            m_link_prev;
        LFID            m_link_next;
        TID             m_tid;
		TID             m_next;
		bool            m_legacy;
        MemAddr         m_pc;
        RegInfo         m_regs;
    };

    class DecodeStage : public Stage
    {
    public:
        PipeAction read();
        PipeAction write();
        DecodeStage(Pipeline& parent, FetchDecodeLatch& input, DecodeReadLatch& output);
    
    private:
        RegAddr TranslateRegister(uint8_t reg, RegType type, unsigned int size, RemoteRegAddr* remoteReg) const;
        void    DecodeInstruction(const Instruction& instr);

        FetchDecodeLatch& m_input;
        DecodeReadLatch&  m_output;
    };

    class ReadStage : public Stage
    {
    public:
        PipeAction read();
        PipeAction write();
        ReadStage(Pipeline& parent, DecodeReadLatch& input, ReadExecuteLatch& output, RegisterFile& regFile, Network& network, ExecuteMemoryLatch& bypass1, MemoryWritebackLatch& bypass2);
    
    private:
        struct OperandInfo
        {
            DedicatedReadPort* port;            ///< Port on the RegFile to use for reading this operand
            RegAddr            addr;            ///< (Base) address of the operand            
            PipeValue          value;           ///< Final value
            int                to_read_mask;    ///< Sub-register of the operand we still need to read
            
            // Address and value as read from the register file
            // The PipeValue actually contains a RegValue, but this way the code can remain generic
            RegAddr            addr_reg;        ///< Address of the value read from register
            PipeValue          value_reg;       ///< Value as read from the register file
        };
        
        bool ReadRegister(OperandInfo& operand, uint32_t literal);
        bool ReadBypasses(OperandInfo& operand);
        void clear(TID tid);

#if TARGET_ARCH == ARCH_SPARC
        // Sparc memory stores require three registers so takes two cycles.
        // First cycle calculates the address and stores it here.
        bool      m_isMemoryStore;
        PipeValue m_storeValue;
#endif

        RegisterFile&           m_regFile;
        Network&                m_network;
        DecodeReadLatch&        m_input;
        ReadExecuteLatch&       m_output;
        ExecuteMemoryLatch&     m_bypass1;
        MemoryWritebackLatch&   m_bypass2;
        
        // Copy of the Writeback latch, because the actual latch will be gone
        // when we need it.
        MemoryWritebackLatch    m_wblatch;
        
        OperandInfo             m_operand1, m_operand2;
    };

    class ExecuteStage : public Stage
    {
    public:
        PipeAction read();
        PipeAction write();
        ExecuteStage(Pipeline& parent, ReadExecuteLatch& input, ExecuteMemoryLatch& output, Allocator& allocator, ThreadTable& threadTable, FamilyTable& familyTable, FPU& fpu);
        
        uint64_t getFlop() const { return m_flop; }
        uint64_t getOp()   const { return m_op; }
    
    private:
        ReadExecuteLatch&       m_input;
        ExecuteMemoryLatch&     m_output;
        Allocator&              m_allocator;
        ThreadTable&            m_threadTable;
        FamilyTable&            m_familyTable;
		FPU&                    m_fpu;
        uint64_t                m_flop;         // FP operations
        uint64_t                m_op;           // Instructions
        
        enum FamilyProperty {
            FAMPROP_START,
            FAMPROP_LIMIT,
            FAMPROP_STEP,
            FAMPROP_BLOCK,
            FAMPROP_PLACE,
        };
        
        bool       MemoryWriteBarrier(TID tid) const;
        PipeAction SetFamilyProperty(LFID fid, FamilyProperty property, uint64_t value);
        PipeAction SetFamilyRegs(LFID fid, const Allocator::RegisterBases bases[]);
        PipeAction ExecuteInstruction();
        PipeAction ExecCreate(LFID fid, MemAddr address, RegAddr exitCodeReg);
        PipeAction ExecBreak(Integer value);
        PipeAction ExecBreak(double value);
        PipeAction ExecKill(LFID fid);
        void       ExecDebug(Integer value, Integer stream) const;
        void       ExecDebug(double value, Integer stream) const;
    };

    class MemoryStage : public Stage
    {
    public:
        PipeAction read();
        PipeAction write();
        MemoryStage(Pipeline& parent, ExecuteMemoryLatch& input, MemoryWritebackLatch& output, DCache& dcache, Allocator& allocator);
    
    private:
        ExecuteMemoryLatch&     m_input;
        MemoryWritebackLatch&   m_output;
        Allocator&              m_allocator;
        DCache&                 m_dcache;
    };

    class WritebackStage : public Stage
    {
    public:
        PipeAction read();
        PipeAction write();
        WritebackStage(Pipeline& parent, MemoryWritebackLatch& input, RegisterFile& regFile, Network& network, Allocator& allocator, ThreadTable& threadTable);
    
    private:
        MemoryWritebackLatch&   m_input;
        RegisterFile&           m_regFile;
        Network&                m_network;
        Allocator&              m_allocator;
        ThreadTable&            m_threadTable;

        // These fields are for multiple-cycle writebacks
        RegIndex                m_writebackOffset;
        uint64_t                m_writebackValue;
        RegSize                 m_writebackSize;
    };

    Pipeline(Processor& parent, const std::string& name, LPID lpid, RegisterFile& regFile, Network& network, Allocator& allocator, FamilyTable& familyTable, ThreadTable& threadTable, ICache& icache, DCache& dcache, FPU& fpu, const Config& config);

    Result OnCycleReadPhase(unsigned int stateIndex);
    Result OnCycleWritePhase(unsigned int stateIndex);
    void   UpdateStatistics();

    const Stage& GetStage(int i) const { return *m_stages[i]; }
    Processor&   GetProcessor()  const { return m_parent; }
    
    uint64_t GetMaxIdleTime() const { return m_maxPipelineIdleTime; }
    uint64_t GetMinIdleTime() const { return m_minPipelineIdleTime; }
    uint64_t GetAvgIdleTime() const { return m_totalPipelineIdleTime / std::max<uint64_t>(1ULL, m_pipelineIdleEvents); }
    
    float    GetEfficiency() const { return (float)m_nStagesRun / NUM_STAGES / std::max<uint64_t>(1ULL, m_pipelineBusyTime); }

    uint64_t GetFlop() const { return m_execute.getFlop(); }
    uint64_t GetOp()   const { return m_execute.getOp(); }

private:
    Processor&    m_parent;
    RegisterFile& m_regFile;
    
    FetchDecodeLatch     m_fdLatch;
    DecodeReadLatch      m_drLatch;
    ReadExecuteLatch     m_reLatch;
    ExecuteMemoryLatch   m_emLatch;
    MemoryWritebackLatch m_mwLatch;

    static const int    NUM_STAGES = 6;
    Stage*              m_stages[NUM_STAGES];
    LatchState*         m_latches[NUM_STAGES - 1];
    bool                m_runnable[NUM_STAGES];
    
    size_t   m_nStagesRunnable;
    size_t   m_nStagesRun;
    uint64_t m_maxPipelineIdleTime;
    uint64_t m_minPipelineIdleTime;
    uint64_t m_totalPipelineIdleTime;
    uint64_t m_pipelineIdleEvents;
    uint64_t m_pipelineIdleTime;
    uint64_t m_pipelineBusyTime;

public:
    FetchStage          m_fetch;
    DecodeStage         m_decode;
    ReadStage           m_read;
    ExecuteStage        m_execute;
    MemoryStage         m_memory;
    WritebackStage      m_writeback;
};

}
#endif

