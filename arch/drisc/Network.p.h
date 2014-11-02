// -*- c++ -*-
#ifndef NETWORK_H
#define NETWORK_H

#include <sim/kernel.h>
#include <sim/inspect.h>
#include <sim/buffer.h>
#include <sim/register.h>
#include <sim/ports.h>
#include <arch/simtypes.h>
#include <arch/drisc/forward.h>

namespace Simulator
{
    namespace drisc
    {

        // {% from "sim/macros.p.h" import gen_variant,gen_struct %}

        // RemoteMessage: delegation messages between cores
        // {% call gen_variant() %}
        ((name RemoteMessage)
         (variants
          (MSG_NONE)
          (MSG_ALLOCATE allocate
           (state
            (PlaceID        place)           ///< The place to allocate at
            (PID            completion_pid)  ///< PID where the thread runs that issued the allocate
            (RegIndex       completion_reg)  ///< Register to write FID back to
            (AllocationType type)            ///< Type of the allocation
            (bool           suspend)         ///< Queue request if no context available?
            (bool           exclusive)       ///< Allocate the exclusive context?

            (bool           bundle)          ///< Is this allocation also bundling a create?
            (MemAddr        pc)              ///< Bundled program counter
            (Integer        parameter)       ///< Bundled program-specified parameter
            (SInteger       index)           ///< Bundled table-specified parameter
               ))

          (MSG_SET_PROPERTY property
           (state
            (Integer        value)           ///< The new value of the property
            (FID            fid)             ///< Family to set the property of
            (FamilyProperty type)            ///< The property to set
               ))

          (MSG_CREATE create
           (state
            (MemAddr  address)               ///< Address of the thread program
            (FID      fid)                   ///< Family to start creation of
            (PID      completion_pid)        ///< PID where the thread that issued the request is running
            (RegIndex completion_reg)        ///< Register to write create-completion to

            (bool     bundle)                ///< Whether this is a create resulting from a bundle
            (Integer  parameter)             ///< Bundled program-specified parameter
            (SInteger index)                 ///< Bundled table-specified parameter
               ))

          (MSG_SYNC sync
           (state
            (FID      fid)                   ///< Family to sync on
            (RegIndex completion_reg)        ///< Register to write sync-completion to
               ))

          (MSG_DETACH detach
           (state
            (FID fid)                        ///< Family to detach
               ))

          (MSG_BREAK brk
           (state
            (PID   pid)                      ///< Core to send break to
            (LFID  fid)                      ///< Family to break
               ))

          (MSG_RAW_REGISTER rawreg
           (state
            (RegAddr   addr noserialize)     ///< Register address to write to
            (RegValue  value noserialize)    ///< Value to write in register
            (PID       pid)                  ///< Processor where the register is
               )
           (serializer_append "__a & Serialization::reg(addr, value);"))

          (MSG_FAM_REGISTER famreg
           (state
            (FID           fid)              ///< Family hosting the register to read or write
            (RemoteRegType kind)             ///< Type of register (global, shared, local)
            (bool          write)            ///< Whether to read or write
            (RegAddr       addr)             ///< Address of register to access
            (union (state
                    (RegValue value)            ///< Value in case of write
                    (RegIndex completion_reg)   ///< Register to send value back to in case of read
                ) noserialize))
           (serializer_append
            "if (write) __a & Serialization::reg(addr, value);
             else __a & addr & completion_reg;"))

             )
         (raw "std::string str() const;")
            )
        // {% endcall %}

        // LinkMessage: messages on link network between adjacent cores
        // {% call gen_variant() %}
        ((name LinkMessage)
         (variants
          (MSG_ALLOCATE allocate
           (state
            (LFID     first_fid)      ///< FID on the first core of the matching family
            (LFID     prev_fid)       ///< FID on the previous core (sender) of allocated family
            (PSize    size)           ///< Size of the place
            (PID      completion_pid) ///< PID where the thread runs that issued the allocate
            (RegIndex completion_reg) ///< Reg on parent_pid of the completion register
            (bool     exact)          ///< Allocate exactly 'size' cores
            (bool     suspend)        ///< Suspend until we get a context (only if exact)
               ))
          (MSG_BALLOCATE ballocate
           (state
            (unsigned min_contexts)   ///< Minimum of contexts found so far
            (PID      min_pid)        ///< Core where the minimum was found
            (PSize    size)           ///< Size of the place
            (PID      completion_pid) ///< PID where the thread runs that issued the allocate
            (RegIndex completion_reg) ///< Reg on parent_pid of the completion register
            (bool     suspend)        ///< Suspend until we get a context (only if exact)
               ))

          (MSG_SET_PROPERTY property
           (state
            (LFID           fid)      ///< The family on which to set the property
            (FamilyProperty type)     ///< The property to set
            (Integer        value)    ///< Value to set
               ))

          (MSG_CREATE create
           (state
            (LFID    fid)                       ///< The family to start
            (PSize   numCores)                  ///< Number of cores participating
            (MemAddr address)                   ///< Initial PC
            (array   regs RegsNo NUM_REG_TYPES) ///< Register counts
               ))

          (MSG_DONE done
           (state
            (LFID fid)           ///< The family that was completed
            (bool broken)        ///< Whether the family terminated via 'break'
               ))

          (MSG_SYNC sync
           (state
            (LFID     fid)             ///< The family to wait on
            (PID      completion_pid)  ///< Core to signal termination to
            (RegIndex completion_reg)  ///< Register in core to receive sync status
               ))

          (MSG_DETACH detach
           (state
            (LFID fid)                 ///< The family to detach
               ))
          (MSG_BREAK brk
           (state
            (LFID fid)                 ///< The family to break
               ))

          (MSG_GLOBAL global
           (state
            (LFID     fid)                ///< The family the global value is broadcasted to
            (RegAddr  addr noserialize)   ///< Which register is being broadcasted
            (RegValue value noserialize)  ///< The register value to write
               )
           (serializer_append "__a & Serialization::reg(addr, value);"))
             )
         (raw "std::string str() const;")
            )
        // {% endcall %}

        /// Allocation response (going backwards)
        // {% call gen_struct() %}
        ((name AllocResponse)
         (state
          (PID      completion_pid) ///< PID where the thread runs that issued the allocate
          (RegIndex completion_reg) ///< Reg on parent_pid of the completion register

          (LFID     prev_fid)  ///< FID of the family on the previous (receiver) core
          (LFID     next_fid)  ///< FID of the family on the next (sender) core if !failed

          (PSize    numCores)  ///< Number of cores actually allocated (0 for failed)
          (bool     exact)     ///< If the allocate was exact, unwind all the way
             ))
        // {% endcall %}

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
    // {% call gen_struct() %}
    ((name SyncInfo)
     (state
      (LFID     fid)
      (PID      pid)
      (RegIndex reg)
      (bool     broken)))
    // {% endcall %}

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
