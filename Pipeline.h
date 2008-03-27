#ifndef PIPELINE_H
#define PIPELINE_H

#include "FamilyTable.h"
#include "ThreadTable.h"
#include "ICache.h"

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

class Pipeline : public IComponent
{
    enum PipeAction
    {
        PIPE_CONTINUE,
        PIPE_FLUSH,
        PIPE_STALL,
		PIPE_IDLE,
    };

public:
	struct Config
	{
		size_t controlBlockSize;
	};

    //
    // Base classes
    //
    class Latch
    {
    public:
        Latch() { clear(); }
        bool empty() const { return m_empty; }
        void clear()       { m_empty = true; }
        void set()         { m_empty = false; }

        LFID fid;
        TID  tid;
        bool swch;
        bool kill;
		bool isFirstThreadInFamily;
        bool isLastThreadInFamily;
    private:
        bool m_empty;
    };

    class Stage : public IComponent
    {
    public:
        virtual PipeAction  read()  = 0;
        virtual PipeAction  write() = 0;
        virtual void        clear(TID tid) {}
        Stage(Pipeline& parent, const std::string& name, Latch* input, Latch* output);

        Latch* getInput()  { return m_input;  }
        Latch* getOutput() { return m_output; }
        const Latch* getInput()  const { return m_input;  }
        const Latch* getOutput() const { return m_output; }

    protected:
        Pipeline& m_parent;

    private:
        Latch* m_input;
        Latch* m_output;
    };

    //
    // Latches
    //
    struct FetchDecodeLatch : public Latch
    {
        MemAddr         pc;
        GFID            gfid;
        Instruction     instr;
		FPCR            fpcr;
        Family::RegInfo familyRegs[NUM_REG_TYPES];
        Thread::RegInfo threadRegs[NUM_REG_TYPES];
		bool            onParent;
        bool            isLastThreadInBlock;
        bool            swch;
        bool            kill;
    };

    struct DecodeReadLatch : public Latch
    {
        MemAddr     pc;

        // Instruction misc
        InstrFormat format;
        uint8_t     opcode;
        uint16_t    function;
        int64_t     displacement;
        uint64_t    literal;
		FPCR        fpcr;

        // Registers addresses and types
        RemoteRegAddr   Rra, Rrb, Rrc;
        RegAddr         Ra, Rb, Rc;

        Family::RegInfo familyRegs[NUM_REG_TYPES];
        Thread::RegInfo threadRegs[NUM_REG_TYPES];
    };

    struct ReadExecuteLatch : public Latch
    {
        MemAddr     pc;

        // Instruction misc
        InstrFormat format;
        uint8_t     opcode;
        uint16_t    function;
        int64_t     displacement;
        uint64_t    literal;
		FPCR        fpcr;

        // Registers addresses, values and types
        RemoteRegAddr   Rrc;
        RegAddr         Rc;
        RegValue        Rav, Rbv;

        Family::RegInfo familyRegs[NUM_REG_TYPES];
        Thread::RegInfo threadRegs[NUM_REG_TYPES];
    };

    struct ExecuteMemoryLatch : public Latch
    {
        // Memory operation information
        MemAddr     address;
        MemSize     size;       // 0 means no memory operation

        // To be written address and value
        RemoteRegAddr   Rrc;
        RegAddr         Rc;
        RegValue        Rcv;
    };

    struct MemoryWritebackLatch : public Latch
    {
        RemoteRegAddr   Rrc;
        RegAddr         Rc;
        RegValue        Rcv;
    };

    //
    // Stages
    //
    class FetchStage : public Stage
    {
    public:
        PipeAction read();
        PipeAction write();
        FetchStage(Pipeline& parent, FetchDecodeLatch& fdlatch, Allocator& allocator, FamilyTable& familyTable, ThreadTable& threadTable, ICache &icache, size_t controlBlockSize);
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

        // Information of current executing thread
        char*           m_buffer;
        int             m_controlBlockSize;
        bool            m_isLastThreadInBlock;
        bool            m_isLastThreadInFamily;
		bool            m_isFirstThreadInFamily;
        bool            m_switched;
		bool            m_onParent;
        LFID            m_fid;
        GFID            m_gfid;
        TID             m_tid;
		TID             m_next;
        MemAddr         m_pc;
		FPCR            m_fpcr;
        Family::RegInfo m_familyRegs[NUM_REG_TYPES];
        Thread::RegInfo m_threadRegs[NUM_REG_TYPES];
    };

    class DecodeStage : public Stage
    {
    public:
        PipeAction read();
        PipeAction write();
        DecodeStage(Pipeline& parent, FetchDecodeLatch& input, DecodeReadLatch& output);
    
