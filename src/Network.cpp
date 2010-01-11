#include "Network.h"
#include "Allocator.h"
#include "Processor.h"
#include "FamilyTable.h"
#include <cassert>
#include <iostream>
using namespace std;

namespace Simulator
{

Network::Network(
    const std::string&        name,
    Processor&                parent,
    PlaceInfo&                place,
    const vector<Processor*>& grid,
    LPID                      lpid,
    Allocator&                alloc,
    RegisterFile&             regFile,
    FamilyTable&              familyTable
) :
    Object(name, parent),
    
    m_parent     (parent),
    m_regFile    (regFile),
    m_familyTable(familyTable),
    m_allocator  (alloc),
    m_place      (place),
    
    m_prev(NULL),
    m_next(NULL),
    m_lpid(lpid),
    m_grid(grid),
    
#define CONSTRUCT_REGISTER(name) name(*parent.GetKernel(), *this, #name)
    CONSTRUCT_REGISTER(m_createLocal),
    CONSTRUCT_REGISTER(m_createRemote),
    CONSTRUCT_REGISTER(m_delegateLocal),
    CONSTRUCT_REGISTER(m_delegateRemote),
    CONSTRUCT_REGISTER(m_delegateFailedLocal),
    CONSTRUCT_REGISTER(m_delegateFailedRemote),

    CONSTRUCT_REGISTER(m_synchronizedFamily),
    CONSTRUCT_REGISTER(m_terminatedFamily),
    CONSTRUCT_REGISTER(m_completedThread),
    CONSTRUCT_REGISTER(m_cleanedUpThread),
    CONSTRUCT_REGISTER(m_remoteSync),
    
    CONSTRUCT_REGISTER(m_registerRequestRemote),
    CONSTRUCT_REGISTER(m_registerResponseRemote),
    CONSTRUCT_REGISTER(m_registerRequestGroup),
    CONSTRUCT_REGISTER(m_registerResponseGroup),
#undef CONTRUCT_REGISTER

    m_hasToken      (*parent.GetKernel(),  lpid == 0), // CPU #0 starts out with the token
    m_wantToken     (*parent.GetKernel(), false),
    m_tokenBusy     (*parent.GetKernel(), false),

