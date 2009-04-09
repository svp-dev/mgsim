#include "Network.h"
#include "Allocator.h"
#include "Processor.h"
#include "FamilyTable.h"
#include <cassert>
using namespace Simulator;
using namespace std;

Network::Network(
    Processor&                parent,
    const string&             name,
    const vector<Processor*>& grid,
    LPID                      lpid,
    Allocator&                alloc,
    RegisterFile&             regFile,
    FamilyTable&              familyTable
) :
    IComponent(&parent, parent.GetKernel(), name, "registers-in|registers-out|completion|creation|token|rsync"),
    
    m_parent     (parent),
    m_regFile    (regFile),
    m_familyTable(familyTable),
    m_allocator  (alloc),
    
    m_prev(NULL),
    m_next(NULL),
    m_lpid(lpid),
    m_grid(grid),
    
	m_createLocal   (parent.GetKernel()),
	m_createRemote  (parent.GetKernel()),
	m_delegateLocal (parent.GetKernel()),
	m_delegateRemote(parent.GetKernel()),

    m_completedFamily(parent.GetKernel()),
    m_completedThread(parent.GetKernel()),
    m_cleanedUpThread(parent.GetKernel()),
    m_remoteSync     (parent.GetKernel()),
    
    p_registerResponseOut(parent.GetKernel()),
	
	m_hasToken      (parent.GetKernel(), lpid == 0), // CPU #0 starts out with the token
	m_wantToken     (parent.GetKernel(), false),
	m_nextWantsToken(parent.GetKernel(), false),
	m_requestedToken(parent.GetKernel(), false)
{
    m_registerRequestIn .addr.fid  = INVALID_LFID;
    m_registerRequestOut.addr.fid  = INVALID_LFID;
    m_registerResponseIn .addr.fid = INVALID_LFID;
    m_registerResponseOut.addr.fid = INVALID_LFID;
    m_registerValue.m_state        = RST_INVALID;
}

void Network::Initialize(Network& prev, Network& next)
{
    m_prev = &prev;
    m_next = &next;
}

bool Network::SendThreadCleanup   (LFID fid) { return m_next->OnThreadCleanedUp(fid); }
bool Network::SendThreadCompletion(LFID fid) { return m_prev->OnThreadCompleted(fid); }
bool Network::SendFamilyCompletion(LFID fid) { return m_next->OnFamilyCompleted(fid); }

// Called by the pipeline to send a register to the next CPU
bool Network::SendRegister(const RemoteRegAddr& addr, const RegValue& value)
{
    assert(addr.fid       != INVALID_LFID);
    assert(addr.reg.index != INVALID_REG_INDEX);
    assert(value.m_state  == RST_FULL);
    
    if (!p_registerResponseOut.Invoke())
    {
        return false;
    }
    
    if (m_registerResponseOut.addr.fid == INVALID_LFID)
    {
        COMMIT
        {
            m_registerResponseOut.addr  = addr;
            m_registerResponseOut.value = value;
        }
        return true;
    }
    return false;
}

// Called by the pipeline to request a register from the previous CPU
bool Network::RequestRegister(const RemoteRegAddr& addr, LFID fid_self)
{
    assert(fid_self       != INVALID_LFID);
    assert(addr.fid       != INVALID_LFID);
    assert(addr.reg.index != INVALID_REG_INDEX);
    
    if (m_registerRequestOut.addr.fid == INVALID_LFID)
    {
        COMMIT
        {
            m_registerRequestOut.addr       = addr;
            m_registerRequestOut.return_fid = fid_self;
        }
        
        DebugSimWrite("Requesting %s register %s in F%u",
            GetRemoteRegisterTypeString(addr.type),
            addr.reg.str().c_str(),
            addr.fid
        );
        return true;
    }
    return false;
}

