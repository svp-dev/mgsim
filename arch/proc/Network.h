#ifndef NETWORK_H
#define NETWORK_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

struct RemoteMessage
{
    enum Type
    {
        MSG_NONE,           ///< No message
        MSG_ALLOCATE,       ///< Allocate family
        MSG_BUNDLE,            ///< Indirect creation
        MSG_SET_PROPERTY,   ///< Set family property
        MSG_CREATE,         ///< Create family
        MSG_SYNC,           ///< Synchronise on family
        MSG_DETACH,         ///< Detach family
        MSG_BREAK,          ///< Break
        MSG_RAW_REGISTER,   ///< Raw register response
        MSG_FAM_REGISTER,   ///< Family register request or response
    };
        
    Type type;      ///< Type of the message
        
    /// The message contents
    union
    {
        struct {
            PlaceID        place;         ///< The place to allocate at
            bool           suspend;       ///< Queue request if no context available?
            bool           exclusive;     ///< Allocate the exclusive context?
            AllocationType type;          ///< Type of the allocation
            PID            completion_pid;///< PID where the thread runs that issued the allocate
            RegIndex       completion_reg;///< Register to write FID back to
            Bundle         bundle;
        } allocate;
            
        struct {
            FID            fid;     ///< Family to set the property of
            FamilyProperty type;    ///< The property to set
            Integer        value;   ///< The new value of the property
        } property;
            
        struct {
            FID      fid;           ///< Family to start creation of
            MemAddr  address;       ///< Address of the family
            RegIndex completion_reg;///< Register to write create-completion to
            PID      completion_pid;
            bool     bundle;
            Integer  parameter;
            size_t   index;
        } create;
            
        struct {
            FID      fid;           ///< Family to sync on
            RegIndex completion_reg;///< Register to write sync-completion to
        } sync;
            
        struct {
            FID fid;                ///< Family to detach
        } detach;

        struct {
            PID  pid;               ///< Core to send break to
            LFID fid;               ///< Family to break
        } brk;
        
        struct
        {
            PID      pid;
            RegAddr  addr;
            RegValue value;
        } rawreg;

        struct
        {
            FID           fid;
            RemoteRegType kind;
            RegAddr       addr;
            union
            {
                RegValue  value;
                RegIndex  completion_reg;
            };
        } famreg;
    };
};

struct LinkMessage
{
    enum Type
    {
        MSG_ALLOCATE,       ///< Allocate family
        MSG_BALLOCATE,      ///< Balanced allocate
        MSG_SET_PROPERTY,   ///< Set family property
        MSG_CREATE,         ///< Create family
        MSG_DONE,           ///< Family has finished on previous core
        MSG_SYNC,           ///< Synchronise on family
        MSG_DETACH,         ///< Detach family
        MSG_BREAK,          ///< Break
        MSG_GLOBAL,         ///< Global register data
    };
        
    Type type;      ///< Type of the message
        
    /// The message contents
    union
    {
        struct
        {
            LFID     first_fid;      ///< FID on the first core of the matching family
            LFID     prev_fid;       ///< FID on the previous core (sender) of allocated family
            PSize    size;           ///< Size of the place
            bool     exact;          ///< Allocate exactly 'size' cores
            bool     suspend;        ///< Suspend until we get a context (only if exact)
            PID      completion_pid; ///< PID where the thread runs that issued the allocate
            RegIndex completion_reg; ///< Reg on parent_pid of the completion register
        } allocate;

        struct
        {
            unsigned min_contexts;   ///< Minimum of contexts found so far
            PID      min_pid;        ///< Core where the minimum was found
            PSize    size;           ///< Size of the place
            bool     suspend;        ///< Suspend until we get a context (only if exact)
            PID      completion_pid; ///< PID where the thread runs that issued the allocate
            RegIndex completion_reg; ///< Reg on parent_pid of the completion register
        } ballocate;
    
        struct
        {
            LFID           fid;
            FamilyProperty type;    ///< The property to set
            Integer        value;   ///< The new value of the property
        } property;
        
        struct
        {
            LFID     fid;
            PSize    numCores;
            MemAddr  address;
            RegsNo   regs[NUM_REG_TYPES];
        } create;
        
        struct
        {
            LFID fid;
        } done;

        struct
        {
            LFID     fid;
            PID      completion_pid;
            RegIndex completion_reg;
        } sync;
        
        struct
        {
            LFID fid;
        } detach;

        struct {
            LFID fid;               ///< Family to break
        } brk;
        
        struct
        {
            LFID     fid;
            RegAddr  addr;
            RegValue value;
        } global;
    };
};

/// Allocation response (going backwards)
struct AllocResponse
{
    PSize    numCores;  ///< Number of cores actually allocated (0 for failed)
    bool     exact;     ///< If the allocate was exact, unwind all the way
    LFID     prev_fid;  ///< FID of the family on the previous (receiver) core
    LFID     next_fid;  ///< FID of the family on the next (sender) core if !failed
        