    p_RegResponseInGroup  ("reg-response-in-group",   delegate::create<Network, &Network::DoRegResponseInGroup  >(*this)),
    p_RegRequestIn        ("reg-request-in",          delegate::create<Network, &Network::DoRegRequestIn        >(*this)),
    p_RegResponseOutGroup ("reg-response-out-group",  delegate::create<Network, &Network::DoRegResponseOutGroup >(*this)),
    p_RegRequestOutGroup  ("reg-request-out-group",   delegate::create<Network, &Network::DoRegRequestOutGroup  >(*this)),
    p_Delegation          ("delegation",              delegate::create<Network, &Network::DoDelegation          >(*this)),
    p_Creation            ("creation",                delegate::create<Network, &Network::DoCreation            >(*this)),
    p_RemoteSync          ("rsync",                   delegate::create<Network, &Network::DoRemoteSync          >(*this)),
    p_ThreadCleanup       ("thread-cleanup",          delegate::create<Network, &Network::DoThreadCleanup       >(*this)),
    p_ThreadTermination   ("thread-termination",      delegate::create<Network, &Network::DoThreadTermination   >(*this)),
    p_FamilySync          ("family-sync",             delegate::create<Network, &Network::DoFamilySync          >(*this)),
    p_FamilyTermination   ("family-termination",      delegate::create<Network, &Network::DoFamilyTermination   >(*this)),
    p_RegRequestOutRemote ("reg-request-out-remote",  delegate::create<Network, &Network::DoRegRequestOutRemote >(*this)),
    p_RegResponseOutRemote("reg-response-out-remote", delegate::create<Network, &Network::DoRegResponseOutRemote>(*this)),
    p_RegResponseInRemote ("reg-response-in-remote",  delegate::create<Network, &Network::DoRegResponseInRemote >(*this)),
    p_ReserveFamily       ("reserve-family",          delegate::create<Network, &Network::DoReserveFamily       >(*this)),
    p_DelegateFailedOut   ("delegate-failed-out",     delegate::create<Network, &Network::DoDelegateFailedOut   >(*this)),
    p_DelegateFailedIn    ("delegate-failed-in",      delegate::create<Network, &Network::DoDelegateFailedIn    >(*this))
{
    m_createLocal   .Sensitive(p_Creation);
    m_createRemote  .Sensitive(p_Creation);
    m_delegateLocal .Sensitive(p_Delegation);
    m_delegateRemote.Sensitive(p_Delegation);
    m_delegateFailedLocal .Sensitive(p_DelegateFailedOut);
    m_delegateFailedRemote.Sensitive(p_DelegateFailedIn);

    m_synchronizedFamily.Sensitive(p_FamilySync);
    m_terminatedFamily  .Sensitive(p_FamilyTermination);
    m_completedThread   .Sensitive(p_ThreadTermination);
    m_cleanedUpThread   .Sensitive(p_ThreadCleanup);
    m_remoteSync        .Sensitive(p_RemoteSync);
    
    m_registerRequestRemote .in .Sensitive(p_RegRequestIn);
    m_registerRequestRemote .out.Sensitive(p_RegRequestOutRemote);
    m_registerResponseRemote.in .Sensitive(p_RegResponseInRemote);
    m_registerResponseRemote.out.Sensitive(p_RegResponseOutRemote);
    m_registerRequestGroup  .in .Sensitive(p_RegRequestIn);
    m_registerRequestGroup  .out.Sensitive(p_RegRequestOutGroup);
    m_registerResponseGroup .in .Sensitive(p_RegResponseInGroup);
    m_registerResponseGroup .out.Sensitive(p_RegResponseOutGroup);
    
    m_wantToken              .Sensitive(p_Creation);
    m_place.m_reserve_context.Sensitive(p_ReserveFamily);
    m_place.m_want_token     .Sensitive(p_Creation);
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
    
    if (addr.gpid == INVALID_GPID)
    {
        assert(addr.lpid == INVALID_LPID || addr.type == RRT_PARENT_SHARED);
        
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
    
    if (addr.gpid == INVALID_GPID)
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
            DebugSimWrite("Requesting remote %s register %s from F%u@CPU%u",
                GetRemoteRegisterTypeString(addr.type),
                addr.reg.str().c_str(),
                (unsigned)addr.fid,
                (unsigned)addr.gpid
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
    assert(request.addr.gpid      == m_parent.GetPID());
    assert(request.addr.lpid      == INVALID_LPID);
    assert(request.addr.fid       != INVALID_LFID);
    assert(request.addr.reg.index != INVALID_REG_INDEX);

    if (m_registerRequestRemote.in.Write(request))
    {
        DebugSimWrite("Received request for remote %s register %s in F%u for F%u@CPU%u",
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
    assert(response.addr.gpid      == m_parent.GetPID());
    assert(response.addr.lpid      == INVALID_LPID);
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
    assert(request.addr.gpid      == INVALID_GPID);
    assert(request.addr.lpid      == INVALID_LPID);
    assert(request.addr.fid       != INVALID_LFID);
    assert(request.addr.reg.index != INVALID_REG_INDEX);
    assert(request.return_pid     == INVALID_GPID);
    assert(request.return_fid     == m_familyTable[request.addr.fid].link_next);
    
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
    assert(response.addr.gpid      == INVALID_GPID);
    assert(response.addr.lpid      == INVALID_LPID || response.addr.type == RRT_PARENT_SHARED);
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
        DebugSimWrite("Delegating create for F%u to CPU%u", (unsigned)fid, (unsigned)dest_pid);
        return true;
    }
    return false;
}

bool Network::SendGroupCreate(LFID fid)
{
    const Family& family = m_familyTable[fid];
    assert(m_hasToken.IsSet());
    assert(m_tokenBusy.IsSet());   
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
    message.parent.fid    = fid;

    for (RegType i = 0; i < NUM_REG_TYPES; ++i)
    {
        message.regsNo[i] = family.regs[i].count;
    }
        
    if (!m_createLocal.Write(message))
    {
        return false;
    }
    
    // Set the global context reservation signal
    assert(!m_place.m_reserve_context.IsSet());
    m_place.m_reserve_context.Set();
    
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
        DebugSimWrite("Sending remote sync to F%u@CPU%u; code=%u", (unsigned)fid, (unsigned)pid, (unsigned)code);
        return true;
    }
    return false;
}

bool Network::OnDelegationFailedReceived(LFID fid)
{
    if (m_delegateFailedRemote.Write(fid))
    {
        DebugSimWrite("Received delegation failure for F%u", (unsigned)fid);
        return true;
    }
    return false;
}

bool Network::OnDelegationCreateReceived(const DelegateMessage& msg)
{
    if (m_delegateRemote.Write(msg))
    {
        DebugSimWrite("Received delegated create from F%u@CPU%u", (unsigned)msg.parent.fid, (unsigned)msg.parent.pid);
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
    m_place.m_want_token.Set(m_lpid);
    return true;
}

bool Network::OnTokenReceived()
{
    m_hasToken.Set();
    return true;
}

void Network::ReleaseToken()
{
    assert(m_hasToken.IsSet());
    m_tokenBusy.Clear();
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

bool Network::WriteRegister(const RemoteRegAddr& raddr, const RegValue& value)
{
    RegAddr addr = m_allocator.GetRemoteRegisterAddress(raddr);
    if (addr.valid())
    {
        // Write it
        DebugSimWrite("Writing %s register %s in F%u",
            GetRemoteRegisterTypeString(raddr.type),
            raddr.reg.str().c_str(),
            (unsigned)raddr.fid
        );
                    
        if (!m_regFile.p_asyncW.Write(addr))
        {
            DeadlockWrite("Unable to acquire port to write register response to %s", addr.str().c_str());
            return false;
        }
                    
        if (!m_regFile.WriteRegister(addr, value, false))
        {
           DeadlockWrite("Unable to write register response to %s", addr.str().c_str());
           return false;
        }
        
        if (raddr.type == RRT_PARENT_SHARED)
        {
            if (!m_allocator.DecreaseFamilyDependency(raddr.fid, FAMDEP_OUTSTANDING_SHAREDS))
            {
                DeadlockWrite("Unable to decrease outstanding shareds for F%u on writeback", (unsigned)raddr.fid);
                return false;
            }
        }
    }
    return true;
}

Result Network::DoRegResponseInGroup()
{
    // Incoming group responses
    assert(!m_registerResponseGroup.in.Empty());       
    const RegisterResponse& response = m_registerResponseGroup.in.Read();
            
    if (response.addr.type == RRT_PARENT_SHARED && response.addr.lpid != m_lpid)
    {
        // This is not the CPU the parent thread is located on. Forward it
        if (!SendRegister(response.addr, response.value))
        {
            DeadlockWrite("Unable to forward group parent shared");
            return FAILED;
        }
    }
    else
    {
        const Family& family = m_familyTable[response.addr.fid];
        assert(family.state != FST_EMPTY);

        if (response.addr.type == RRT_PARENT_SHARED && family.parent.gpid != INVALID_GPID)
        {
            // This is a parent shared on the local parent core in a delegated create.
            // It should be forwarded to the remote parent core.
            RemoteRegAddr addr(response.addr);
            addr.fid  = family.parent.fid;
            addr.gpid = family.parent.gpid;
            addr.lpid = INVALID_LPID;
            if (!SendRegister(addr, response.value))
            {
                DeadlockWrite("Unable to forward remote parent shared");
                return FAILED;
            }
        }
        else
        {
            // Incoming shared, global or parent shared for us. (Try to) write it.
            if (!WriteRegister(response.addr, response.value))
            {
                return FAILED;
            }
        }
    }
    
    // We've processed this register response
    m_registerResponseGroup.in.Clear();
    return SUCCESS;
}

Result Network::DoRegResponseInRemote()
{
    // Incoming remote responses
    assert(!m_registerResponseRemote.in.Empty());

    const RegisterResponse& response = m_registerResponseRemote.in.Read();
    const Family&           family   = m_familyTable[response.addr.fid];
    assert(family.state != FST_EMPTY);

    if (response.addr.type == RRT_FIRST_DEPENDENT && family.type != Family::LOCAL)
    {
        // Incoming remote shareds must be responses to parent shareds, which were
        // requested from the next core (where the first thread runs), so we just
        // forward the response (unless we're a local family
        RemoteRegAddr addr(response.addr);
        addr.fid  = family.link_next;
        addr.gpid = INVALID_GPID;
        if (!SendRegister(addr, response.value))
        {
            DeadlockWrite("Unable to forward remote parent shared");
            return FAILED;
        }
    }
    else
    {
        // Incoming global or (parent) shared for us. Write it.
        if (!WriteRegister(response.addr, response.value))
        {
            return FAILED;
        }
    }
            
    // We've processed this register response
    m_registerResponseRemote.in.Clear();
    return SUCCESS;
}
        
Result Network::DoRegRequestIn()
{
    // Incoming group/remote requests
    Register<RegisterRequest>* reg = NULL;
    if (!m_registerRequestGroup.in.Empty()) {
        reg = &m_registerRequestGroup.in;
    } else {
        assert (!m_registerRequestRemote.in.Empty());
        reg = &m_registerRequestRemote.in;
    }
        
    const RegisterRequest& request = reg->Read();
    const Family&          family  = m_familyTable[request.addr.fid];
        
    if (request.addr.type == RRT_PARENT_SHARED && family.parent.gpid != INVALID_GPID)
    {
        // A parent shared request came in on the local parent core of a delegated create.
        assert(reg == &m_registerRequestGroup.in);

        // Forward it to the remote parent
        RemoteRegAddr addr = request.addr;
        addr.gpid = family.parent.gpid;
        addr.fid  = family.parent.fid;

        DebugSimWrite("Forwarding request for parent shared %s to F%u@CPU%u",
            addr.reg.str().c_str(), (unsigned)addr.fid, (unsigned)addr.gpid);

        if (!RequestRegister(addr, request.addr.fid))
        {
            DeadlockWrite("Unable to forward request for parent shared to remote parent core");
            return FAILED;
        }

        reg->Clear();
        return SUCCESS;
    }
    else
    {
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
        return_addr.gpid = request.return_pid;
        return_addr.lpid = INVALID_LPID;
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
                forward.gpid = family.parent.gpid;
                forward.fid  = family.parent.fid;

                if (!RequestRegister(forward, request.addr.fid))
                {                            
                    DeadlockWrite("Unable to forward request to remote place");
                    return FAILED;
                }

                DebugSimWrite("Forwarded remote request to %s register %s in F%u to F%u@CPU%u",
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
}
    
Result Network::DoRegResponseOutGroup()
{
    // Outgoing group responses
    assert(!m_registerResponseGroup.out.Empty());
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
        
Result Network::DoRegRequestOutGroup()
{
    // Outgoing group requests
    assert(!m_registerRequestGroup.out.Empty());
    if (!m_prev->OnRegisterRequested(m_registerRequestGroup.out.Read()))
    {
        DeadlockWrite("Unable to send register request to previous processor");
        return FAILED;
    }

    // We've sent this register request
    m_registerRequestGroup.out.Clear();
    return SUCCESS;
}

Result Network::DoRegResponseOutRemote()
{
    // Outgoing remote responses
    assert(!m_registerResponseRemote.out.Empty());
    const RegisterResponse& response = m_registerResponseRemote.out.Read();
    assert(response.value.m_state == RST_FULL);
            
    DebugSimWrite("Sending %s register %s in F%u to CPU%u",
        GetRemoteRegisterTypeString(response.addr.type),
        response.addr.reg.str().c_str(),
        (unsigned)response.addr.fid,
        (unsigned)response.addr.gpid
    );
            
    if (!m_grid[response.addr.gpid]->GetNetwork().OnRemoteRegisterReceived(response))
    {
        DeadlockWrite("Unable to send register response to CPU%u", (unsigned)response.addr.gpid);
        return FAILED;
    }

    // We've sent this register response
    m_registerResponseRemote.out.Clear();
    return SUCCESS;
}
    
Result Network::DoRegRequestOutRemote()
{
    // Outgoing remote requests
    assert(!m_registerRequestRemote.out.Empty());
    const RegisterRequest& request = m_registerRequestRemote.out.Read();
    if (!m_grid[request.addr.gpid]->GetNetwork().OnRemoteRegisterRequested(request))
    {
        DeadlockWrite("Unable to send register request to CPU%u", (unsigned)request.addr.gpid);
        return FAILED;
    }

    // We've sent this register request
    m_registerRequestRemote.out.Clear();
    return SUCCESS;
}
    
Result Network::DoDelegation()
{
    // We're not processing a delegated create, check if there is a create
    if (!m_delegateRemote.Empty())
    {
        // Process the received delegation
        const DelegateMessage& msg =m_delegateRemote.Read();
        Result result = m_allocator.OnDelegatedCreate(msg);
        if (result == FAILED)
        {
            DeadlockWrite("Unable to process received delegation create");
            return FAILED;
        }
            
        if (result == DELAYED)
        {
            // We need to send back 
            DebugSimWrite("Delegated create failed");
            if (!m_delegateFailedLocal.Write(make_pair(msg.parent.pid, msg.parent.fid)))
            {
                DeadlockWrite("Unable to write failure to buffer");
                return FAILED;
            }
        }
        
        m_delegateRemote.Clear();
        return SUCCESS;
    }

    assert(!m_delegateLocal.Empty());
    // Process the outgoing delegation
    const pair<GPID, DelegateMessage>& delegate = m_delegateLocal.Read();
    const GPID             pid = delegate.first;
    const DelegateMessage& msg = delegate.second;
                
    // Send the create
    if (!m_grid[pid]->GetNetwork().OnDelegationCreateReceived(msg))
    {
        DeadlockWrite("Unable to send delegation create to CPU%u", (unsigned)pid);
        return FAILED;
    }

    DebugSimWrite("Sent delegated family create to CPU%u", (unsigned)pid);
    m_delegateLocal.Clear();
    return SUCCESS;
}
    
Result Network::DoCreation()
{
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
        return SUCCESS;
    }
        
    if (!m_createLocal.Empty())
    {
        // Send the create
        if (!m_next->OnGroupCreateReceived(m_createLocal.Read()))
        {
            DeadlockWrite("Unable to send group create to next processor");
            return FAILED;
        }

        m_createLocal.Clear();
        return SUCCESS;
    }
        
    // Only if there are no creates, do we handle the token.
    // This ensures that the token always follows the create on the ring network
    // and as such prevents certain deadlocks with concurrent group creates.
    if (m_hasToken.IsSet())
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
            m_tokenBusy.Set();
            m_wantToken.Clear();
            m_place.m_want_token.Clear(m_lpid);
        }
        else if (m_place.m_want_token.IsSet())
        {
            // Another core wants the token, send it on its way
            if (m_tokenBusy.IsSet())
            {
                DeadlockWrite("Waiting for token to be released before it can be sent to next processor");
                return FAILED;
            }
            
            // Pass the token to the next CPU
            DebugSimWrite("Sending token to next processor");
            if (!m_next->OnTokenReceived())
            {
                DeadlockWrite("Unable to send token to next processor");
                return FAILED;
            }
            
            m_hasToken.Clear();
        }
        return SUCCESS;
    }
    
    return FAILED;
}
    
Result Network::DoRemoteSync()
{
    // Send the remote sync
    assert(!m_remoteSync.Empty());
    const RemoteSync& rs = m_remoteSync.Read();
            
    if (!m_grid[rs.pid]->GetNetwork().OnRemoteSyncReceived(rs.fid, rs.code))
    {
        DeadlockWrite("Unable to send remote sync for F%u to CPU%u", (unsigned)rs.fid, (unsigned)rs.pid);
        return FAILED;
    }
            
    m_remoteSync.Clear();
    return SUCCESS;
}
        
Result Network::DoThreadCleanup()
{
    assert(!m_cleanedUpThread.Empty());
    LFID fid = m_cleanedUpThread.Read();
    if (!m_allocator.OnRemoteThreadCleanup(fid))
    {
        DeadlockWrite("Unable to mark thread cleanup on F%u", (unsigned)fid);
        return FAILED;
    }
    m_cleanedUpThread.Clear();
    return SUCCESS;
}

Result Network::DoThreadTermination()
{
    assert(!m_completedThread.Empty());
    LFID fid = m_completedThread.Read();
    if (!m_allocator.OnRemoteThreadCompletion(fid))
    {
        DeadlockWrite("Unable to mark thread completion on F%u", (unsigned)fid);
        return FAILED;
    }
    m_completedThread.Clear();
    return SUCCESS;
}
        
Result Network::DoFamilySync()
{
    assert(!m_synchronizedFamily.Empty());
    LFID fid = m_synchronizedFamily.Read();
    if (!m_next->OnFamilySynchronized(fid))
    {
       DeadlockWrite("Unable to send family synchronization for F%u", (unsigned)fid);
       return FAILED;
    }
    m_synchronizedFamily.Clear();
    return SUCCESS;
}
        
Result Network::DoFamilyTermination()
{
    assert(!m_terminatedFamily.Empty());
    LFID fid = m_terminatedFamily.Read();
    if (!m_prev->OnFamilyTerminated(fid))
    {
        DeadlockWrite("Unable to send family termination for F%u", (unsigned)fid);
        return FAILED;
    }
    m_terminatedFamily.Clear();
    return SUCCESS;
}   
        
Result Network::DoReserveFamily()
{
    // We need to reserve a context
    DebugSimWrite("Reserving context for group create");
    m_allocator.ReserveContext( m_hasToken.IsSet() );
        
    if (m_hasToken.IsSet()) {
        // We sent the reservation signal, so just clear it now. This
        // means it's only active for a single cycle, which is important,
        // or we'll reserve multiple contexts for a single create.
        m_place.m_reserve_context.Clear();
    }
    return SUCCESS;
}
    
Result Network::DoDelegateFailedOut()
{
    assert(!m_delegateFailedLocal.Empty());
    const std::pair<GPID, LFID>& df = m_delegateFailedLocal.Read();
    if (!m_grid[df.first]->GetNetwork().OnDelegationFailedReceived(df.second))
    {
        DeadlockWrite("Unable to send delegation failure for F%u to CPU%u", (unsigned)df.second, (unsigned)df.first);
        return FAILED;
    }
    m_delegateFailedLocal.Clear();
    return SUCCESS;
}
    
Result Network::DoDelegateFailedIn()
{
    assert(!m_delegateFailedRemote.Empty());
    LFID fid = m_delegateFailedRemote.Read();
    if (!m_allocator.OnDelegationFailed(fid))
    {
        DeadlockWrite("Unable to process delegation failure for F%u", (unsigned)fid);
        return FAILED;
    }
    m_delegateFailedRemote.Clear();
    return SUCCESS;
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
             << " on CPU" << request.addr.gpid << endl;
    }

    if (!m_registerRequestRemote.in.Empty())
    {
        const RegisterRequest& request = m_registerRequestRemote.in.Read();
        out << "* Received request for "
             << GetRemoteRegisterTypeString(request.addr.type) << " register "
             << request.addr.reg.str()
             << " in F" << request.addr.fid
             << " from CPU" << request.addr.gpid << endl;
    }

    if (!m_registerResponseRemote.out.Empty())
    {
        const RegisterResponse& response = m_registerResponseRemote.out.Read();
        out << "* Sending "
             << GetRemoteRegisterTypeString(response.addr.type) << " register "
             << response.addr.reg.str()
             << " to F" << response.addr.fid
             << " on CPU" << response.addr.gpid << endl;
    }

    if (!m_registerResponseRemote.in.Empty())
    {
        const RegisterResponse& response = m_registerResponseRemote.in.Read();
        out << "* Received "
             << GetRemoteRegisterTypeString(response.addr.type) << " register "
             << response.addr.reg.str()
             << " in F" << response.addr.fid
             << " from CPU" << response.addr.gpid << endl;
    }
    out << endl;

    out << "Token:" << endl;
    if (m_hasToken.IsSet())  out << "* Processor has token (in use: " << boolalpha << m_tokenBusy.IsSet() << ")" << endl;
    if (m_wantToken.IsSet()) out << "* Processor wants token" << endl;
    out << endl;

    out << "Families and threads:" << endl;
    if (!m_terminatedFamily    .Empty()) out << "* Sending family termination of F" << m_terminatedFamily.Read() << endl;
    if (!m_synchronizedFamily  .Empty()) out << "* Sending family synchronization of F" << m_synchronizedFamily.Read() << endl;
    if (!m_completedThread     .Empty()) out << "* Sending thread completion of F" << m_completedThread.Read() << endl;
    if (!m_cleanedUpThread     .Empty()) out << "* Sending thread cleanup of F" << m_cleanedUpThread.Read() << endl;
    if (!m_delegateRemote      .Empty()) out << "* Received delegated create of PC 0x" << hex << m_delegateRemote.Read().address << dec << endl;
    if (!m_delegateFailedRemote.Empty()) out << "* Received delegation failure for F" << m_delegateFailedRemote.Read() << dec << endl;
}

}