// This CPU received a register request from it's next CPU
bool Network::OnRegisterRequested(const RegisterRequest& request)
{
    assert(request.return_fid     != INVALID_LFID);
    assert(request.addr.fid       != INVALID_LFID);
    assert(request.addr.reg.index != INVALID_REG_INDEX);
    
    if (m_registerRequestIn.addr.fid == INVALID_LFID)
    {	
	    DebugSimWrite("Received request for %s register %s in F%u",
	        GetRemoteRegisterTypeString(request.addr.type),
	        request.addr.reg.str().c_str(),
	        request.addr.fid
	    );
	    
		COMMIT { m_registerRequestIn = request; }
        return true;
    }
    return false;
}

// This CPU received a register from it's previous CPU
bool Network::OnRegisterReceived(const RegisterResponse& response)
{
    assert(response.addr.fid       != INVALID_LFID);
    assert(response.addr.reg.index != INVALID_REG_INDEX);
    assert(response.value.m_state  == RST_FULL);
    
    if (m_registerResponseIn.addr.fid == INVALID_LFID)
    {
	    DebugSimWrite("Received response for %s register %s in F%u",
	        GetRemoteRegisterTypeString(response.addr.type),
	        response.addr.reg.str().c_str(),
	        response.addr.fid
       );
       
		COMMIT{ m_registerResponseIn = response; }
		return true;
    }
    return false;
}

bool Network::OnThreadCleanedUp(LFID fid)
{
    assert(fid != INVALID_LFID);
    if (m_cleanedUpThread.IsEmpty())
    {
        DebugSimWrite("Received thread cleanup notification for F%u", fid);
        m_cleanedUpThread.Write(fid);
        return true;
    }
    return false;
}

bool Network::OnThreadCompleted(LFID fid)
{
    assert(fid != INVALID_LFID);
    if (m_completedThread.IsEmpty())
    {
        DebugSimWrite("Received thread completion notification for F%u", fid);
        m_completedThread.Write(fid);
        return true;
    }
    return false;
}

bool Network::OnFamilyCompleted(LFID fid)
{
    assert(fid != INVALID_LFID);
    if (m_completedFamily.IsEmpty())
    {
        DebugSimWrite("Received family completion notification for F%u", fid);
        m_completedFamily.Write(fid);
        return true;
    }
    return false;
}

bool Network::SendDelegatedCreate(LFID fid)
{
	if (!m_delegateLocal.IsFull())
    {
        COMMIT
        {
            // Buffer the family information
			const Family& family = m_familyTable[fid];

            DelegateMessage message;
            message.start      = family.start;
			message.limit      = family.limit;
			message.step       = family.step;
			message.blockSize  = family.virtBlockSize;
			message.address    = family.pc;
			message.parent.pid = m_parent.GetPID();
			message.parent.fid = fid;
			message.exclusive  = family.place.exclusive;

            for (RegType i = 0; i < NUM_REG_TYPES; i++)
            {
				message.regsNo[i] = family.regs[i].count;
			}

			GPID dest_pid = family.place.pid;
			assert(dest_pid != m_parent.GetPID());

			m_delegateLocal.Write(make_pair(dest_pid, message));
			DebugSimWrite("Delegating create for (F%u) to P%u", fid, dest_pid);
        }
        return true;
    }
    return false;
}

bool Network::SendGroupCreate(LFID fid)
{
	if (!m_createLocal.IsFull())
    {
        assert(m_hasToken.Read());
        COMMIT
        {
            // Buffer the family information
			const Family& family = m_familyTable[fid];

            assert(family.parent.lpid == m_lpid);
            
            CreateMessage message;
            message.first_fid     = fid;
			message.link_prev     = fid;
            message.infinite      = family.infinite;
            message.start         = family.start;
			message.step          = family.step;
			message.nThreads      = family.nThreads;
			message.virtBlockSize = family.virtBlockSize;
			message.physBlockSize = family.physBlockSize;
			message.address       = family.pc;
			message.parent.pid    = family.parent.lpid;
			message.parent.tid    = family.parent.tid;

            for (RegType i = 0; i < NUM_REG_TYPES; ++i)
            {
				message.regsNo[i] = family.regs[i].count;
			}

			m_createLocal.Write(message);
			DebugSimWrite("Broadcasting group create", fid);
        }
        return true;
    }
    return false;
}