    PID      completion_pid; ///< PID where the thread runs that issued the allocate
    RegIndex completion_reg; ///< Reg on parent_pid of the completion register
};   

class Network : public Object, public Inspect::Interface<Inspect::Read>
{
    /*
     A specialization of the generic register to implement arbitration
    */
    template <typename T>
    class Register : public Simulator::Register<T>
    {
        ArbitratedService<> m_service;

    public:
        void AddProcess(const Process& process) {
            m_service.AddProcess(process);
        }
        
        bool Write(const T& data)
        {
            if (!this->Empty() || !m_service.Invoke()) {
                return false;
            }
            Simulator::Register<T>::Write(data);
            return true;
        }

        Register(Object& object, const std::string& name)
            : Object(name, object, object.GetClock()),
              Storage(name, object, object.GetClock()), 
              Simulator::Register<T>(name + ".reg", object, object.GetClock()), 
              m_service(object, object.GetClock(), name + ".p_service")
        {
        }
    };

    /*
     A register pair is an output and input register on different cores that
     are directly connected to each other. It contains a single process that
     moves data from the output on one core to the input register on the
     other core.
     
     The network class should have a process to be sensitive on the input
     register.
    */
	template <typename T>
	class RegisterPair : public Object
	{
	private:
	    Register<T>* remote;     ///< Remote register to send output to
	    Process      p_Transfer; ///< The transfer process
	    
	public:
	    Register<T>  out;        ///< Register for outgoing messages
	    Register<T>  in;         ///< Register for incoming messages
	    
	    /// Transfers the output data to the input buffer
	    Result DoTransfer()
	    {
	        assert(!out.Empty());
	        assert(remote != NULL);
	        if (!remote->Write(out.Read()))
	        {
	            return FAILED;
	        }
	        out.Clear();
	        return SUCCESS;
	    }
	    
	    /// Connects the output to the input on the destination core
	    void Initialize(RegisterPair<T>& dest)
	    {
	        assert(remote == NULL);
	        remote = &dest.in;
	        dest.in.AddProcess(p_Transfer);
            p_Transfer.SetStorageTraces(dest.in);
	    }

        RegisterPair(Object& parent, const std::string& name)
            : Object(name, parent),
              remote(NULL),
              p_Transfer(*this, "transfer", delegate::create<RegisterPair, &RegisterPair::DoTransfer>(*this)),
              out(parent, name + ".out"),
              in (parent, name + ".in")
        {
            out.Sensitive(p_Transfer);
        }
	};
	
public:
    struct SyncInfo
    {
        LFID     fid;
        PID      pid;
        RegIndex reg;
    };
    
    Network(const std::string& name, Processor& parent, Clock& clock, const std::vector<Processor*>& grid, Allocator& allocator, RegisterFile& regFile, FamilyTable& familyTable, Config& config);
    void Initialize(Network* prev, Network* next);

    bool SendMessage(const RemoteMessage& msg);
    bool SendMessage(const LinkMessage& msg);
    bool SendAllocResponse(const AllocResponse& msg);
    bool SendSync(const SyncInfo& event);

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    struct DelegateMessage : public RemoteMessage
    {
        PID src;  ///< Source processor
        PID dest; ///< Destination processor
    };
    
    bool ReadLastShared(LFID fid, const RegAddr& addr, RegValue& value);
    bool WriteRegister(LFID fid, RemoteRegType kind, const RegAddr& raddr, const RegValue& value);
    bool OnDetach(LFID fid);
    bool OnBreak(LFID fid);
    bool OnSync(LFID fid, PID completion_pid, RegIndex completion_reg);
    
    // Processes
    Result DoLink();
    Result DoAllocResponse();
    Result DoDelegationOut();
    Result DoDelegationIn();
    Result DoSyncs();

    Processor&                     m_parent;
    RegisterFile&                  m_regFile;
    FamilyTable&                   m_familyTable;
    Allocator&                     m_allocator;
    Network*                       m_prev;
    Network*                       m_next;
    const std::vector<Processor*>& m_grid;
    unsigned int                   m_loadBalanceThreshold;

public:
    // Delegation network
    Register<DelegateMessage>   m_delegateOut;    ///< Outgoing delegation messages
    Register<DelegateMessage>   m_delegateIn;     ///< Incoming delegation messages
    RegisterPair<LinkMessage>   m_link;           ///< Forward link through the cores
    RegisterPair<AllocResponse> m_allocResponse;  ///< Backward link for allocation unroll/commit
    
    // Synchronizations destined for outgoing delegation network.
    // We need this buffer to break the circular depedency between the
    // link and delegation network.
    Buffer<SyncInfo> m_syncs;

    // Processes
    Process p_DelegationOut;
    Process p_DelegationIn;
    Process p_Link;
    Process p_AllocResponse;
    Process p_Syncs;
};

#endif

