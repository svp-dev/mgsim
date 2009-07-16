#include "Network.h"
#include "Allocator.h"
#include "Processor.h"
#include "FamilyTable.h"
#include <cassert>
#include <iostream>
using namespace std;

namespace Simulator
{

#define CONSTRUCT_REGISTER(name, state)       name(parent.GetKernel(), *this, state,  #name)
#define CONSTRUCT_FLAG(name, state, val)      name(parent.GetKernel(), *this, state,  #name, val)
#define CONSTRUCT_REGISTER_PAIR(name, s1, s2) name(parent.GetKernel(), *this, s1, s2, #name)

Network::Network(
    Processor&                parent,
    const string&             name,
    const vector<Processor*>& grid,
    LPID                      lpid,
    Allocator&                alloc,
    RegisterFile&             regFile,
    FamilyTable&              familyTable
) :
    IComponent(&parent, parent.GetKernel(), name, "reg-response-in|reg-request-in|reg-response-group-out|reg-request-group-out|delegation|creation|rsync|thread-cleanup|thread-termination|family-sync|family-termination|reg-request-remote-out|reg-response-remote-out"),
    
    m_parent     (parent),
    m_regFile    (regFile),
    m_familyTable(familyTable),
    m_allocator  (alloc),
    
    m_prev(NULL),
    m_next(NULL),
    m_lpid(lpid),
    m_grid(grid),
    
    CONSTRUCT_REGISTER(m_createLocal,    5),
    CONSTRUCT_REGISTER(m_createRemote,   5),
    CONSTRUCT_REGISTER(m_delegateLocal,  4),
    CONSTRUCT_REGISTER(m_delegateRemote, 4),

    CONSTRUCT_REGISTER(m_synchronizedFamily, 9),
    CONSTRUCT_REGISTER(m_terminatedFamily,  10),
    CONSTRUCT_REGISTER(m_completedThread,    8),
    CONSTRUCT_REGISTER(m_cleanedUpThread,    7),
    CONSTRUCT_REGISTER(m_remoteSync,         6),
    
    CONSTRUCT_REGISTER_PAIR(m_registerRequestRemote,  1, 11),
    CONSTRUCT_REGISTER_PAIR(m_registerResponseRemote, 0, 12),
    CONSTRUCT_REGISTER_PAIR(m_registerRequestGroup,   1, 3),
    CONSTRUCT_REGISTER_PAIR(m_registerResponseGroup,  0, 2),
	
