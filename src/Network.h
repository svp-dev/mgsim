#ifndef NETWORK_H
#define NETWORK_H

#include "ports.h"

namespace Simulator
{

class Processor;
class RegisterFile;
class Allocator;
class FamilyTable;

template <typename T>
class Register : public IRegister
{
    struct Data
    {
        bool m_full;
        T    m_data;
    };
    Data m_data, m_read;

    void OnUpdate()
    {
        m_read = m_data;
    }

public:
    const T& Read() const     { return m_read.m_data; }
    void Write(const T& data) { COMMIT{ m_data.m_data = data; m_data.m_full = true; } }
    bool IsEmpty() const      { return !m_data.m_full; }
    bool IsFull()  const      { return  m_read.m_full; }
    void Clear()              { COMMIT{ m_data.m_full = m_read.m_full = false; } }

    Register(Kernel& kernel) : IRegister(kernel)
    {
        m_data.m_full = false;
		m_read.m_full = false;
    }

    Register(Kernel& kernel, const T& def) : IRegister(kernel)
    {
        m_data.m_full = false;
		m_read.m_full = true;
		m_read.m_data = def;
		m_data.m_data = def;
    }
};

/// Network message for delegated creates
struct DelegateMessage
{
    SInteger  start;
    SInteger  limit;
    SInteger  step;
    TSize     blockSize;
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
	int64_t   start;                ///< Index start
	int64_t   step;                 ///< Index step size
	uint64_t  nThreads;             ///< Number of threads in the family
	uint64_t  virtBlockSize;        ///< Virtual block size
	TSize     physBlockSize;        ///< Physical block size
	MemAddr   address;			    ///< Initial address of new threads
    struct {
        LPID pid;
        TID  tid;
    } parent;                       ///< Parent Thread ID
    RegsNo    regsNo[NUM_REG_TYPES];///< Register counts
};

class Network : public IComponent
{
    struct RegisterRequest
    {
        RemoteRegAddr addr;         ///< Address of the register to read
        LFID          return_fid;   ///< FID of the family on the next core to write back to
    };
    
    struct RegisterResponse
    {
        RemoteRegAddr addr;  ///< Address of the register to write
        RegValue      value; ///< Value, in case of response
    };

public:
    Network(Processor& parent, const std::string& name, const std::vector<Processor*>& grid, LPID lpid, Allocator& allocator, RegisterFile& regFile, FamilyTable& familyTable);
    void Initialize(Network& prev, Network& next);

    bool SendGroupCreate(LFID fid);
    bool SendDelegatedCreate(LFID fid);
    bool RequestToken();
    bool SendThreadCleanup(LFID fid);
    bool SendThreadCompletion(LFID fid);
    bool SendFamilyCompletion(LFID fid);
    bool SendRemoteSync(GPID pid, LFID fid, ExitCode code);
    
    bool SendRegister   (const RemoteRegAddr& addr, const RegValue& value);
    bool RequestRegister(const RemoteRegAddr& addr, LFID fid_self);
    
private:
    bool SetupFamilyNextLink(LFID fid, LFID link_next);
    bool OnGroupCreateReceived(const CreateMessage& msg);
    bool OnDelegationCreateReceived(const DelegateMessage& msg);
    bool OnRemoteTokenRequested();
    bool OnTokenReceived();
    bool OnThreadCleanedUp(LFID fid);
    bool OnThreadCompleted(LFID fid);
    bool OnFamilyCompleted(LFID fid);
    bool OnRemoteSyncReceived(LFID fid, ExitCode code);
    
    bool OnRegisterRequested(const RegisterRequest& request);
    bool OnRegisterReceived (const RegisterResponse& response);

    Result OnCycleReadPhase(unsigned int stateIndex);
    Result OnCycleWritePhase(unsigned int stateIndex);

    Processor&                     m_parent;
    RegisterFile&                  m_regFile;
    FamilyTable&                   m_familyTable;
    Allocator&                     m_allocator;
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
    Register<std::pair<GPID, DelegateMessage> > m_delegateLocal;  ///< Outgoing delegation create
	Register<DelegateMessage>                   m_delegateRemote; ///< Incoming delegation created

	// Notifications
    Register<LFID>       m_completedFamily;     ///< Incoming 'family completed' notification
    Register<LFID>       m_completedThread;     ///< Incoming 'thread completed' notification
    Register<LFID>       m_cleanedUpThread;     ///< Incoming 'thread cleaned up' notification
    Register<RemoteSync> m_remoteSync;          ///< Incoming remote synchronization

	// Register communication
	ArbitratedService p_registerResponseOut; ///< Port arbitrating outgoing registers
    RegisterRequest   m_registerRequestOut;  ///< Outgoing register request
    RegisterRequest   m_registerRequestIn;   ///< Incoming register request
    RegisterResponse  m_registerResponseOut; ///< Outgoing register response
    RegisterResponse  m_registerResponseIn;  ///< Incoming register response
    RegValue          m_registerValue;       ///< Value of incoming register request

	// Token management
    Register<bool> m_hasToken;		 ///< We have the token
    Register<bool> m_wantToken;		 ///< We want the token
    Register<bool> m_nextWantsToken; ///< Next processor wants the token
	Register<bool> m_requestedToken; ///< We've requested the token

};

}
#endif

