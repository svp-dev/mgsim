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

    void onUpdate()
    {
        m_read = m_data;
    }

public:
    const T& read() const     { return m_read.m_data; }
    void write(const T& data) { COMMIT{ m_data.m_data = data; m_data.m_full = true; } }
    bool empty() const        { return !m_data.m_full; }
    bool full()  const        { return  m_read.m_full; }
    void clear()              { COMMIT{ m_data.m_full = m_read.m_full = false; } }

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

        void clear()        { m_full = m_processed = m_forwarded = false; }
        void setProcessed() { m_processed = true; if (m_forwarded) clear(); }
        void setForwarded() { m_forwarded = true; if (m_processed) clear(); }
        Data()              { clear(); }
    };

    Data          m_temp;
    Data          m_remote;
    Data          m_local;
    Data          m_sending;

    void onUpdate()
    {
        if (m_temp.m_full)
        {
            m_remote = m_temp;
            m_temp.clear();
        }

        if (!isSendingFull())
        {
            if (isRemoteFull() && !m_remote.m_forwarded)
            {
                m_sending = m_remote;
                m_remote.setForwarded();
            }
            else if (isLocalFull() && m_local.m_processed)
            {
                m_sending = m_local;
                m_local.setForwarded();
            }
            m_sending.setProcessed();
        }
    }

public:
    void writeLocal(const T& data, bool processed = true) { COMMIT{ m_local.m_data = data; m_local.m_full = true; if (processed) m_local.setProcessed(); } }
    void writeRemote(const T& data)                       { COMMIT{ m_temp .m_data = data; m_temp .m_full = true; } }

    void setLocalProcessed()   { COMMIT{ m_local  .setProcessed(); } }
    void setRemoteProcessed()  { COMMIT{ m_remote .setProcessed(); } }
    void setSendingForwarded() { COMMIT{ m_sending.setForwarded(); } }

    const T& readLocal()   const { return m_local.m_data;   }
    const T& readRemote()  const { return m_remote.m_data;  }
    const T& readSending() const { return m_sending.m_data; }

    bool isRemoteProcessed() const { return m_remote.m_processed; }
    bool isSendingFull()     const { return m_sending.m_full; }
    bool isLocalFull()       const { return m_local.m_full;   }
    bool isRemoteFull()      const { return m_remote.m_full;  }
    bool empty()             const { return !isLocalFull() && !isRemoteFull() && !isSendingFull(); }

    BroadcastRegisters(Kernel& kernel) : IRegister(kernel)
    {
    }
};

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
    RemoteTID parent;                // Parent Thread ID
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
        PID  pid;

        RemoteFID(GFID _fid = INVALID_GFID, PID _pid = INVALID_PID)
			: fid(_fid), pid(_pid)
        {
        }
    };

public:
    Network(Processor& parent, const std::string& name, Allocator& allocator, RegisterFile& regFile, FamilyTable& familyTable);
    void initialize(Network& prev, Network& next);

    // Public functions
    bool sendFamilyReservation(GFID fid);
    bool sendFamilyUnreservation(GFID fid);
    bool sendFamilyCreate(LFID fid);
    bool requestToken();
    bool sendThreadCleanup(GFID fid);
    bool sendThreadCompletion(GFID fid);
    bool sendFamilyCompletion(GFID fid);
	
	// addr is into the thread's shareds space
    bool sendShared   (GFID fid, bool parent, const RegAddr& addr, const RegValue& value);
    bool requestShared(GFID fid, const RegAddr& addr, bool parent);
    
    //
    // Network-specific stuff, do not call outside of this class
    //
    bool onFamilyReservationReceived(const RemoteFID& rfid);
    bool onFamilyUnreservationReceived(const RemoteFID& rfid);
    bool onFamilyCreateReceived(const CreateMessage& msg);
	bool onGlobalReceived(PID parent, const RegValue& value);
    bool onRemoteTokenRequested();
    bool onTokenReceived();
    bool onThreadCleanedUp(GFID fid);
    bool onThreadCompleted(GFID fid);
    bool onFamilyCompleted(GFID fid);
    Result onSharedRequested(const SharedInfo& sharedInfo);
    Result onSharedReceived(const SharedInfo& sharedInfo);

    Result onCycleReadPhase(unsigned int stateIndex);
    Result onCycleWritePhase(unsigned int stateIndex);

//private:
    Processor&      m_parent;
    RegisterFile&   m_regFile;
    FamilyTable&    m_familyTable;
    Allocator&      m_allocator;
    Network*        m_prev;
    Network*        m_next;

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
		BroadcastRegisters<std::pair<PID, RegValue> > value;

		GlobalInfo(Kernel& kernel) : value(kernel) {}
	};

	// Create information
    Register<std::pair<LFID, CreateMessage> > m_createLocal;
	Register<CreateMessage> m_createRemote;
	CreateState             m_createState;
	LFID                    m_createFid;
	RegIndex                m_globalsBase[NUM_REG_TYPES];
	GlobalInfo              m_global;

	// Notifications and reservations
    BroadcastRegisters<RemoteFID> m_reservation;
    BroadcastRegisters<RemoteFID> m_unreservation;
    Register<GFID>                m_completedFamily;
    Register<GFID>                m_completedThread;
    Register<GFID>                m_cleanedUpThread;

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