    m_hasToken      (parent.GetKernel(),           lpid == 0), // CPU #0 starts out with the token
    m_wantToken     (parent.GetKernel(), *this, 5, false),
    m_tokenUsed     (parent.GetKernel(),           true),
    m_nextWantsToken(parent.GetKernel(), *this, 5, false),
    m_requestedToken(parent.GetKernel(),           false)
{
}

void Network::Initialize(Network& prev, Network& next)
{
    m_prev = &prev;
    m_next = &next;
}

bool Network::SendThreadCleanup   (LFID fid) { return m_next->OnThreadCleanedUp(fid); }
bool Network::SendThreadCompletion(LFID fid) { return m_prev->OnThreadCompleted(fid); }

bool Network::SendFamilySynchronization(LFID fid)
{
    assert(fid != INVALID_LFID);
    if (m_synchronizedFamily.Write(fid))
    {
        DebugSimWrite("Stored family synchronization for F%u", (unsigned)fid);
        return true;
    }
    return false;
}

bool Network::SendFamilyTermination(LFID fid)
{
    assert(fid != INVALID_LFID);
    if (m_terminatedFamily.ForceWrite(fid))
    {
        DebugSimWrite("Stored family termination for F%u", (unsigned)fid);
        return true;
    }
    return false;
}

bool Network::SendRegister(const RemoteRegAddr& addr, const RegValue& value)
{
    assert(addr.fid       != INVALID_LFID);
    assert(addr.reg.index != INVALID_REG_INDEX);
    assert(value.m_state  == RST_FULL);

    RegisterResponse response;
    response.addr  = addr;
    response.value = value;
    
    if (addr.pid == INVALID_GPID)
    {    
        if (!m_registerResponseGroup.out.Write(response))
        {
            return false;
        }
    }
    else
    {
        if (!m_registerResponseRemote.out.Write(response))
        {
            return false;
        }
    }
    return true;
}

bool Network::RequestRegister(const RemoteRegAddr& addr, LFID fid_self)
{
    assert(fid_self       != INVALID_LFID);
    assert(addr.fid       != INVALID_LFID);
    assert(addr.reg.index != INVALID_REG_INDEX);
    
    RegisterRequest req;
    req.addr       = addr;
    req.return_fid = fid_self;
    
    if (addr.pid == INVALID_GPID)
    {
        req.return_pid = INVALID_GPID;
        if (m_registerRequestGroup.out.Write(req))
        {        
            DebugSimWrite("Requesting %s register %s in F%u",
                GetRemoteRegisterTypeString(addr.type),
                addr.reg.str().c_str(),
                (unsigned)addr.fid
            );
            return true;
        }
    }
    else
    {
        req.return_pid = m_parent.GetPID();
        if (m_registerRequestRemote.out.Write(req))
        {
            DebugSimWrite("Requesting remote %s register %s in F%u",
                GetRemoteRegisterTypeString(addr.type),
                addr.reg.str().c_str(),
                (unsigned)addr.fid
            );
            return true;
        }
    }
    return false;
}

// Called by a distant core to request a register for a delegated create
bool Network::OnRemoteRegisterRequested(const RegisterRequest& request)
{
    assert(request.return_fid     != INVALID_LFID);
    assert(request.return_pid     != INVALID_GPID);
    assert(request.addr.pid       == m_parent.GetPID());
    assert(request.addr.fid       != INVALID_LFID);
    assert(request.addr.reg.index != INVALID_REG_INDEX);

    if (m_registerRequestRemote.in.Write(request))
    {
        DebugSimWrite("Requesting remote %s register %s in F%u from F%u@P%u",
            GetRemoteRegisterTypeString(request.addr.type),
            request.addr.reg.str().c_str(),
            (unsigned)request.addr.fid, (unsigned)request.return_fid, (unsigned)request.return_pid
        );
        return true;
    }
    return false;    
}

// This CPU received a register from a remote CPU
bool Network::OnRemoteRegisterReceived(const RegisterResponse& response)
{
    assert(response.addr.pid       == m_parent.GetPID());
    assert(response.addr.fid       != INVALID_LFID);
    assert(response.addr.reg.index != INVALID_REG_INDEX);
    assert(response.value.m_state  == RST_FULL);
    
	if (m_registerResponseRemote.in.Write(response))
	{
	    DebugSimWrite("Received remote response for %s register %s in F%u",
	        GetRemoteRegisterTypeString(response.addr.type),
	        response.addr.reg.str().c_str(),
	        (unsigned)response.addr.fid
        );       
		return true;
    }
    return false;
}

// This CPU received a register request from it's next CPU
bool Network::OnRegisterRequested(const RegisterRequest& request)
{
    assert(request.return_pid     == INVALID_GPID);
    assert(request.return_fid     != INVALID_LFID);
    assert(request.addr.pid       == INVALID_GPID);
    assert(request.addr.fid       != INVALID_LFID);
    assert(request.addr.reg.index != INVALID_REG_INDEX);
    
    if (m_registerRequestGroup.in.Write(request))
    {	
	    DebugSimWrite("Received request for %s register %s in F%u from F%u@next",
	        GetRemoteRegisterTypeString(request.addr.type),
	        request.addr.reg.str().c_str(),
	        (unsigned)request.addr.fid, (unsigned)request.return_fid
	    );
        return true;
    }
    return false;
}

// This CPU received a register from it's previous CPU
bool Network::OnRegisterReceived(const RegisterResponse& response)
{
    assert(response.addr.pid       == INVALID_GPID);
    assert(response.addr.fid       != INVALID_LFID);
    assert(response.addr.reg.index != INVALID_REG_INDEX);
    assert(response.value.m_state  == RST_FULL);
    
    if (m_registerResponseGroup.in.Write(response))
    {
	    DebugSimWrite("Received response for %s register %s in F%u",
	        GetRemoteRegisterTypeString(response.addr.type),
	        response.addr.reg.str().c_str(),
	        (unsigned)response.addr.fid
        );
		return true;
    }
    return false;
}

bool Network::OnThreadCleanedUp(LFID fid)
{
    assert(fid != INVALID_LFID);
    if (m_cleanedUpThread.Write(fid))
    {
        DebugSimWrite("Received thread cleanup notification for F%u", (unsigned)fid);
        return true;
    }
    return false;
}

bool Network::OnThreadCompleted(LFID fid)
{
    assert(fid != INVALID_LFID);
    if (m_completedThread.Write(fid))
    {
        DebugSimWrite("Received thread completion notification for F%u", (unsigned)fid);
        return true;
    }
    return false;
}

bool Network::OnFamilySynchronized(LFID fid)
{
    if (!m_allocator.DecreaseFamilyDependency(fid, FAMDEP_PREV_SYNCHRONIZED))
    {
        DeadlockWrite("Unable to mark family synchronization on F%u", (unsigned)fid);
        return false;
    }
    return true;
}

bool Network::OnFamilyTerminated(LFID fid)
{
    assert(fid != INVALID_LFID);
    if (!m_allocator.DecreaseFamilyDependency(fid, FAMDEP_NEXT_TERMINATED))
    {
        DeadlockWrite("Unable to mark family termination on F%u", (unsigned)fid);
        return false;
    }
    return true;
}

bool Network::SendDelegatedCreate(LFID fid)
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
		
