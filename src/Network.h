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

/// Network message for delegated creates
struct DelegateMessage
{
    SInteger  start;
    SInteger  limit;
    SInteger  step;
    Integer   blockSize;
    bool      exclusive;
	MemAddr   address;
	struct {
	    GPID pid;
	    LFID fid;
    }         parent;
    RegsNo    regsNo[NUM_REG_TYPES];
};

/// Network message for group creates
struct CreateMessage
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
    struct {
        GPID gpid;
        LPID lpid;
        LFID fid;
    } parent;                       ///< Parent ID
    RegsNo    regsNo[NUM_REG_TYPES];///< Register counts
};

class Network : public IComponent
{
    template <typename T>
    class Register : public Simulator::Register<T>
    {
        ArbitratedService m_service;

    public:
        void AddSource(const ArbitrationSource& source) {
            m_service.AddSource(source);
        }
        
        // Use ForceWrite to avoid deadlock issues with network buffers
        // if you KNOW that the buffer will be cleared at the same cycle.
        bool ForceWrite(const T& data)
        {
            if (!m_service.Invoke()) {
                return false;
            }
            Simulator::Register<T>::Write(data);
            return true;
        }
    
        bool Write(const T& data)
        {
            if (!this->Empty() || !m_service.Invoke()) {
                return false;
            }
            Simulator::Register<T>::Write(data);
            return true;
        }

        Register(Kernel& kernel, const IComponent& component, const std::string& name)
            : Simulator::Register<T>(kernel), m_service(component, name)
        {
        }
    };

	template <typename T>
	struct RegisterPair
	{
	    Register<T> out;  ///< Register for outgoing messages
	    Register<T> in;   ///< Register for incoming messages

        RegisterPair(Kernel& kernel, const IComponent& component, const std::string& name)
            : out(kernel, component, name + ".out"),
              in (kernel, component, name + ".in")
        {
        }
	};
	
public:
    Network(Processor& parent, const std::string& name, PlaceInfo& place, const std::vector<Processor*>& grid, LPID lpid, Allocator& allocator, RegisterFile& regFile, FamilyTable& familyTable);
    void Initialize(Network& prev, Network& next);

    bool SendGroupCreate(LFID fid);
    bool SendDelegatedCreate(LFID fid);
    bool RequestToken();
    void ReleaseToken();
    bool SendThreadCleanup(LFID fid);
    bool SendThreadCompletion(LFID fid);
    bool SendFamilySynchronization(LFID fid);
    bool SendFamilyTermination(LFID fid);
    bool SendRemoteSync(GPID pid, LFID fid, ExitCode code);
    
    bool SendRegister   (const RemoteRegAddr& addr, const RegValue& value);
    bool RequestRegister(const RemoteRegAddr& addr, LFID fid_self);
    
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    struct RegisterRequest
    {
        RemoteRegAddr addr;         ///< Address of the register to read
        GPID          return_pid;   ///< Address of the core to send to (delegated requests only)
        LFID          return_fid;   ///< FID of the family on the next core to write back to
    };
    
    struct RegisterResponse
    {
        RemoteRegAddr addr;  ///< Address of the register to write
        RegValue      value; ///< Value, in case of response
    };

    bool SetupFamilyNextLink(LFID fid, LFID link_next);
    bool OnGroupCreateReceived(const CreateMessage& msg);
    bool OnDelegationCreateReceived(const DelegateMessage& msg);
    bool OnDelegationFailedReceived(LFID fid);
    bool OnTokenReceived();
    bool OnThreadCleanedUp(LFID fid);
    bool OnThreadCompleted(LFID fid);
    bool OnFamilySynchronized(LFID fid);
    bool OnFamilyTerminated(LFID fid);
    bool OnRemoteSyncReceived(LFID fid, ExitCode code);
    bool OnRegisterRequested(const RegisterRequest& request);
    bool OnRegisterReceived (const RegisterResponse& response);
    bool OnRemoteRegisterRequested(const RegisterRequest& request);
    bool OnRemoteRegisterReceived(const RegisterResponse& response);
    bool ReadRegister(const RegisterRequest& request);
    bool WriteRegister(const RemoteRegAddr& addr, const RegValue& value);

    Result OnCycle(unsigned int stateIndex);

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
    Register<CreateMessage>   m_createLocal;    ///< Outgoing group create
	Register<CreateMessage>   m_createRemote;   ///< Incoming group create

    // Delegation creates
    Register<std::pair<GPID, DelegateMessage> > m_delegateLocal;        ///< Outgoing delegation create
	Register<DelegateMessage>                   m_delegateRemote;       ///< Incoming delegation create
    Register<std::pair<GPID, LFID> >            m_delegateFailedLocal;  ///< Outgoing delegation failure
    Register<LFID>                              m_delegateFailedRemote; ///< Incoming delegation failure

	// Notifications
    Register<LFID>       m_synchronizedFamily; ///< Outgoing 'family synchronized' notification
    Register<LFID>       m_terminatedFamily;   ///< Outgoing 'family terminated' notification
	
    Register<LFID>       m_completedThread;     ///< Incoming 'thread completed' notification
    Register<LFID>       m_cleanedUpThread;     ///< Incoming 'thread cleaned up' notification
    Register<RemoteSync> m_remoteSync;          ///< Incoming remote synchronization

	// Register communication
	RegisterPair<RegisterRequest>  m_registerRequestRemote;  ///< Remote register request
	RegisterPair<RegisterResponse> m_registerResponseRemote; ///< Remote register response
    RegisterPair<RegisterRequest>  m_registerRequestGroup;   ///< Group register request
    RegisterPair<RegisterResponse> m_registerResponseGroup;  ///< Group register response

	// Token management
    Flag m_hasToken;    ///< We have the token
    Flag m_wantToken; 	///< We want the token
    Flag m_tokenBusy;   ///< Is the token still in use?
};

}
#endif

