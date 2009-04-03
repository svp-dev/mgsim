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

template <typename T>
class BroadcastRegisters : public IRegister
{
    struct Data
    {
        bool m_forwarded;
        bool m_processed;
        bool m_full;
        T    m_data;

        void Clear()        { m_full = m_processed = m_forwarded = false; }
        void SetProcessed() { m_processed = true; if (m_forwarded) Clear(); }
        void SetForwarded() { m_forwarded = true; if (m_processed) Clear(); }
        Data()              { Clear(); }
    };

    Data          m_temp;
    Data          m_remote;
    Data          m_local;
    Data          m_sending;

    void OnUpdate()
    {
        if (m_temp.m_full)
        {
            m_remote = m_temp;
            m_temp.Clear();
        }

        if (!IsSendingFull())
        {
            if (IsRemoteFull() && !m_remote.m_forwarded)
            {
                m_sending = m_remote;
                m_remote.SetForwarded();
            }
            else if (IsLocalFull() && m_local.m_processed)
            {
                m_sending = m_local;
                m_local.SetForwarded();
            }
            m_sending.SetProcessed();
        }
    }

public:
    void WriteLocal(const T& data, bool processed = true) { COMMIT{ m_local.m_data = data; m_local.m_full = true; if (processed) m_local.SetProcessed(); } }
    void WriteRemote(const T& data)                       { COMMIT{ m_temp .m_data = data; m_temp .m_full = true; } }

    void SetLocalProcessed()   { COMMIT{ m_local  .SetProcessed(); } }
    void SetRemoteProcessed()  { COMMIT{ m_remote .SetProcessed(); } }
    void SetSendingForwarded() { COMMIT{ m_sending.SetForwarded(); } }

    const T& ReadLocal()   const { return m_local.m_data;   }
    const T& ReadRemote()  const { return m_remote.m_data;  }
    const T& ReadSending() const { return m_sending.m_data; }

    bool IsRemoteProcessed() const { return m_remote.m_processed; }
    bool IsSendingFull()     const { return m_sending.m_full; }
    bool IsLocalFull()       const { return m_local.m_full;   }
    bool IsRemoteFull()      const { return m_remote.m_full;  }
    bool IsEmpty()           const { return !IsLocalFull() && !IsRemoteFull() && !IsSendingFull(); }
    
    BroadcastRegisters(Kernel& kernel) : IRegister(kernel)
    {
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
    GFID      fid;                   // Global Family ID to use for the family
    bool      infinite;
	int64_t   start;
	int64_t   step;
	uint64_t  nThreads;
	uint64_t  virtBlockSize;
	TSize     physBlockSize;
	MemAddr   address;			     // Address of the thread
    struct {
        LPID pid;
        TID  tid;
    } parent;                        // Parent Thread ID
    RegsNo    regsNo[NUM_REG_TYPES]; // Register information
};

class Network : public IComponent
{
	struct RegInfo
	{
		RegIndex current;
		RegSize  count;
	};

    struct SharedInfo
    {
        GFID     fid;		// Family of the shared
        RegAddr  addr;		// Address of the shared (type and 0-based index)
        RegValue value;		// Value, in case of response
        bool     parent;	// Read/write from/to parent

        SharedInfo() : fid(INVALID_GFID) {}
    };

    struct RemoteFID
    {
        GFID fid;
        LPID pid;

        RemoteFID(GFID fid, LPID pid) : fid(fid), pid(pid) {}
        RemoteFID() {}
    };

public:
    Network(Processor& parent, const std::string& name, const std::vector<Processor*>& grid, LPID lpid, Allocator& allocator, RegisterFile& regFile, FamilyTable& familyTable);
    void Initialize(Network& prev, Network& next);

    // Public functions
    bool SendFamilyReservation(GFID fid);
    bool SendFamilyUnreservation(GFID fid);
    bool SendFamilyCreate(LFID fid);
    bool SendFamilyDelegation(LFID fid);
    bool RequestToken();
    bool SendThreadCleanup(GFID fid);
    bool SendThreadCompletion(GFID fid);
    bool SendFamilyCompletion(GFID fid);
    bool SendRemoteSync(GPID pid, LFID fid, ExitCode code);
	
	// addr is into the thread's shareds space
    bool SendShared   (GFID fid, bool parent, const RegAddr& addr, const RegValue& value);
    bool RequestShared(GFID fid, const RegAddr& addr, bool parent);
    
    //
    // Network-specific stuff, do not call outside of this class
    //
    bool OnFamilyReservationReceived(const RemoteFID& rfid);
    bool OnFamilyUnreservationReceived(const RemoteFID& rfid);
    bool OnFamilyCreateReceived(const CreateMessage& msg);
    bool OnFamilyDelegationReceived(const DelegateMessage& msg);
	bool OnGlobalReceived(LPID parent, const RegValue& value);
    bool OnRemoteTokenRequested();
    bool OnTokenReceived();
    bool OnThreadCleanedUp(GFID fid);
    bool OnThreadCompleted(GFID fid);
    bool OnFamilyCompleted(GFID fid);
    bool OnRemoteSyncReceived(LFID fid, ExitCode code);
    Result OnSharedRequested(const SharedInfo& sharedInfo);
    Result OnSharedReceived(const SharedInfo& sharedInfo);

    Result OnCycleReadPhase(unsigned int stateIndex);
    Result OnCycleWritePhase(unsigned int stateIndex);

//private:
    Processor&                     m_parent;
    RegisterFile&                  m_regFile;
    FamilyTable&                   m_familyTable;
    Allocator&                     m_allocator;
    Network*                       m_prev;
    Network*                       m_next;
    LPID                           m_lpid;
    const std::vector<Processor*>& m_grid;

	enum CreateState
	{
		CS_PROCESSING_NONE,
		CS_PROCESSING_LOCAL,
		CS_PROCESSING_REMOTE,
	};

	struct GlobalInfo
	{
		RegAddr  addr;
		RegSize  count;
		RegValue local;
		BroadcastRegisters<std::pair<LPID, RegValue> > value;

		GlobalInfo(Kernel& kernel) : value(kernel) {}
	};
	
	struct RemoteSync
	{
	    GPID     pid;
	    LFID     fid;
	    ExitCode code;
	};

	// Create information
    Register<std::pair<LFID, CreateMessage  > > m_createLocal;
    Register<std::pair<GPID, DelegateMessage> > m_delegateLocal;
    
	Register<CreateMessage>   m_createRemote;
	Register<DelegateMessage> m_delegateRemote;
	CreateState               m_createState;
	LFID                      m_createFid;
	RegIndex                  m_globalsBase[NUM_REG_TYPES];
	GlobalInfo                m_global;

	// Notifications and reservations
    BroadcastRegisters<RemoteFID> m_reservation;
    BroadcastRegisters<RemoteFID> m_unreservation;
    Register<GFID>                m_completedFamily;
    Register<GFID>                m_completedThread;
    Register<GFID>                m_cleanedUpThread;
    Register<RemoteSync>          m_remoteSync;

	// Shareds communication
    SharedInfo m_sharedRequest;
    SharedInfo m_sharedResponse;
    SharedInfo m_sharedReceived;

	// Token stuff
    Register<bool> m_hasToken;		 // We have the token
    unsigned int   m_lockToken;	 	 // #Locks on the token
    Register<bool> m_wantToken;		 // We want the token
    Register<bool> m_nextWantsToken; // Next processor wants the token
	Register<bool> m_requestedToken; // We've requested the token
};

}
#endif