    if (m_delegateLocal.Write(make_pair(dest_pid, message)))
    {
		DebugSimWrite("Delegating create for (F%u) to P%u", (unsigned)fid, (unsigned)dest_pid);
        return true;
    }
    return false;
}

bool Network::SendGroupCreate(LFID fid)
{
 	const Family& family = m_familyTable[fid];
    assert(m_hasToken.IsSet());
    assert(!m_tokenUsed.IsSet());   
    assert(family.parent.lpid == m_lpid);
            
    // Buffer the family information
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
	message.parent.gpid   = family.parent.gpid;
	message.parent.lpid   = family.parent.lpid;
	message.parent.tid    = family.parent.tid;

    for (RegType i = 0; i < NUM_REG_TYPES; ++i)
    {
		message.regsNo[i] = family.regs[i].count;
	}
		
    m_tokenUsed.Set();
    
    if (!m_createLocal.Write(message))
    {
        return false;
    }
    
    DebugSimWrite("Broadcasting group create for F%u", (unsigned)fid);
    return true;
}

bool Network::SendRemoteSync(GPID pid, LFID fid, ExitCode code)
{
    assert(pid != INVALID_GPID);
    assert(fid != INVALID_LFID);
    
    RemoteSync rs;
    rs.pid  = pid;
    rs.fid  = fid;
    rs.code = code;
    
    if (m_remoteSync.Write(rs))
    {
        DebugSimWrite("Sending remote sync to F%u@P%u; code=%u", (unsigned)fid, (unsigned)pid, (unsigned)code);
        return true;
    }
    return false;
}

bool Network::OnDelegationCreateReceived(const DelegateMessage& msg)
{
    if (m_delegateRemote.Write(msg))
    {
    	DebugSimWrite("Received delegated create from F%u@P%u", (unsigned)msg.parent.fid, (unsigned)msg.parent.pid);
        return true;
    }
    return false;
}

bool Network::OnGroupCreateReceived(const CreateMessage& msg)
{
    if (m_createRemote.Write(msg))
    {
        DebugSimWrite("Received group create");
        return true;
    }
    return false;
}

/// Called by the Allocator when it wants to do a group create
bool Network::RequestToken()
{
    m_wantToken.Set();
	m_tokenUsed.Clear();
    return true;
}

bool Network::OnTokenReceived()
{
    m_hasToken.Set();
	m_requestedToken.Clear();
    return true;
}

bool Network::OnRemoteTokenRequested()
{
    m_nextWantsToken.Set();
    return true;
}

