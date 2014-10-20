// -*- c++ -*-
#ifndef NETWORK_H
#define NETWORK_H

#include <sim/kernel.h>
#include <sim/inspect.h>
#include <sim/buffer.h>
#include <sim/register.h>
#include <sim/ports.h>
#include <arch/simtypes.h>
#include "forward.h"

namespace Simulator
{
namespace drisc
{

struct RemoteMessage
{
    enum Type
    {
        MSG_NONE,           ///< No message
        MSG_ALLOCATE,       ///< Allocate family
        MSG_SET_PROPERTY,   ///< Set family property
        MSG_CREATE,         ///< Create family
        MSG_SYNC,           ///< Synchronise on family
        MSG_DETACH,         ///< Detach family
        MSG_BREAK,          ///< Break
        MSG_RAW_REGISTER,   ///< Raw register response
        MSG_FAM_REGISTER,   ///< Family register request or response
    };

    Type type;      ///< Type of the message

    SERIALIZE(a) {
        a & type;
        switch(type) {
        case MSG_NONE: break;
        case MSG_ALLOCATE: a & allocate; break;
        case MSG_SET_PROPERTY: a & property; break;
        case MSG_CREATE: a & create; break;
        case MSG_SYNC: a & sync; break;
        case MSG_DETACH: a & detach; break;
        case MSG_BREAK: a & brk; break;
        case MSG_RAW_REGISTER: a & rawreg; break;
        case MSG_FAM_REGISTER: a & famreg; break;
        }
    }


    /// The message contents
    union
    {
        struct {
            PlaceID        place;         ///< The place to allocate at
            PID            completion_pid;///< PID where the thread runs that issued the allocate
            RegIndex       completion_reg;///< Register to write FID back to
            AllocationType type;          ///< Type of the allocation
            bool           suspend;       ///< Queue request if no context available?
            bool           exclusive;     ///< Allocate the exclusive context?

            bool           bundle;        ///< Is this allocation also bundling a create?
            MemAddr        pc;            ///< Bundled program counter
            Integer        parameter;     ///< Bundled program-specified parameter
            SInteger       index;         ///< Bundled table-specified parameter
            SERIALIZE(a) { a & place & completion_pid & completion_reg
                    & type & suspend & exclusive & bundle & pc
                    & parameter & index; }
        } allocate;

        struct {
            Integer        value;   ///< The new value of the property
            FID            fid;     ///< Family to set the property of
            FamilyProperty type;    ///< The property to set
            SERIALIZE(a) { a & value & fid & type; }
        } property;

        struct {
            MemAddr  address;       ///< Address of the thread program
            FID      fid;           ///< Family to start creation of
            PID      completion_pid;///< PID where the thread that issued the request is running
            RegIndex completion_reg;///< Register to write create-completion to

            bool     bundle;        ///< Whether this is a create resulting from a bundle
            Integer  parameter;     ///< Bundled program-specified parameter
            SInteger index;         ///< Bundled table-specified parameter
            SERIALIZE(a) { a & address & fid & completion_pid & completion_reg
                    & bundle & parameter & index; }
        } create;

        struct {
            FID      fid;           ///< Family to sync on
            RegIndex completion_reg;///< Register to write sync-completion to
            SERIALIZE(a) { a & fid & completion_reg; }
        } sync;

        struct {
            FID fid;                ///< Family to detach
            SERIALIZE(a) { a & fid; }
        } detach;

        struct {
            PID  pid;               ///< Core to send break to
            LFID fid;               ///< Family to break
            SERIALIZE(a) { a & pid & fid; }
        } brk;

        struct
        {
            RegAddr  addr;
            RegValue value;
            PID      pid;
            SERIALIZE(a) { a & Serialization::reg(addr, value) & pid; }
        } rawreg;

        struct
        {
            FID           fid;
            RemoteRegType kind;
            bool          write;
            RegAddr       addr;
            union
            {
                RegValue  value;
                RegIndex  completion_reg;
            };
            SERIALIZE(a) {
                a & fid & kind & write;
                if (write)
                    a & Serialization::reg(addr, value);
                else
                    a & addr & completion_reg;
            }
        } famreg;
    };

    std::string str() const;
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

    SERIALIZE(a) {
        a & type;
        switch(type) {
        case MSG_ALLOCATE: a & allocate; break;
        case MSG_BALLOCATE: a & ballocate; break;
        case MSG_SET_PROPERTY: a & property; break;
        case MSG_CREATE: a & create; break;
        case MSG_DONE: a & done; break;
        case MSG_SYNC: a & sync; break;
        case MSG_DETACH: a & detach; break;
        case MSG_BREAK: a & brk; break;
        case MSG_GLOBAL: a & global; break;
        }
    }

    /// The message contents
    union
    {
        struct
        {
            LFID     first_fid;      ///< FID on the first core of the matching family
            LFID     prev_fid;       ///< FID on the previous core (sender) of allocated family
            PSize    size;           ///< Size of the place
            PID      completion_pid; ///< PID where the thread runs that issued the allocate
            RegIndex completion_reg; ///< Reg on parent_pid of the completion register
            bool     exact;          ///< Allocate exactly 'size' cores
            bool     suspend;        ///< Suspend until we get a context (only if exact)
            SERIALIZE(a) { a & first_fid & prev_fid
                    & size & completion_pid & completion_reg
                    & exact & suspend; }
        } allocate;

