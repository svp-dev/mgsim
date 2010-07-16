#ifndef NETWORK_H
#define NETWORK_H

#include "storage.h"

namespace Simulator
{

class Processor;
class RegisterFile;
class Allocator;
class FamilyTable;
struct PlaceInfo;

/// Message for remote creates
struct RemoteCreateMessage
{
	MemAddr  address;            ///< Address of the family
    FID      fid;                ///< FID of the family to create
    RegIndex completion;         ///< Register on remote processor to write completion back to
};

/// Network message for completed creates
struct CreateResult
{
    LFID     fid_parent;
    LFID     fid_remote;
    LFID     fid_last;
    RegIndex completion;
};

/// Network message for group creates
struct GroupCreateMessage
{
    LFID      first_fid;            ///< FID of the family on the creating CPU
    LFID      link_prev;            ///< FID to use for the next CPU's family's link_prev
    bool      infinite;             ///< Infinite create?
	SInteger  start;                ///< Index start
	SInteger  step;                 ///< Index step size
	Integer   nThreads;             ///< Number of threads in the family
	Integer   virtBlockSize;        ///< Virtual block size
	TSize     physBlockSize;        ///< Physical block size
	MemAddr   address;			    ///< Initial address of new threads
	LPID      parent_lpid;          ///< Parent core
    RegIndex  completion;           ///< Register in parent thread to write on completion
    RegsNo    regsNo[NUM_REG_TYPES];///< Register counts
};

/// Register messages over the group
struct RegisterMessage
{
    RemoteRegAddr addr;        ///< Address of the register to read or write
    RegValue      value;       ///< The value of the register to write
    RegIndex      return_addr; ///< Register address to write back to (RRT_LAST_SHARED only)
};
    
struct RemoteMessage
{
    enum Type
    {
        MSG_NONE,           ///< Invalid message
        MSG_ALLOCATE,       ///< Allocate family
        MSG_SET_PROPERTY,   ///< Set family property
        MSG_CREATE,         ///< Create family
        MSG_DETACH,         ///< Detach family
        MSG_SYNC,           ///< Synchronise on family
        MSG_REGISTER,       ///< Register request or response
    };
        
    Type type;      ///< Type of the message
        
    /// The message contents
    union
    {
        struct {
            GPID     pid;
            bool     exclusive;
            bool     suspend;
            RegIndex completion;
        } allocate;
            
        struct {
            FID            fid;
            FamilyProperty type;
            Integer        value;
        } property;
            
        RemoteCreateMessage create;
            
        struct {
            FID      fid;
            RegIndex reg;
        } sync;
            
        struct {
            FID fid;
        } detach;

        RegisterMessage reg;
    };
};

class Network : public Object
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

        Register(const Object& object, const std::string& name)
            : Simulator::Register<T>(*object.GetKernel()), m_service(object, name)
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
	    }

        RegisterPair(Object& parent, const std::string& name)
            : Object(name, parent),
              remote(NULL),
              p_Transfer("transfer", delegate::create<RegisterPair, &RegisterPair::DoTransfer>(*this)),
              out(parent, name + ".out"),
              in (parent, name + ".in")
        {
            out.Sensitive(p_Transfer);
        }
	};
	
public:
    Network(const std::string& name, Processor& parent, PlaceInfo& place, const std::vector<Processor*>& grid, LPID lpid, Allocator& allocator, RegisterFile& regFile, FamilyTable& familyTable);
    void Initialize(Network& prev, Network& next);

    bool SendMessage(const RemoteMessage& msg);
    
    bool SendGroupCreate(LFID fid, RegIndex completion);
    bool RequestToken();
    void ReleaseToken();
    bool SendThreadCleanup(LFID fid);
    bool SendFamilySynchronization(LFID fid);
    bool SendAllocation(const PlaceID& place, RegIndex reg);
    
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    struct DelegateMessage : public RemoteMessage
    {
        GPID src;  ///< Source processor
        GPID dest; ///< Destination processor
    };
    
    bool OnTokenReceived();
    bool ReadLastShared(const RemoteRegAddr& addr, RegValue& value);
    bool WriteRegister(const RemoteRegAddr& addr, const RegValue& value);
    
    // Processes
    Result DoRegisters();
    Result DoCreation();
    Result DoThreadCleanup();
    Result DoFamilySync();
    Result DoReserveFamily();
    Result DoCreateResult();
    Result DoDelegationOut();
    Result DoDelegationIn();

    Processor&                     m_parent;
    RegisterFile&                  m_regFile;
    FamilyTable&                   m_familyTable;
    Allocator&                     m_allocator;
    PlaceInfo&                     m_place;
    Network*                       m_prev;
    Network*                       m_next;
    LPID                           m_lpid;
    const std::vector<Processor*>& m_grid;

public:
	struct RemoteSync
	{
	    GPID     pid;
	    LFID     fid;
	    ExitCode code;
	};
	
	// Group creates
    Register<GroupCreateMessage>   m_createLocal;    ///< Outgoing group create
	Register<GroupCreateMessage>   m_createRemote;   ///< Incoming group create

	// Inter-core messages
    RegisterPair<LFID>         m_synchronizedFamily; ///< Notification: Family synchronized
    RegisterPair<CreateResult> m_createResult;       ///< Create result
    
	// Register communication
    RegisterPair<RegisterMessage> m_registers;  ///< Group registers channel
	
    // Delegation network
    Register<DelegateMessage> m_delegateOut;    ///< Outgoing delegation messages
    Register<DelegateMessage> m_delegateIn;     ///< Incoming delegation messages

	// Token management
    Flag       m_hasToken;    ///< We have the token
    SingleFlag m_wantToken;   ///< We want the token
    Flag       m_tokenBusy;   ///< Is the token still in use?
    
    // Processes
    Process p_Registers;
    Process p_Creation;
    Process p_FamilySync;
    Process p_ReserveFamily;
    Process p_CreateResult;
    Process p_DelegationOut;
    Process p_DelegationIn;
};

}
#endif