bool Network::OnRemoteSyncReceived(LFID fid, ExitCode code)
{
    DebugSimWrite("Received remote sync for F%u; code: %u", (unsigned)fid, (unsigned)code);
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

Result Network::OnCycle(unsigned int stateIndex)
{
    switch (stateIndex)
    {
    case 0:
    {
        // Incoming group/remote responses
        Register<RegisterResponse>* reg;
        if (!m_registerResponseGroup.in.Empty()) {
            reg = &m_registerResponseGroup.in;
        } else {
            assert(!m_registerResponseRemote.in.Empty());
            reg = &m_registerResponseRemote.in;
        }
        
        assert(reg != NULL);
        if (reg != NULL)
        {
            const RegisterResponse& response = reg->Read();
			const Family& family = m_familyTable[response.addr.fid];
            if (response.addr.type == RRT_PARENT_SHARED && family.parent.lpid != m_lpid)
            {
                // This is not the CPU the parent thread is located on. Forward it
                RemoteRegAddr addr(response.addr);
                addr.fid = family.link_next;
                if (!SendRegister(addr, response.value))
                {
                    DeadlockWrite("Unable to forward parent shared");
                    return FAILED;
                }
            }
            else
            {
                DebugSimWrite("Writing %s register %s in F%u",
        	        GetRemoteRegisterTypeString(response.addr.type),
                    response.addr.reg.str().c_str(),
                    (unsigned)response.addr.fid
                );
                    
                RegAddr addr = m_allocator.GetRemoteRegisterAddress(response.addr);
                if (addr.valid())
                {
                    // Write it
                    if (!m_regFile.p_asyncW.Write(addr))
                    {
                        DeadlockWrite("Unable to acquire port to write register response to %s", addr.str().c_str());
                        return FAILED;
                    }
                    
                    if (!m_regFile.WriteRegister(addr, response.value, false))
                    {
                        DeadlockWrite("Unable to write register response to %s", addr.str().c_str());
                        return FAILED;
                    }
                    
    				if (response.addr.type == RRT_PARENT_SHARED)
	    			{
		    			if (!m_allocator.DecreaseFamilyDependency(response.addr.fid, FAMDEP_OUTSTANDING_SHAREDS))
			    		{
			    		    DeadlockWrite("Unable to decrease outstanding shareds for F%u on writeback", (unsigned)response.addr.fid);
				    		return FAILED;
					    }
    				}
                }
            }

            // We've processed this register response
            reg->Clear();
        }
    	return SUCCESS;
    }
    
    case 1:
    {
        // Incoming group/remote requests
        Register<RegisterRequest>* reg = NULL;
        if (!m_registerRequestGroup.in.Empty()) {
            reg = &m_registerRequestGroup.in;
        } else {
            assert (!m_registerRequestRemote.in.Empty());
            reg = &m_registerRequestRemote.in;
        }
        
        assert(reg != NULL);
        {
            const RegisterRequest& request = reg->Read();
			RegAddr addr = m_allocator.GetRemoteRegisterAddress(request.addr);
            if (!addr.valid())
			{
			    // The source thread hasn't been allocated yet.
			    // This shouldn't happen for remote requests -- they must always hit the parent
			    // which should always be allocated.
			    assert(reg != &m_registerRequestRemote.in);
			    
			    DebugSimWrite("Discarding request for %s register %s in F%u: register not yet allocated",
        	        GetRemoteRegisterTypeString(request.addr.type),
			        request.addr.reg.str().c_str(),
			        (unsigned)request.addr.fid
			    );
			    
			    reg->Clear();
			    return SUCCESS;
	        }
	        
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
                
            if (value.m_state != RST_FULL && value.m_remote.fid != INVALID_LFID)
            {
                // We can only have one remote request waiting on a register
    	        DebugSimWrite("Discarding request for %s register %s in F%u: register has already been requested",
       	            GetRemoteRegisterTypeString(request.addr.type),
		            request.addr.reg.str().c_str(),
		            (unsigned)request.addr.fid
	            );
			    
       			reg->Clear();
       			return SUCCESS;
            }
            
            DebugSimWrite("Read %s register %s in F%u from %s",
                GetRemoteRegisterTypeString(request.addr.type),
                request.addr.reg.str().c_str(),
                (unsigned)request.addr.fid,
                addr.str().c_str()
            );
                    
            // Construct return address from request information
            RemoteRegAddr return_addr;
            return_addr.type = (request.addr.type == RRT_GLOBAL) ? RRT_GLOBAL : RRT_FIRST_DEPENDENT;
    		return_addr.pid  = request.return_pid;
		    return_addr.fid  = request.return_fid;
    		return_addr.reg  = request.addr.reg;
                
			if (value.m_state == RST_FULL)
			{
			    // Create response and we're done
			    if (!SendRegister(return_addr, value))
        	    {
                    DeadlockWrite("Unable to send response to register request");
		            return FAILED;
                }
                
                reg->Clear();
                return SUCCESS;
            }
            
			// Write back a remote waiting state
        	assert(value.m_remote.fid == INVALID_LFID);
        			
            if (!m_regFile.p_asyncW.Write(addr))
            {
                DeadlockWrite("Unable to acquire port to write to register %s to mark as remote waiting", addr.str().c_str());
                return FAILED;
            }
                    
			value.m_remote = return_addr;				    
            if (!m_regFile.WriteRegister(addr, value, false))
            {
                DeadlockWrite("Unable to write register %s to mark as remote waiting", addr.str().c_str());
                return FAILED;
            }

	        DebugSimWrite("Writing remote wait to %s register %s in F%u",
	            GetRemoteRegisterTypeString(request.addr.type),
	            request.addr.reg.str().c_str(),
        	    (unsigned)request.addr.fid
            );
                    
            if (request.addr.type == RRT_GLOBAL)
            {
                // Check to forward the request
                const Family& family = m_familyTable[request.addr.fid];
                        
                if (family.parent.lpid != m_lpid)
                {
                    // We're not on the parent processor, forward on the ring
                    RemoteRegAddr forward = request.addr;
                    forward.fid = family.link_prev;
                    if (!RequestRegister(forward, request.addr.fid))
                    {
                        DeadlockWrite("Unable to forward request");
                        return FAILED;
                    }

                    DebugSimWrite("Forwarded remote request to %s register %s in F%u",
                        GetRemoteRegisterTypeString(request.addr.type),
                        request.addr.reg.str().c_str(),
                        (unsigned)request.addr.fid
                    );
                }
                else if (family.parent.gpid != INVALID_GPID)
                {
                    // This is a delegated create; forward the request to the creating core
                    RemoteRegAddr forward = request.addr;
                    forward.pid = family.parent.gpid;
                    forward.fid = family.parent.fid;

                    if (!RequestRegister(forward, request.addr.fid))
                    {                            
                        DeadlockWrite("Unable to forward request to remote place");
                        return FAILED;
                    }

                    DebugSimWrite("Forwarded remote request to %s register %s in F%u to F%u@P%u",
                        GetRemoteRegisterTypeString(request.addr.type),
                        request.addr.reg.str().c_str(),
                        (unsigned)request.addr.fid,
                        (unsigned)family.parent.fid,
                        (unsigned)family.parent.gpid
                    );
                }
		    }
				
			reg->Clear();
			return SUCCESS;
        }        			
		break;
    }
    
    case 2:
        // Outgoing group responses
        assert(!m_registerResponseGroup.out.Empty());
        {
	        const RegisterResponse& response = m_registerResponseGroup.out.Read();
            assert(response.value.m_state == RST_FULL);
            
            DebugSimWrite("Sending %s register %s in F%u",
    	        GetRemoteRegisterTypeString(response.addr.type),
                response.addr.reg.str().c_str(),
                (unsigned)response.addr.fid
            );
            
			if (!m_next->OnRegisterReceived(response))
            {
                DeadlockWrite("Unable to send register response to next processor");
                return FAILED;
            }

            // We've sent this register response
            m_registerResponseGroup.out.Clear();
            return SUCCESS;
        }
        break;
        
    case 3:
        // Outgoing group requests
		assert(!m_registerRequestGroup.out.Empty());
        {
            if (!m_prev->OnRegisterRequested(m_registerRequestGroup.out.Read()))
            {
                DeadlockWrite("Unable to send register request to previous processor");
                return FAILED;
            }
            
            // We've sent this register request
            m_registerRequestGroup.out.Clear();
			return SUCCESS;
        }
        break;

    case 12:
        // Outgoing remote responses
        assert(!m_registerResponseRemote.out.Empty());
        {
	        const RegisterResponse& response = m_registerResponseRemote.out.Read();
            assert(response.value.m_state == RST_FULL);
            
            DebugSimWrite("Sending %s register %s in F%u to P%u",
    	        GetRemoteRegisterTypeString(response.addr.type),
                response.addr.reg.str().c_str(),
                (unsigned)response.addr.fid,
                (unsigned)response.addr.pid
            );
            
			if (!m_grid[response.addr.pid]->GetNetwork().OnRemoteRegisterReceived(response))
            {
                DeadlockWrite("Unable to send register response to P%u", (unsigned)response.addr.pid);
                return FAILED;
            }

            // We've sent this register response
            m_registerResponseRemote.out.Clear();
            return SUCCESS;
        }
        break;
        
    case 11:
        // Outgoing remote requests
		assert(!m_registerRequestRemote.out.Empty());
        {
	        const RegisterRequest& request = m_registerRequestRemote.out.Read();
            if (!m_grid[request.addr.pid]->GetNetwork().OnRemoteRegisterRequested(request))
            {
                DeadlockWrite("Unable to send register request to P%u", (unsigned)request.addr.pid);
                return FAILED;
            }
            
            // We've sent this register request
            m_registerRequestRemote.out.Clear();
			return SUCCESS;
        }
        break;

    case 4:
    	// We're not processing a delegated create, check if there is a create
		if (!m_delegateRemote.Empty())
		{
		    // Process the received delegation
			if (!m_allocator.OnDelegatedCreate(m_delegateRemote.Read()))
			{
			    DeadlockWrite("Unable to process received delegation create");
				return FAILED;
			}

			m_delegateRemote.Clear();
		    return SUCCESS;
		}

		assert(!m_delegateLocal.Empty());
		{
		    // Process the outgoing delegation
			const pair<GPID, DelegateMessage>& delegate = m_delegateLocal.Read();
			const GPID             pid = delegate.first;
			const DelegateMessage& msg = delegate.second;
				
			// Send the create
			if (!m_grid[pid]->GetNetwork().OnDelegationCreateReceived(msg))
			{
			    DeadlockWrite("Unable to send delegation create to P%u", (unsigned)pid);
				return FAILED;
			}

            DebugSimWrite("Sent delegated family create to P%u", (unsigned)pid);
			m_delegateLocal.Clear();
		    return SUCCESS;
		}		
        break;

    case 5:
    	// We're not processing a group create, check if there is a create
		if (!m_createRemote.Empty())
		{
			// Process the received create
			CreateMessage msg = m_createRemote.Read();

            if (msg.parent.lpid == m_lpid)
            {
                // The create has come back to the creating CPU.
                // Link the family entry to the one on the previous CPU.
                if (!m_allocator.SetupFamilyPrevLink(msg.first_fid, msg.link_prev))
                {
                    return FAILED;
                }
            }
            else
            {
                // Determine the next link
                LFID link_next = (m_next->m_lpid != msg.parent.lpid) ? INVALID_LFID : msg.first_fid;
            
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
    		        DeadlockWrite("Unable to setup F%u's next link to F%u", (unsigned)fid, (unsigned)msg.link_prev);
		            return FAILED;
		        }
            
                // Forward the create
  			    COMMIT{ msg.link_prev = fid; }

		        if (!m_next->OnGroupCreateReceived(msg))
		        {
       			    DeadlockWrite("Unable to forward group create to next processor");
			        return FAILED;
			    }
            }
			m_createRemote.Clear();
		}
		else if (!m_createLocal.Empty())
		{
			// Send the create
			if (!m_next->OnGroupCreateReceived(m_createLocal.Read()))
			{
			    DeadlockWrite("Unable to send group create to next processor");
				return FAILED;
			}

			m_createLocal.Clear();
		}
        // Only if there are no creates, do we handle the token.
        // This ensures that the token always follows the create on the ring network
        // and as such prevents certain deadlocks with concurrent group creates.
		else if (m_hasToken.IsSet())
		{
			// We have the token
			if (m_wantToken.IsSet())
			{
				// We want it as well, approve the Create
				if (!m_allocator.OnTokenReceived())
				{
				    DeadlockWrite("Unable to continue group create with token");
					return FAILED;
				}
				m_wantToken.Clear();
			}
			else if (m_tokenUsed.IsSet() && m_nextWantsToken.IsSet())
			{
				// Pass the token to the next CPU
				DebugSimWrite("Sending token to next processor");
				if (!m_next->OnTokenReceived())
				{
				    DeadlockWrite("Unable to send token to next processor");
					return FAILED;
				}
				
				m_nextWantsToken.Clear();
				m_hasToken.Clear();
			}
		}
		// We don't have the token
		else if ((m_wantToken.IsSet() || m_nextWantsToken.IsSet()) && !m_requestedToken.IsSet())
		{
			// But we, or m_next, wants it, so request it.
			if (!m_prev->OnRemoteTokenRequested())
			{
			    DeadlockWrite("Unable to request token from previous processor");
				return FAILED;
			}
			
			// Set a flag to prevent us from spamming the previous CPU
			m_requestedToken.Set();
		}
        return SUCCESS;
    
    case 6:
        // Send the remote sync
        assert(!m_remoteSync.Empty());
        {
            const RemoteSync& rs = m_remoteSync.Read();
            
            if (!m_grid[rs.pid]->GetNetwork().OnRemoteSyncReceived(rs.fid, rs.code))
            {
                DeadlockWrite("Unable to send remote sync for F%u to P%u", (unsigned)rs.fid, (unsigned)rs.pid);
                return FAILED;
            }
            
            m_remoteSync.Clear();
            return SUCCESS;
        }
        break;
        
    case 7:
        assert(!m_cleanedUpThread.Empty());
        {
            LFID fid = m_cleanedUpThread.Read();
            if (!m_allocator.OnRemoteThreadCleanup(fid))
            {
                DeadlockWrite("Unable to mark thread cleanup on F%u", (unsigned)fid);
                return FAILED;
            }
            m_cleanedUpThread.Clear();
			return SUCCESS;
        }
        break;

    case 8:
		assert(!m_completedThread.Empty());
        {
            LFID fid = m_completedThread.Read();
            if (!m_allocator.OnRemoteThreadCompletion(fid))
            {
                DeadlockWrite("Unable to mark thread completion on F%u", (unsigned)fid);
                return FAILED;
            }
            m_completedThread.Clear();
			return SUCCESS;
        }
        break;
        
    case 9:
		assert(!m_synchronizedFamily.Empty());
        {
            LFID fid = m_synchronizedFamily.Read();
            if (!m_next->OnFamilySynchronized(fid))
            {
                DeadlockWrite("Unable to send family synchronization for F%u", (unsigned)fid);
                return FAILED;
            }
            m_synchronizedFamily.Clear();
			return SUCCESS;
        }	
        break;
        
    case 10:
		assert(!m_terminatedFamily.Empty());
        {
            LFID fid = m_terminatedFamily.Read();
            if (!m_prev->OnFamilyTerminated(fid))
            {
                DeadlockWrite("Unable to send family termination for F%u", (unsigned)fid);
                return FAILED;
            }
            m_terminatedFamily.Clear();
			return SUCCESS;
        }	
        break;
    }

    return DELAYED;
}