    private:
        static InstrFormat getInstrFormat(uint8_t opcode);
        RegAddr            translateRegister(uint8_t reg, RegType type, RemoteRegAddr* remoteReg) const;

        FetchDecodeLatch&   m_input;
        DecodeReadLatch&    m_output;
    };

    class ReadStage : public Stage
    {
    public:
        PipeAction read();
        PipeAction write();
        ReadStage(Pipeline& parent, DecodeReadLatch& input, ReadExecuteLatch& output, RegisterFile& regFile, Network& network, ExecuteMemoryLatch& bypass1, MemoryWritebackLatch& bypass2);
    
    private:
        bool readRegister(RegAddr reg, ReadPort& port, RegValue& val, bool& RvFromExec);

        RegisterFile&           m_regFile;
        Network&                m_network;
        DecodeReadLatch&        m_input;
        ReadExecuteLatch&       m_output;
        ExecuteMemoryLatch&     m_bypass1;
        MemoryWritebackLatch&   m_bypass2;

        // The read values
        RegValue m_rav, m_rbv;
        bool m_ravFromExec, m_rbvFromExec;
    };

    class ExecuteStage : public Stage
    {
    public:
        PipeAction read();
        PipeAction write();
        ExecuteStage(Pipeline& parent, ReadExecuteLatch& input, ExecuteMemoryLatch& output, Allocator& allocator, FamilyTable& familyTable, ThreadTable& threadTable, ICache& icache, FPU& fpu);
        
        uint64_t getFlop() const { return m_flop; }
        uint64_t getOp()   const { return m_op; }
    
    private:
        ReadExecuteLatch&       m_input;
        ExecuteMemoryLatch&     m_output;
        Allocator&              m_allocator;
        FamilyTable&            m_familyTable;
        ThreadTable&            m_threadTable;
        ICache&                 m_icache;
		FPU&                    m_fpu;
        uint64_t                m_flop;    // FP operations
        uint64_t                m_op;      // Instructions

        static bool execINTA(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func);
        static bool execINTL(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func);
        static bool execINTS(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func);
        static bool execINTM(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func);
        static bool execFLTV(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func);
        static bool execFLTI(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func);
        static bool execFLTL(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func);
		static bool execITFP(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func);
		static bool execFPTI(RegValue& Rcv, const RegValue& Rav, const RegValue& Rbv, int func);
        static bool branchTaken(uint8_t opcode, const RegValue& value);
    };

    class MemoryStage : public Stage
    {
    public:
        PipeAction read();
        PipeAction write();
        MemoryStage(Pipeline& parent, ExecuteMemoryLatch& input, MemoryWritebackLatch& output, DCache& dcache, Allocator& allocator, RegisterFile& regFile, FamilyTable& familyTable);
    
    private:
        ExecuteMemoryLatch&     m_input;
        MemoryWritebackLatch&   m_output;
        Allocator&              m_allocator;
        DCache&                 m_dcache;
        RegisterFile&           m_regFile;
        FamilyTable&            m_familyTable;
    };

    class WritebackStage : public Stage
    {
    public:
        PipeAction read();
        PipeAction write();
        WritebackStage(Pipeline& parent, MemoryWritebackLatch& input, RegisterFile& regFile, Network& network, Allocator& allocator);
    
    private:
        MemoryWritebackLatch&   m_input;
        RegisterFile&           m_regFile;
        Network&                m_network;
        Allocator&              m_allocator;
    };

    Pipeline(Processor& parent, const std::string& name, RegisterFile& regFile, Network& network, Allocator& allocator, FamilyTable& familyTable, ThreadTable& threadTable, ICache& icache, DCache& dcache, FPU& fpu, const Config& config);

    bool idle()   const;

    Result onCycleReadPhase(int stateIndex);
    Result onCycleWritePhase(int stateIndex);

    const Stage& getStage(int i) const { return *m_stages[i]; }
    Processor& getProcessor() const { return m_parent; }

    uint64_t getFlop() const { return m_execute.getFlop(); }
    uint64_t getOp()   const { return m_execute.getOp(); }

private:
    Processor&          m_parent;
    RegisterFile&       m_regFile;

    FetchDecodeLatch     m_fdLatch;
    DecodeReadLatch      m_drLatch;
    ReadExecuteLatch     m_reLatch;
    ExecuteMemoryLatch   m_emLatch;
    MemoryWritebackLatch m_mwLatch;

    static const int    NUM_STAGES = 6;
    Stage*              m_stages[NUM_STAGES];
    bool                m_runnable[NUM_STAGES];

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