bool Network::SendRemoteSync(GPID pid, LFID fid, ExitCode code)
{
    assert(pid != INVALID_GPID);
    assert(fid != INVALID_LFID);
    
    if (m_remoteSync.IsFull())
    {
        return false;
    }
    
    RemoteSync rs;
    rs.pid  = pid;
    rs.fid  = fid;
    rs.code = code;
    m_remoteSync.Write(rs);
    DebugSimWrite("Sending remote sync to F%u@P%u; code=%d", fid, pid, code);
    return true;
}

bool Network::OnDelegationCreateReceived(const DelegateMessage& msg)
{
    // The delegation should come from a different processor
    assert(msg.parent.pid != m_parent.GetPID());
    
    if (m_delegateRemote.IsFull())
	{
		return false;
	}

	m_delegateRemote.Write(msg);
	DebugSimWrite("Received delegated create from F%u@P%u", msg.parent.fid, msg.parent.pid);
    return true;
}

bool Network::OnGroupCreateReceived(const CreateMessage& msg)
{
    if (msg.parent.pid != m_lpid)
    {
        // The create hasn't made a full circle yet,
        // store it for processing and forwarding.
        if (m_createRemote.IsFull())
	    {
    		return false;
    	}
    
    	m_createRemote.Write(msg);
    	DebugSimWrite("Received group create");
    }
    else
    {
        // The create has come back to the creating CPU.
        // Link the family entry to the one on the previous CPU.
        if (!m_allocator.SetupFamilyPrevLink(msg.first_fid, msg.link_prev))
        {
            return false;
        }
    }
    return true;
}

/// Called by the Allocator when it wants to do a group create
bool Network::RequestToken()
{
    m_wantToken.Write(true);
    return true;
}

bool Network::OnTokenReceived()
{
    m_hasToken.Write(true);
	m_requestedToken.Write(false);
    return true;
}

bool Network::OnRemoteTokenRequested()
{
    m_nextWantsToken.Write(true);
    return true;
}

bool Network::OnRemoteSyncReceived(LFID fid, ExitCode code)
{
    DebugSimWrite("Received remote sync for F%u; code: %d", fid, code);
    if (!m_allocator.OnRemoteSync(fid, code))
    {
        return false;
    }
    return true;
}

bool Network::SetupFamilyNextLink(LFID fid, LFID link_next)
{
    if (!m_allocator.SetupFamilyNextLink(fid, link_next))
    {
        return false;
    }
    return true;
}

Result Network::OnCycleReadPhase(unsigned int stateIndex)
{
    switch (stateIndex)
    {
    case 1:
        // Read a shared to send, if there is a request and no response yet
        if (m_registerRequestIn.addr.fid != INVALID_LFID)
        {
			RegAddr addr = m_allocator.GetRemoteRegisterAddress(m_registerRequestIn.addr);
            if (addr.valid())
            {
                // The thread of the register has been allocated, read it
                if (!m_regFile.p_asyncR.Read())
                {
                    return FAILED;
                }

                RegValue value;
                if (!m_regFile.ReadRegister(addr, value))
                {
                    return FAILED;
                }
                assert(value.m_state != RST_INVALID);
                if (value.m_state != RST_FULL)
                {
       			    assert(value.m_remote.reg.fid == INVALID_LFID);
       			}

                DebugSimWrite("Read %s register %s in F%u from %s",
                    GetRemoteRegisterTypeString(m_registerRequestIn.addr.type),
                    m_registerRequestIn.addr.reg.str().c_str(),
                    m_registerRequestIn.addr.fid,
                    addr.str().c_str()
                );
                
                COMMIT{ m_registerValue = value; }
            }
			else
			{
			    DebugSimWrite("Discarding request for %s register %s in F%u: register not yet allocated",
        	        GetRemoteRegisterTypeString(m_registerRequestIn.addr.type),
			        m_registerRequestIn.addr.reg.str().c_str(),
			        m_registerRequestIn.addr.fid
			    );
			    
    			COMMIT{ m_registerRequestIn.addr.fid = INVALID_LFID; }
	        }
			return SUCCESS;
        }
        break;
    }
    return DELAYED;
}