void Network::Cmd_Help(ostream& out, const vector<string>& /* arguments */) const
{
    out <<
    "The network component manages all inter-processor communication such as\n"
    "the broadcasting of creates or exchange of shareds and globals. It also\n"
    "connects each processor to the delegation network.\n"
    "For communication within the group it uses a ring network exclusively.\n\n"
    "Supported operations:\n"
    "- read <component>\n"
    "  Reads and displays the various registers and buffers from the component.\n";
}

void Network::Cmd_Read(ostream& out, const vector<string>& /* arguments */) const
{
    out << dec;
    out << "Registers:" << endl;
    if (!m_registerRequestGroup.out.Empty())
    {
        const RegisterRequest& request = m_registerRequestGroup.out.Read();
        out << "* Requesting "
             << GetRemoteRegisterTypeString(request.addr.type) << " register "
             << request.addr.reg.str()
             << " from F" << request.addr.fid
             << " on previous processor" << endl;
    }

    if (!m_registerRequestGroup.in.Empty())
    {
        const RegisterRequest& request = m_registerRequestGroup.in.Read();
        out << "* Received request for "
             << GetRemoteRegisterTypeString(request.addr.type) << " register "
             << request.addr.reg.str()
             << " in F" << request.addr.fid
             << " from next processor" << endl;
    }

    if (!m_registerResponseGroup.out.Empty())
    {
        const RegisterResponse& response = m_registerResponseGroup.out.Read();
        out << "* Sending "
             << GetRemoteRegisterTypeString(response.addr.type) << " register "
             << response.addr.reg.str()
             << " to F" << response.addr.fid
             << " on next processor" << endl;
    }

    if (!m_registerResponseGroup.in.Empty())
    {
        const RegisterResponse& response = m_registerResponseGroup.in.Read();
        out << "* Received "
             << GetRemoteRegisterTypeString(response.addr.type) << " register "
             << response.addr.reg.str()
             << " in F" << response.addr.fid
             << " from previous processor" << endl;
    }

    if (!m_registerRequestRemote.out.Empty())
    {
        const RegisterRequest& request = m_registerRequestRemote.out.Read();
        out << "* Requesting "
             << GetRemoteRegisterTypeString(request.addr.type) << " register "
             << request.addr.reg.str()
             << " from F" << request.addr.fid
             << " on P" << request.addr.pid << endl;
    }

    if (!m_registerRequestRemote.in.Empty())
    {
        const RegisterRequest& request = m_registerRequestRemote.in.Read();
        out << "* Received request for "
             << GetRemoteRegisterTypeString(request.addr.type) << " register "
             << request.addr.reg.str()
             << " in F" << request.addr.fid
             << " from P" << request.addr.pid << endl;
    }

    if (!m_registerResponseRemote.out.Empty())
    {
        const RegisterResponse& response = m_registerResponseRemote.out.Read();
        out << "* Sending "
             << GetRemoteRegisterTypeString(response.addr.type) << " register "
             << response.addr.reg.str()
             << " to F" << response.addr.fid
             << " on P" << response.addr.pid << endl;
    }

    if (!m_registerResponseRemote.in.Empty())
    {
        const RegisterResponse& response = m_registerResponseRemote.in.Read();
        out << "* Received "
             << GetRemoteRegisterTypeString(response.addr.type) << " register "
             << response.addr.reg.str()
             << " in F" << response.addr.fid
             << " from P" << response.addr.pid << endl;
    }
    out << endl;

    out << "Token:" << endl;
    if (m_hasToken.IsSet())       out << "* Processor has token (used: " << boolalpha << m_tokenUsed.IsSet() << ")" << endl;
    if (m_wantToken.IsSet())      out << "* Processor wants token" << endl;
    if (m_nextWantsToken.IsSet()) out << "* Next processor wants token" << endl;
    out << endl;

    out << "Families and threads:" << endl;
    if (!m_terminatedFamily  .Empty()) out << "* Sending family termination of F" << m_terminatedFamily.Read() << endl;
    if (!m_synchronizedFamily.Empty()) out << "* Sending family synchronization of F" << m_synchronizedFamily.Read() << endl;
    if (!m_completedThread   .Empty()) out << "* Sending thread completion of F" << m_completedThread.Read() << endl;
    if (!m_cleanedUpThread   .Empty()) out << "* Sending thread cleanup of F" << m_cleanedUpThread.Read() << endl;
    if (!m_delegateRemote    .Empty()) out << "* Received delegated create of PC 0x" << hex << m_delegateRemote.Read().address << dec << endl;
}

}