        struct
        {
            unsigned min_contexts;   ///< Minimum of contexts found so far
            PID      min_pid;        ///< Core where the minimum was found
            PSize    size;           ///< Size of the place
            PID      completion_pid; ///< PID where the thread runs that issued the allocate
            RegIndex completion_reg; ///< Reg on parent_pid of the completion register
            bool     suspend;        ///< Suspend until we get a context (only if exact)
            SERIALIZE(a) { a & min_contexts & min_pid
                    & size & completion_pid
                    & completion_reg & suspend; }
        } ballocate;

        struct
        {
            LFID           fid;
            FamilyProperty type;    ///< The property to set
            Integer        value;   ///< The new value of the property
            SERIALIZE(a) { a & fid & type & value; }
        } property;

        struct
        {
            LFID     fid;
            PSize    numCores;
            MemAddr  address;
            RegsNo   regs[NUM_REG_TYPES];
            SERIALIZE(a) {
                a & fid & numCores & address;
                for (auto &r : regs)
                    a & r;
            }
        } create;

        struct
        {
            LFID fid;
            bool broken;
            SERIALIZE(a) { a & fid & broken; }
        } done;

        struct
        {
            LFID     fid;
            PID      completion_pid;
            RegIndex completion_reg;
            SERIALIZE(a) { a & fid & completion_pid & completion_reg; }
        } sync;

        struct
        {
            LFID fid;
            SERIALIZE(a) { a & fid; }
        } detach;

        struct {
            LFID fid;               ///< Family to break
            SERIALIZE(a) { a & fid; }
        } brk;

        struct
        {
            LFID     fid;
            RegAddr  addr;
            RegValue value;
            SERIALIZE(a)
            { a & fid & Serialization::reg(addr, value); }
        } global;
    };

    std::string str() const;
};

/// Allocation response (going backwards)
struct AllocResponse
{
    PID      completion_pid; ///< PID where the thread runs that issued the allocate
    RegIndex completion_reg; ///< Reg on parent_pid of the completion register

    LFID     prev_fid;  ///< FID of the family on the previous (receiver) core
    LFID     next_fid;  ///< FID of the family on the next (sender) core if !failed

    PSize    numCores;  ///< Number of cores actually allocated (0 for failed)
    bool     exact;     ///< If the allocate was exact, unwind all the way

    SERIALIZE(a) { a & completion_pid & completion_reg
            & prev_fid & next_fid & numCores & exact; }
};

class Network : public Object, public Inspect::Interface<Inspect::Read>
{
    /*
     A specialization of the generic register to implement arbitration
    */
    template <typename T, typename Arbitrator = PriorityArbitratedPort>
    class Register : public Simulator::Register<T>
    {
        ArbitratedService<Arbitrator> p_service;

    public:
        void AddProcess(const Process& process)
        {
            p_service.AddProcess(process);
        }

        bool Write(const T& data)
        {
            if (!this->Empty() || !p_service.Invoke()) {
                return false;
            }
            Simulator::Register<T>::Write(data);
            return true;
        }

        Register(const std::string& name, Object& parent, Clock& clock)
            : Object(name, parent),
              Storage(name, parent, clock),
              Simulator::Register<T>(name, parent, clock),
              p_service(clock, this->GetName() + ".p_service")
        {
        }
        static constexpr const char *NAME_PREFIX = "rn_";
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

            RegisterPair(const std::string& name, Object& parent, Clock& clock)
                : Object(name, parent),
                  remote(NULL),
                  InitProcessInTemplate(p_Transfer, DoTransfer),
                  InitStorage(out, clock),
                  InitStorage(in, clock)
            {
                out.Sensitive(p_Transfer);
            }
            RegisterPair(const RegisterPair&) = delete; // No copy
            RegisterPair& operator=(const RegisterPair&) = delete; // No assign


	};

public:
    struct SyncInfo
    {
        LFID     fid;
        PID      pid;
        RegIndex reg;
        bool     broken;
    };

    Network(const std::string& name, DRISC& parent, Clock& clock,
            const std::vector<DRISC*>& grid);
    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;

    void Connect(Network* prev, Network* next);
    void Initialize();

    bool SendMessage(const RemoteMessage& msg);
    bool SendMessage(const LinkMessage& msg);
    bool SendAllocResponse(const AllocResponse& msg);
    bool SendSync(const SyncInfo& event);

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    struct DelegateMessage
    {
        PID           src;     ///< Source processor
        PID           dest;    ///< Destination processor
        RemoteMessage payload; ///< Body of message
        SERIALIZE(a) { a & "dm" & src & dest & payload; }
    };

    bool ReadRegister(LFID fid, RemoteRegType kind, const RegAddr& addr, RegValue& value);
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

    RegisterFile&                  m_regFile;
    FamilyTable&                   m_familyTable;
    Allocator&                     m_allocator;
    Network*                       m_prev;
    Network*                       m_next;
    const std::vector<DRISC*>& m_grid;
    unsigned int                   m_loadBalanceThreshold;

    Object& GetDRISCParent() const { return *GetParent(); }

    // Statistics
    DefineSampleVariable(uint64_t, numAllocates);
    DefineSampleVariable(uint64_t, numCreates);

public:
    // Delegation network
    Register<DelegateMessage>   m_delegateOut;    ///< Outgoing delegation messages
    Register<DelegateMessage, CyclicArbitratedPort>   m_delegateIn;     ///< Incoming delegation messages
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

}
}

#endif