Result Network::OnCycleWritePhase(unsigned int stateIndex)
{
    switch (stateIndex)
    {
    case 0:
        // Incoming register channel
        if (m_registerResponseIn.addr.fid != INVALID_LFID)
        {
			const Family& family = m_familyTable[m_registerResponseIn.addr.fid];
            if (m_registerResponseIn.addr.type == RRT_PARENT_SHARED && family.parent.lpid != m_lpid)
            {
                // This is not the CPU the parent thread is located on. Forward it
                RemoteRegAddr addr(m_registerResponseIn.addr);
                addr.fid = family.link_next;
                if (!SendRegister(addr, m_registerResponseIn.value))
                {
                    DeadlockWrite("Unable to forward parent shared");
                    return FAILED;
                }
            }
            else
            {
                DebugSimWrite("Writing %s register %s in F%u",
        	        GetRemoteRegisterTypeString(m_registerResponseIn.addr.type),
                    m_registerResponseIn.addr.reg.str().c_str(),
                    m_registerResponseIn.addr.fid
                );
                    
                RegAddr addr = m_allocator.GetRemoteRegisterAddress(m_registerResponseIn.addr);
                if (addr.valid())
                {
                    // Write it
                    if (!m_regFile.p_asyncW.Write(addr))
                    {
                        DeadlockWrite("Unable to acquire port to write register response to %s", addr.str().c_str());
                        return FAILED;
                    }
                    
                    if (!m_regFile.WriteRegister(addr, m_registerResponseIn.value, false))
                    {
                        DeadlockWrite("Unable to write register response to %s", addr.str().c_str());
                        return FAILED;
                    }
                    
    				if (m_registerResponseIn.addr.type == RRT_PARENT_SHARED)
	    			{
		    			if (!m_allocator.DecreaseFamilyDependency(m_registerResponseIn.addr.fid, FAMDEP_OUTSTANDING_SHAREDS))
			    		{
			    		    DeadlockWrite("Unable to decrease outstanding shareds for F%u on writeback", m_registerResponseIn.addr.fid);
				    		return FAILED;
					    }
    				}
                }
            }

            // We've processed this register response
            COMMIT{ m_registerResponseIn.addr.fid = INVALID_LFID; }
			return SUCCESS;
        }
        break;

    case 1:
        // Outgoing register channel
        
        // There's either a register sent, or an incoming register request has been read
        if (m_registerResponseOut.addr.fid != INVALID_LFID ||
           (m_registerRequestIn.addr.fid != INVALID_LFID && m_registerValue.m_state != RST_INVALID))
        {
            // Outgoing response
            if (m_registerResponseOut.addr.fid == INVALID_LFID)
            {
                // No outgoing register, so send the request that we've read
                
                // Construct return address from request information
                RemoteRegAddr return_addr;
			    return_addr.type = (m_registerRequestIn.addr.type == RRT_GLOBAL) ? RRT_GLOBAL : RRT_FIRST_DEPENDENT;
			    return_addr.fid  = m_registerRequestIn.return_fid;
    		    return_addr.reg  = m_registerRequestIn.addr.reg;
                
				if (m_registerValue.m_state == RST_FULL)
				{
				    // Create response
                    RegisterResponse response;
				    response.addr  = return_addr;
					response.value = m_registerValue;

			        if (!m_next->OnRegisterReceived(response))
                    {
                        DeadlockWrite("Unable to send response to register request to next processor");
                        return FAILED;
                    }
				}
				else
				{
				    // Write back a remote waiting state
        			RegAddr addr = m_allocator.GetRemoteRegisterAddress(m_registerRequestIn.addr);
        			assert(addr.valid());
        			assert(m_registerValue.m_remote.reg.fid == INVALID_LFID);
        			
                    if (!m_regFile.p_asyncW.Write(addr))
                    {
                        DeadlockWrite("Unable to acquire port to write to register %s to mark as remote waiting", addr.str().c_str());
                        return FAILED;
                    }
                    
				    RegValue value(m_registerValue);
			        value.m_state      = RST_WAITING;
				    value.m_remote.pid = INVALID_GPID;
				    value.m_remote.reg = return_addr;
				    
                    if (!m_regFile.WriteRegister(addr, value, false))
                    {
                        DeadlockWrite("Unable to write register %s to mark as remote waiting", addr.str().c_str());
                        return FAILED;
                    }

	                DebugSimWrite("Writing remote wait to %s register %s in F%u",
	                    GetRemoteRegisterTypeString(m_registerRequestIn.addr.type),
	                    m_registerRequestIn.addr.reg.str().c_str(),
        	            m_registerRequestIn.addr.fid
                    );
				}
				
				COMMIT {
				    m_registerRequestIn.addr.fid = INVALID_LFID;
				    m_registerValue.m_state = RST_INVALID;
				}
	        }
	        else
	        {
                assert(m_registerResponseOut.value.m_state == RST_FULL);
            
                DebugSimWrite("Sending %s register %s in F%u",
    	            GetRemoteRegisterTypeString(m_registerResponseOut.addr.type),
                    m_registerResponseOut.addr.reg.str().c_str(),
                    m_registerResponseOut.addr.fid
                );
            
			    if (!m_next->OnRegisterReceived(m_registerResponseOut))
                {
                    DeadlockWrite("Unable to send register response to next processor");
                    return FAILED;
                }

                // We've sent this register response
			    COMMIT{ m_registerResponseOut.addr.fid = INVALID_LFID; }
			}
			return SUCCESS;
		}
        
		if (m_registerRequestOut.addr.fid != INVALID_LFID)
        {
            // Outgoing request
            if (!m_prev->OnRegisterRequested(m_registerRequestOut))
            {
                DeadlockWrite("Unable to send register request to previous processor");
                return FAILED;
            }
            
            // We've sent this register request
            COMMIT{ m_registerRequestOut.addr.fid = INVALID_LFID; }
			return SUCCESS;
        }
        break;

    case 2:
        // Thread and family completion channel
        if (m_cleanedUpThread.IsFull())
        {
            LFID fid = m_cleanedUpThread.Read();
            if (!m_allocator.OnRemoteThreadCleanup(fid))
            {
                DeadlockWrite("Unable to mark thread cleanup on F%u", fid);
                return FAILED;
            }
            m_cleanedUpThread.Clear();
			return SUCCESS;
        }
        
		if (m_completedThread.IsFull())
        {
            LFID fid = m_completedThread.Read();
            if (!m_allocator.OnRemoteThreadCompletion(fid))
            {
                DeadlockWrite("Unable to mark thread completion on F%u", fid);
                return FAILED;
            }
            m_completedThread.Clear();
			return SUCCESS;
        }
        
		if (m_completedFamily.IsFull())
        {
            LFID fid = m_completedFamily.Read();
			if (!m_allocator.DecreaseFamilyDependency(fid, FAMDEP_PREV_TERMINATED))
            {
                DeadlockWrite("Unable to mark family termination on F%u", fid);
                return FAILED;
            }
            m_completedFamily.Clear();
			return SUCCESS;
        }
        break;

    case 3:
    	// We're not processing a create, check if there is a create
		if (m_delegateRemote.IsFull())
		{
		    // Process the received delegation
			const DelegateMessage& msg = m_delegateRemote.Read();
				
			if (!m_allocator.OnDelegatedCreate(msg))
			{
			    DeadlockWrite("Unable to process received delegation create");
				return FAILED;
			}

			m_delegateRemote.Clear();
		    return SUCCESS;
		}

		if (m_delegateLocal.IsFull())
		{
		    // Process the outgoing delegation
			const pair<GPID, DelegateMessage>& delegate = m_delegateLocal.Read();
			const GPID             pid = delegate.first;
			const DelegateMessage& msg = delegate.second;
				
			// Send the create
			if (!m_grid[pid]->GetNetwork().OnDelegationCreateReceived(msg))
			{
			    DeadlockWrite("Unable to send delegation create to P%u", pid);
				return FAILED;
			}

            DebugSimWrite("Sent delegated family create to P%u", pid);
			m_delegateLocal.Clear();
		    return SUCCESS;
		}
			
		if (m_createRemote.IsFull())
		{
			// Process the received create
			CreateMessage msg = m_createRemote.Read();

            // Determine the next link
            LFID link_next = (m_next->m_lpid != msg.parent.pid) ? INVALID_LFID : msg.first_fid;
            
            // Process the create
            LFID fid = m_allocator.OnGroupCreate(msg, link_next);
            if (fid == INVALID_LFID)
		    {
		        DeadlockWrite("Unable to process received group create");
   				return FAILED;
		    }
		    
		    // Send the FID back to the previous processor
		    if (!m_prev->SetupFamilyNextLink(msg.link_prev, fid))
		    {
		        DeadlockWrite("Unable to setup F%u's next link to F%u", fid, msg.link_prev);
		        return FAILED;
		    }
            
            // Forward the create
  			COMMIT{ msg.link_prev = fid; }

		    if (!m_next->OnGroupCreateReceived(msg))
		    {
   			    DeadlockWrite("Unable to forward group create to next processor");
			    return FAILED;
			}

			m_createRemote.Clear();
			return SUCCESS;
		}

		if (m_createLocal.IsFull())
		{
			const CreateMessage& msg = m_createLocal.Read();

			// Send the create
			if (!m_next->OnGroupCreateReceived(msg))
			{
			    DeadlockWrite("Unable to send group create to next processor");
				return FAILED;
			}

			m_createLocal.Clear();
			return SUCCESS;
		}
        break;

    case 4:
		if (m_hasToken.Read())
		{
			// We have the token
			if (m_wantToken.Read())
			{
				// We want it as well, approve the Create
				if (!m_allocator.OnTokenReceived())
				{
				    DeadlockWrite("Unable to continue group create with token");
					return FAILED;
				}
				m_wantToken.Write(false);
				return SUCCESS;
			}

			if (m_nextWantsToken.Read())
			{
				// Pass the token to the next CPU
				DebugSimWrite("Sending token to next processor");
				if (!m_next->OnTokenReceived())
				{
				    DeadlockWrite("Unable to send token to next processor");
					return FAILED;
				}
				m_nextWantsToken.Write(false);
				m_hasToken.Write(false);
				return SUCCESS;
			}
		}
		// We don't have the token
		else if ((m_wantToken.Read() || m_nextWantsToken.Read()) && !m_requestedToken.Read())
		{
			// But we, or m_next, wants it, so request it.
			if (!m_prev->OnRemoteTokenRequested())
			{
			    DeadlockWrite("Unable to request token from previous processor");
				return FAILED;
			}
			// Set a flag to prevent us from spamming the previous CPU
			m_requestedToken.Write(true);
			return SUCCESS;
		}
        break;
    
    case 5:
        // Send the remote sync
        if (m_remoteSync.IsFull())
        {
            const RemoteSync& rs = m_remoteSync.Read();
            
            if (!m_grid[rs.pid]->GetNetwork().OnRemoteSyncReceived(rs.fid, rs.code))
            {
                DeadlockWrite("Unable to send remote sync for F%u to P%u", rs.fid, rs.pid);
                return FAILED;
            }
            
            m_remoteSync.Clear();
            return SUCCESS;
        }
        break;
    }

	// Nothing to do
    return DELAYED;
}

