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
    Clock&                    clock,
    PlaceInfo&                place,
    const vector<Processor*>& grid,
    LPID                      lpid,
    Allocator&                alloc,
    RegisterFile&             regFile,
    FamilyTable&              familyTable
) :
    Object(name, parent, clock),
    
    m_parent     (parent),
    m_regFile    (regFile),
    m_familyTable(familyTable),
    m_allocator  (alloc),
    m_place      (place),
    
    m_prev(NULL),
    m_next(NULL),
    m_lpid(lpid),
    m_grid(grid),

#define CONSTRUCT_REGISTER(name) name(*this, #name)
    CONSTRUCT_REGISTER(m_createLocal),
    CONSTRUCT_REGISTER(m_createRemote),
    CONSTRUCT_REGISTER(m_synchronizedFamily),
    CONSTRUCT_REGISTER(m_createResult),
    CONSTRUCT_REGISTER(m_registers),
    CONSTRUCT_REGISTER(m_delegateOut),
    CONSTRUCT_REGISTER(m_delegateIn),
#undef CONTRUCT_REGISTER

    m_hasToken      ("f_hasToken", *this, clock,  lpid == 0), // CPU #0 starts out with the token
    m_wantToken     ("f_wantToken", *this, clock, false),
    m_tokenBusy     ("f_tokenBusy", *this, clock, false),

    p_Registers    ("registers",      delegate::create<Network, &Network::DoRegisters    >(*this)),
    p_Creation     ("creation",       delegate::create<Network, &Network::DoCreation     >(*this)),
    p_FamilySync   ("family-sync",    delegate::create<Network, &Network::DoFamilySync   >(*this)),
    p_ReserveFamily("reserve-family", delegate::create<Network, &Network::DoReserveFamily>(*this)),
    p_CreateResult ("create-result",  delegate::create<Network, &Network::DoCreateResult >(*this)),
    p_DelegationOut("delegation-out", delegate::create<Network, &Network::DoDelegationOut>(*this)),
    p_DelegationIn ("delegation-in",  delegate::create<Network, &Network::DoDelegationIn >(*this))
{
    m_createLocal .Sensitive(p_Creation);
    m_createRemote.Sensitive(p_Creation);
    
    m_synchronizedFamily.in.Sensitive(p_FamilySync);
    m_createResult      .in.Sensitive(p_CreateResult);
    m_registers         .in.Sensitive(p_Registers);

    m_wantToken              .Sensitive(p_Creation);
    m_place.m_reserve_context.Sensitive(p_ReserveFamily);
    m_place.m_want_token     .Sensitive(p_Creation);
    
    m_delegateOut.Sensitive(p_DelegationOut);
    m_delegateIn .Sensitive(p_DelegationIn);
}

void Network::Initialize(Network& prev, Network& next)
{
    m_prev = &prev;
    m_next = &next;

#define INITIALIZE(object,dest) object.Initialize(dest.object)
    INITIALIZE(m_synchronizedFamily, next);
    INITIALIZE(m_createResult,       prev);
    INITIALIZE(m_registers,          next);
#undef INITIALIZE
}

bool Network::SendMessage(const RemoteMessage& msg)
{
    assert(msg.type != RemoteMessage::MSG_NONE);
    
    if (msg.type == RemoteMessage::MSG_REGISTER && msg.reg.addr.fid.pid == INVALID_GPID)
    {
        // Group register message
        assert(msg.reg.addr.reg.index != INVALID_REG_INDEX);

        if (msg.reg.addr.fid.lfid != INVALID_LFID) {
            DebugSimWrite("Sending %s register %s in F%u",
                GetRemoteRegisterTypeString(msg.reg.addr.type),
                msg.reg.addr.reg.str().c_str(), (unsigned)msg.reg.addr.fid.lfid );
        } else {
            DebugSimWrite("Sending %s register to %s",
                GetRemoteRegisterTypeString(msg.reg.addr.type),
                msg.reg.addr.reg.str().c_str() );
        }

        if (!m_registers.out.Write(msg.reg))
        {
            DeadlockWrite("Unable to buffer group register message");
            return false;
        }
        DebugSimWrite("Sending group register message");
    }
    else if (msg.type == RemoteMessage::MSG_DETACH && msg.detach.fid.pid == INVALID_GPID)
    {
        // Group detach message
        RegisterMessage reg;
        reg.addr.type = RRT_DETACH;
        reg.addr.fid  = msg.detach.fid;
        
        if (!m_registers.out.Write(reg))
        {
            DeadlockWrite("Unable to buffer group detach message for F%u", (unsigned)msg.detach.fid.lfid);
            return false;
        }
        DebugSimWrite("Sending group detach message for F%u", (unsigned)msg.detach.fid.lfid);
    }
    else if (msg.type == RemoteMessage::MSG_BRK && msg.brk.pid == INVALID_GPID)
    {
        RegisterMessage reg;
        reg.addr.type       = RRT_BRK;
        reg.value.m_integer = msg.brk.index;		
        reg.addr.fid.lfid   = msg.brk.lfid;
		
        if (!m_registers.out.Write(reg))
        {
            DeadlockWrite("Unable to buffer group break message for F%u", (unsigned)msg.brk.lfid);
            return false;
        }

        DebugSimWrite("Sending group break message for F%u", (unsigned)msg.brk.lfid);	
    }
    else
    {
        // Delegated message
        DelegateMessage dmsg;
        (RemoteMessage&)dmsg = msg;
        dmsg.src = m_parent.GetPID();

        switch (msg.type)
        {
        case RemoteMessage::MSG_ALLOCATE:     dmsg.dest = msg.allocate.pid; break;
        case RemoteMessage::MSG_SET_PROPERTY: dmsg.dest = msg.property.fid.pid; break;
        case RemoteMessage::MSG_CREATE:       dmsg.dest = msg.create.fid.pid; break;
        case RemoteMessage::MSG_DETACH:       dmsg.dest = msg.detach.fid.pid; break;
        case RemoteMessage::MSG_SYNC:         dmsg.dest = msg.sync.fid.pid; break;
        case RemoteMessage::MSG_REGISTER:     dmsg.dest = msg.reg.addr.fid.pid; break;
        case RemoteMessage::MSG_BRK:          dmsg.dest = msg.brk.pid; break;
        default:  assert(false);              dmsg.dest = INVALID_GPID; break;
        }
        
        assert(dmsg.dest != INVALID_GPID);

        if (dmsg.dest == dmsg.src)
        {
            if (!m_delegateIn.Write(dmsg))
            {
                DeadlockWrite("Unable to buffer local network message to loopback");
                return false;
            }
            DebugSimWrite("Sending local network message to loopback");
        }
        else
        {
            if (!m_delegateOut.Write(dmsg))
            {
                DeadlockWrite("Unable to buffer remote network message for CPU%u", (unsigned)dmsg.dest);
                return false;
            }
            DebugSimWrite("Sending remote network message to CPU%u", (unsigned)dmsg.dest);
        }
    }
    return true;
}

bool Network::SendThreadCleanup(LFID fid)
{
    assert(fid != INVALID_LFID);

    // Cleanup notifications have to go over the same channel as registers to
    // ensure proper ordering.    
    RegisterMessage msg;
    msg.addr.type     = RRT_CLEANUP;
    msg.addr.fid.lfid = fid;
    if (!m_registers.out.Write(msg))
    {
        DeadlockWrite("Unable to buffer thread cleanup for F%u", (unsigned)fid);
        return false;
    }
    
    DebugSimWrite("Sending thread cleanup for F%u", (unsigned)fid);    
    return true;
}

bool Network::SendFamilySynchronization(LFID fid)
{
    assert(fid != INVALID_LFID);
    if (!m_synchronizedFamily.out.Write(fid))
    {
        DeadlockWrite("Unable to buffer family synchronization for F%u", (unsigned)fid);
        return false;
    }
    DebugSimWrite("Sending family synchronization for F%u", (unsigned)fid);
    return true;
}

bool Network::SendAllocation(const PlaceID& place, RegIndex reg)
{
    assert(place.pid != m_parent.GetPID());
    
    DelegateMessage msg;
    msg.type = DelegateMessage::MSG_ALLOCATE;
    msg.src  = m_parent.GetPID();
    msg.dest = place.pid;
    msg.allocate.exclusive  = place.exclusive;
    msg.allocate.suspend    = place.suspend;
    msg.allocate.completion = reg;
    
    if (!m_delegateOut.Write(msg))
    {
        DeadlockWrite("Unable to buffer remote allocation for CPU%u", (unsigned)msg.dest);
        return false;
    }
    DeadlockWrite("Sending remote allocation to CPU%u", (unsigned)msg.dest);
    return true;
}

bool Network::SendGroupCreate(LFID fid, RegIndex completion)
{
    const Family& family = m_familyTable[fid];
    assert(m_hasToken.IsSet());
    assert(m_tokenBusy.IsSet());   
    assert(family.parent_lpid == m_lpid);
            
    // Buffer the family information
    GroupCreateMessage message;
    message.first_fid     = fid;
    message.link_prev     = fid;
    message.infinite      = family.infinite;
    message.start         = family.start;
    message.step          = family.step;
    message.nThreads      = family.nThreads;
    message.virtBlockSize = family.virtBlockSize;
    message.physBlockSize = family.physBlockSize;
    message.address       = family.pc;
    message.completion    = completion;
    message.parent_lpid   = family.parent_lpid;
    
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
    if (!m_place.m_reserve_context.Set())
    {
        return false;
    }
    
    DebugSimWrite("Sending group create for F%u", (unsigned)fid);
    return true;
}

/// Called by the Allocator when it wants to do a group create
bool Network::RequestToken()
{
    if (!m_wantToken.Set())
    {
        return false;
    }
    m_place.m_want_token.Set(m_lpid);
    return true;
}

bool Network::OnTokenReceived()
{
    if (!m_hasToken.Set())
    {
        return false;
    }
    return true;
}

bool Network::ReleaseToken()
{
    assert(m_hasToken.IsSet());
    if (!m_tokenBusy.Clear())
    {
        return false;
    }
    return true;
}

bool Network::WriteRegister(const RemoteRegAddr& raddr, const RegValue& value)
{
    RegAddr addr = m_allocator.GetRemoteRegisterAddress(raddr);
    if (addr != INVALID_REG)
    {
        // Write it
        DebugSimWrite("Writing %s register %s in F%u to %s",
            GetRemoteRegisterTypeString(raddr.type),
            raddr.reg.str().c_str(),
            (unsigned)raddr.fid.lfid,
            addr.str().c_str()
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
    }
    return true;
}

Result Network::DoRegisters()
{
    // Handle incoming register messages
    assert(!m_registers.in.Empty());
    
    RegisterMessage msg     = m_registers.in.Read();
    const Family&   family  = m_familyTable[msg.addr.fid.lfid];
    bool            forward = false;
    
    switch (msg.addr.type)
    {
    case RRT_LAST_SHARED:
        // In most cases, last shared messages forward
        forward = true;
        
        if (msg.value.m_state == RST_INVALID)
        {
            // This is a shared request
            if (family.hasLastThread)
            {
                // We have the last thread, read the shared
                if (!ReadLastShared(msg.addr, msg.value))
                {
                    return FAILED;
                }
            }
        }
        // This is a shared response
        else if (family.parent_lpid == m_lpid)
        {
            // Shared response on the parent core; Write it
            DebugSimWrite("Writing a %s register to %s",
                GetRemoteRegisterTypeString(msg.addr.type),
                msg.addr.reg.str().c_str() );

            RegAddr addr = MAKE_REGADDR(msg.addr.reg.type, msg.return_addr);
            if (!m_regFile.p_asyncW.Write(addr))
            {
                DeadlockWrite("Unable to acquire port to write register response to %s", addr.str().c_str());
                return FAILED;
            }
                    
            assert(msg.value.m_state == RST_FULL);
            if (!m_regFile.WriteRegister(addr, msg.value, false))
            {
                DeadlockWrite("Unable to write register response to %s", addr.str().c_str());
                return FAILED;
            }
            
            // We don't need to forward anymore
            forward = false;
        }
        break;

    case RRT_GLOBAL:
        // Forward globals to the next core, unless we're on the parent core
        forward = (family.parent_lpid != m_lpid);
        // Fall-through

    case RRT_NEXT_DEPENDENT:
    case RRT_FIRST_DEPENDENT:
        // Incoming register for us, write it
        if (!WriteRegister(msg.addr, msg.value))
        {
            return FAILED;
        }
        break;

    case RRT_CLEANUP:    
        if (!m_allocator.OnRemoteThreadCleanup(msg.addr.fid.lfid))
        {
            DeadlockWrite("Unable to mark thread cleanup on F%u", (unsigned)msg.addr.fid.lfid);
            return FAILED;
        }
        break;

    case RRT_DETACH:
        if (!m_allocator.DecreaseFamilyDependency(msg.addr.fid.lfid, FAMDEP_DETACHED))
        {
            DeadlockWrite("Unable to mark family detachment of F%u", (unsigned)msg.addr.fid.lfid);
            return FAILED;
        }
        DebugSimWrite("Detached F%u", (unsigned)msg.addr.fid.lfid);
        
        // Forward the detachment, unless we're on the parent
        if (family.parent_lpid != m_lpid)
        {
            forward = true;
        }
        break;
    
    case RRT_BRK:	
        DebugSimWrite("Response to remote BREAK with index %u from previous CPU",(unsigned)msg.value.m_integer);
        if (!family.dependencies.breaked)
        {
            m_allocator.DecreaseFamilyDependency(msg.addr.fid.lfid,FAMDEP_BREAKED);
            DebugSimWrite("Mark F%u BREAKED",(unsigned)msg.addr.fid.lfid);
            
            if (m_lpid == (family.parent_lpid + 1) % m_parent.GetPlaceSize())
            {
                if (msg.value.m_integer == 0)
                {
                    Integer index  = family.index;
                    Integer offset = family.virtBlockSize - (family.index % family.virtBlockSize);
                    index += offset + (m_parent.GetPlaceSize() - 1) * family.virtBlockSize;
                    msg.value.m_integer = index;
                    DebugSimWrite("Change index to %u",(unsigned)msg.value.m_integer);
                }
            }
            
            if (!m_allocator.OnGroupBreak(msg.addr.fid.lfid, msg.value.m_integer))
            {
                DeadlockWrite("Unable to break family F%u", (unsigned)msg.addr.fid.lfid);
                return FAILED;
            }
        }
        else
        {
            DebugSimWrite("F%u has been BREAKED,only send foward to next",(unsigned)msg.addr.fid.lfid);
        }

        if (family.parent_lpid != m_lpid)
        {
            forward = true;
        }
        break;
	    
    default:
        assert(false);
        break;
    }
    
    if (forward)
    {
        // Forward the message on the ring
        msg.addr.fid.lfid = family.link_next;
        
        if (!m_registers.out.Write(msg))
        {
            DeadlockWrite("Unable to forward group register message");
            return FAILED;
        }
    }
    
    // We've processed this register message
    m_registers.in.Clear();
    return SUCCESS;
}

bool Network::ReadLastShared(const RemoteRegAddr& raddr, RegValue& value)
{
    const RegAddr addr = m_allocator.GetRemoteRegisterAddress(raddr);
    assert(addr != INVALID_REG);

    // The thread of the register has been allocated, read it
    if (!m_regFile.p_asyncR.Read())
    {
        return FAILED;
    }

    if (!m_regFile.ReadRegister(addr, value))
    {
        return FAILED;
    }

    if (value.m_state != RST_FULL)
    {
        /*
         It's possible that the last shared in a family hasn't been written.
         Print a warning, and return a dummy value.
        */
        DebugProgWrite("Reading unwritten %s register %s from last thread in F%u from %s",
            GetRemoteRegisterTypeString(raddr.type),
            raddr.reg.str().c_str(),
            (unsigned)raddr.fid.lfid,
            addr.str().c_str()
        );
            
        value.m_state = RST_FULL;
        switch (addr.type)
        {
        case RT_FLOAT:   value.m_float.fromfloat(0.0f); break;
        case RT_INTEGER: value.m_integer = 0; break;
        }
    }
    else
    {
        DebugSimWrite("Read %s register %s in F%u from %s",
            GetRemoteRegisterTypeString(raddr.type),
            raddr.reg.str().c_str(),
            (unsigned)raddr.fid.lfid,
            addr.str().c_str()
        );
    }
        
    return true;
}

Result Network::DoDelegationOut()
{
    // Send outgoing message over the delegation network
    assert(!m_delegateOut.Empty());
    const DelegateMessage& msg = m_delegateOut.Read();
    assert(msg.src == m_parent.GetPID());
    assert(msg.dest != m_parent.GetPID());

    // Send to destination (could be ourselves)
    if (!m_grid[msg.dest]->GetNetwork().m_delegateIn.Write(msg))
    {
        DeadlockWrite("Unable to buffer outgoing delegation message into destination input buffer");
        return FAILED;
    }
    
    m_delegateOut.Clear();
    return SUCCESS;    
}

Result Network::DoDelegationIn()
{
    // Handle incoming message from the delegation network
    assert(!m_delegateIn.Empty());
    const DelegateMessage& msg = m_delegateIn.Read();
    
    assert(msg.dest == m_parent.GetPID());
    
    switch (msg.type)
    {
    case DelegateMessage::MSG_ALLOCATE:
    {
        PlaceID place;
        place.type      = PLACE_GROUP;
        place.suspend   = msg.allocate.suspend;
        place.exclusive = msg.allocate.exclusive;
        
        FID fid;
        Result result = m_allocator.AllocateFamily(place, msg.src, msg.allocate.completion, &fid);
        if (result == FAILED)
        {
            DeadlockWrite("Unable to process received delegated allocate");
            return FAILED;
        }
        
        if (result == SUCCESS)
        {
            // We got an entry right away, return it
            RemoteMessage ret;
            ret.type = RemoteMessage::MSG_REGISTER;
            ret.reg.addr.type       = RRT_RAW;
            ret.reg.addr.fid.pid    = msg.src;
            ret.reg.addr.fid.lfid   = INVALID_LFID;
            ret.reg.addr.reg        = MAKE_REGADDR(RT_INTEGER, msg.allocate.completion);
            ret.reg.value.m_state   = RST_FULL;
            ret.reg.value.m_integer = m_parent.PackFID(fid);

            if (msg.src == m_parent.GetPID())
            {
                /*
                This response is meant for us. To avoid having to go to the output buffer,
                and then into this input buffer again, we forcibly overwrite the contents
                of the input register with the response.
                This is also necessary to avoid a circular dependency on the output buffer.
                */
                DelegateMessage dmsg;
                (RemoteMessage&)dmsg = ret;
                dmsg.type = DelegateMessage::MSG_REGISTER;
                dmsg.src  = msg.src;
                dmsg.dest = msg.src;
                m_delegateIn.Simulator::Register<DelegateMessage>::Write(dmsg);

                // Return here to avoid clearing the input buffer. We want to process this
                // response next cycle.
                return SUCCESS;
            }
            
            if (msg.src == m_parent.GetPID())
            {
                /*
                This response is meant for us. To avoid having to go to the output buffer,
                and then into this input buffer again, we forcibly overwrite the contents
                of the input register with the response.
                This is also necessary to avoid a circular dependency on the output buffer.
                */
                DelegateMessage dmsg;
                (RemoteMessage&)dmsg = ret;
                dmsg.type = DelegateMessage::MSG_REGISTER;
                dmsg.src  = msg.src;
                dmsg.dest = msg.src;
                m_delegateIn.Simulator::Register<DelegateMessage>::Write(dmsg);
            
                // Return here to avoid clearing the input buffer. We want to process this
                // response next cycle.
                return SUCCESS;
            }

            if (!SendMessage(ret))
            {
                DeadlockWrite("Unable to return FID for remote allocation");
                return FAILED;
            }
        }
        break;
    }
    
    case DelegateMessage::MSG_SET_PROPERTY:
    {
        Family& family = m_allocator.GetFamilyChecked(msg.property.fid.lfid, msg.property.fid.capability);
        COMMIT
        {
            switch (msg.property.type)
            {
                case FAMPROP_START: family.start         = (SInteger)msg.property.value; break;
                case FAMPROP_LIMIT: family.limit         = (SInteger)msg.property.value; break;
                case FAMPROP_STEP:  family.step          = (SInteger)msg.property.value; break;
                case FAMPROP_BLOCK: family.virtBlockSize = (TSize)msg.property.value; break;
                default: assert(false); break;
            }
        }
        break;
    }
        
    case DelegateMessage::MSG_CREATE:
        // Process the received delegated create
        if (!m_allocator.OnDelegatedCreate(msg.create, msg.src))
        {
            DeadlockWrite("Unable to process received delegation create");
            return FAILED;
        }
        break;
    
    case DelegateMessage::MSG_SYNC:
        if (!m_allocator.OnRemoteSync(msg.sync.fid.lfid, msg.sync.fid.capability, msg.src, msg.sync.reg))
        {
            DeadlockWrite("Unable to handle remote sync for F%u", (unsigned)msg.sync.fid.lfid);
            return FAILED;
        }
        break;
        
    case DelegateMessage::MSG_DETACH:
    {
        const Family& family = m_allocator.GetFamilyChecked(msg.detach.fid.lfid, msg.detach.fid.capability);
        assert(family.type == Family::LOCAL);
        
        if (!m_allocator.DecreaseFamilyDependency(msg.detach.fid.lfid, FAMDEP_DETACHED))
        {
            DeadlockWrite("Unable to mark family detachment of F%u", (unsigned)msg.detach.fid.lfid);
            return FAILED;
        }
        DebugSimWrite("Detached F%u", (unsigned)msg.detach.fid.lfid);
        break;
    }
	
    case DelegateMessage::MSG_BRK:
    {
        DebugSimWrite("Response to remote BREAK from CPU%u",(unsigned)msg.src);
		
        const Family& family = m_familyTable[msg.brk.lfid];
		
        DebugSimWrite("Index of F%u is %u now ", (unsigned)msg.brk.lfid, (unsigned)family.index);

        RemoteMessage msg_;
        Integer index  = family.index;
        Integer offset = family.virtBlockSize - (family.index % family.virtBlockSize);
        index += offset + (m_parent.GetPlaceSize() - 1) * family.virtBlockSize;
		
        msg_.type       = RemoteMessage::MSG_BRK;
        msg_.brk.lfid   = family.link_next;
        msg_.brk.pid    = INVALID_GPID;		
        msg_.brk.index  = (msg.src == m_parent.GetPID() + 1)?index : 0 ;
		
        DebugSimWrite("Parent CPU%u forward BREAK to next CPU and index is %u", (unsigned)msg.brk.pid,(unsigned)msg_.brk.index);
		
        if (!SendMessage(msg_))
        {
            DeadlockWrite("Unable to send break message for Group Family");
            return FAILED;
        }		
        break;
	}
        
    case DelegateMessage::MSG_REGISTER:
        switch (msg.reg.addr.type)
        {
        case RRT_LAST_SHARED:
        {
            // Create response and we're done
            DelegateMessage response;
            response.type = DelegateMessage::MSG_REGISTER;
            response.src  = m_parent.GetPID();
            response.dest = msg.src;
            response.reg.addr.fid.lfid = INVALID_LFID;
            response.reg.addr.fid.pid  = msg.src;
            response.reg.addr.type     = RRT_RAW;
            response.reg.addr.reg      = MAKE_REGADDR(msg.reg.addr.reg.type, msg.reg.return_addr);

            if (!ReadLastShared(msg.reg.addr, response.reg.value))
            {
                return FAILED;
            }

            if (response.dest == m_parent.GetPID())
            {
                /*
                This response is meant for us. To avoid having to go to the output buffer,
                and then into this input buffer again, we forcibly overwrite the contents
                of the input register with the response.
                This is also necessary to avoid a circular dependency on the output buffer.
                */
                m_delegateIn.Simulator::Register<DelegateMessage>::Write(response);
            
                // Return here to avoid clearing the input buffer. We want to process this
                // response next cycle.
                return SUCCESS;
            }
        
            if (!m_delegateOut.Write(response))
            {
                DeadlockWrite("Unable to buffer outgoing remote register response");
                return FAILED;
            }
            break;
        }
        
        case RRT_RAW:
        {
            assert(msg.reg.value.m_state == RST_FULL);
            
            if (!m_regFile.p_asyncW.Write(msg.reg.addr.reg))
            {
                DeadlockWrite("Unable to acquire port to write register response to %s", msg.reg.addr.reg.str().c_str());
                return FAILED;
            }
                    
            if (!m_regFile.WriteRegister(msg.reg.addr.reg, msg.reg.value, false))
            {
                DeadlockWrite("Unable to write register response to %s", msg.reg.addr.reg.str().c_str());
                return FAILED;
            }
            
            DebugSimWrite("Written register response to %s", msg.reg.addr.reg.str().c_str() );
            break;
        }
        
        case RRT_GLOBAL:
        case RRT_FIRST_DEPENDENT:
        {
            const Family& family = m_allocator.GetFamilyChecked(msg.reg.addr.fid.lfid, msg.reg.addr.fid.capability);

            if (family.type == Family::LOCAL)
            {
                // Write to the register on this core
                if (!WriteRegister(msg.reg.addr, msg.reg.value))
                {
                    return FAILED;
                }
            }
            else if (family.type == Family::GROUP)
            {
                // Forward the message to the next core
                RemoteMessage fwd;
                fwd.type = RemoteMessage::MSG_REGISTER;
                fwd.reg.addr.type     = msg.reg.addr.type;
                fwd.reg.addr.fid.pid  = INVALID_GPID;
                fwd.reg.addr.fid.lfid = family.link_next;
                fwd.reg.addr.reg      = msg.reg.addr.reg;
                fwd.reg.value         = msg.reg.value;
                
                if (!SendMessage(fwd))
                {
                    return FAILED;
                }
            }            
            break;
        }
        
        default:
            assert(false);
            break;
        }
        break;
    
    default:
        assert(false);
        break;
    }

    m_delegateIn.Clear();
    return SUCCESS;
}
    
Result Network::DoCreation()
{
    if (!m_createRemote.Empty())
    {
        // Process the received create
        const GroupCreateMessage& msg = m_createRemote.Read();

        // Process the create
        // This returns the local family table entry that was
        // allocated for this family. Although the context is
        // reserved, this can fail if the arbitration for
        // allocation fails.
        const LFID fid = m_allocator.OnGroupCreate(msg);
        if (fid == INVALID_LFID)
        {
            DeadlockWrite("Unable to process received group create");
            return FAILED;
        }

        if (m_next->m_lpid == msg.parent_lpid)
        {
            // The create has come to the last CPU.
            // I.e., the next core the parent core.
        
            // The family on the parent core is the next link.
            COMMIT{ m_familyTable[fid].link_next = msg.first_fid; }

            // The next_link is setup, activate the family
            if (!m_allocator.ActivateFamily(fid))
            {
                return FAILED;
            }
            
            // Send create completion message to the previous core
            CreateResult result;
            result.fid_parent = msg.link_prev;
            result.fid_remote = fid;
            result.fid_last   = fid;
            result.completion = msg.completion;

            if (!m_createResult.out.Write(result))
            {
                DeadlockWrite("Unable to create result for F%u to previous core", (unsigned)fid);
                return FAILED;
            }
            DebugSimWrite("Sent create result for F%u to F%u on previous core", (unsigned)result.fid_remote, (unsigned)result.fid_parent);
        }
        else
        {
            // Forward the create
            GroupCreateMessage forward(msg);
            COMMIT{ forward.link_prev = fid; }
            if (!m_next->m_createRemote.Write(forward))
            {
                DeadlockWrite("Unable to forward group create to next processor");
                return FAILED;
            }
            DebugSimWrite("Forwarded group create to next processor");
        }
        m_createRemote.Clear();
        return SUCCESS;
    }
        
    if (!m_createLocal.Empty())
    {
        // Send the create
        if (!m_next->m_createRemote.Write(m_createLocal.Read()))
        {
            DeadlockWrite("Unable to send group create to next processor");
            return FAILED;
        }
        DebugSimWrite("Sent group create to next processor");

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
            
            if (!m_tokenBusy.Set())
            {
                DeadlockWrite("Unable to set busy flag");
                return FAILED;
            }
            
            if (!m_wantToken.Clear())
            {
                DeadlockWrite("Unable to clear want-token flag");
                return FAILED;
            }
            
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
            
            if (!m_hasToken.Clear())
            {
                DeadlockWrite("Unable to clear token flag");
                return FAILED;
            }
        }
        return SUCCESS;
    }
    
    return FAILED;
}
    
Result Network::DoFamilySync()
{
    assert(!m_synchronizedFamily.in.Empty());
    LFID fid = m_synchronizedFamily.in.Read();
    if (!m_allocator.DecreaseFamilyDependency(fid, FAMDEP_PREV_SYNCHRONIZED))
    {
        DeadlockWrite("Unable to mark family synchronization on F%u", (unsigned)fid);
        return FAILED;
    }
    m_synchronizedFamily.in.Clear();
    return SUCCESS;
}
        
Result Network::DoReserveFamily()
{
    // We need to reserve a context
    DebugSimWrite("Reserving context for group create");
    if (!m_hasToken.IsSet())
    {
        m_allocator.ReserveContext();
    }
    
    if (m_hasToken.IsSet()) {
        // We sent the reservation signal, so just clear it now. This
        // means it's only active for a single cycle, which is important,
        // or we'll reserve multiple contexts for a single create.
        m_place.m_reserve_context.Clear();
    }
    return SUCCESS;
}

Result Network::DoCreateResult()
{
    assert(!m_createResult.in.Empty());
    const CreateResult& result = m_createResult.in.Read();
    
    Family& family = m_familyTable[result.fid_parent];
    assert(family.state     != FST_EMPTY);
    assert(family.type      == Family::GROUP);
    assert(family.link_next == INVALID_LFID);

    // Setup the next link
    DebugSimWrite("Setting next FID for F%u to F%u", (unsigned)result.fid_parent, (unsigned)result.fid_remote);
    COMMIT{ family.link_next = result.fid_remote; }

    // Now that we have link_next set up, we can activate the
    // family (in case it writes to shareds, it needs to have
    // a place to push them to).
    if (!m_allocator.ActivateFamily(result.fid_parent))
    {
        DeadlockWrite("Unable to activate F%u", (unsigned)result.fid_parent);
        return FAILED;
    }

    if (family.parent_lpid != m_lpid)
    {
        // This is not the parent core
        assert(family.link_prev != INVALID_LFID);
        
        // Forward the create result
        CreateResult forward;
        forward.fid_parent = family.link_prev;
        forward.fid_remote = result.fid_parent;
        forward.fid_last   = result.fid_last;
        forward.completion = result.completion;
    
        if (!m_createResult.out.Write(forward))
        {
            DeadlockWrite("Unable to buffer group create result for previous core");
            return FAILED;
        }
    }
    else
    {
        // This is the parent core
        assert(family.link_prev   == INVALID_LFID);
        assert(family.type        == Family::GROUP);
        assert(family.parent_lpid == m_lpid);

        // Set up the previous link to the FID of the family on the last core.
        COMMIT{ family.link_prev = result.fid_last; }
        
        // Write back the FID so the parent thread can continue
        FID fid;
        fid.lfid       = result.fid_parent;
        fid.pid        = m_parent.GetPID();
        fid.capability = family.capability;

        RegAddr addr = MAKE_REGADDR(RT_INTEGER, result.completion);
        RegValue value;
        value.m_state = RST_FULL;
        value.m_integer = m_parent.PackFID(fid);

        if (!m_regFile.p_asyncW.Write(addr))
        {
            DeadlockWrite("Unable to acquire port to write create completion to %s", addr.str().c_str());
            return FAILED;
        }

        if (!m_regFile.WriteRegister(addr, value, false))
        {
            DeadlockWrite("Unable to write create completion to %s", addr.str().c_str());
            return FAILED;
        }

        DebugSimWrite("Wrote create completion for F%u to %s", (unsigned)fid.lfid, addr.str().c_str());
    }
    
    m_createResult.in.Clear();
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

    if (!m_registers.out.Empty())
    {
        const RegisterMessage& msg = m_registers.out.Read();
        out << "* Sending ";
        switch (msg.addr.type)
        {
        case RRT_DETACH:  out << " family detachment for F" << msg.addr.fid.lfid; break;
        case RRT_CLEANUP: out << " thread cleanup for F" << msg.addr.fid.lfid; break;
        default:
            out << GetRemoteRegisterTypeString(msg.addr.type) << " register "
                << msg.addr.reg.str()
                << " to F" << msg.addr.fid.lfid
                << " on next processor";
            break;
        }
        out << endl;
    }
        
    if (!m_registers.in.Empty())
    {
        const RegisterMessage& msg = m_registers.in.Read();
        out << "* Received ";
        switch (msg.addr.type)
        {
        case RRT_DETACH:  out << " family detachment for F" << msg.addr.fid.lfid; break;
        case RRT_CLEANUP: out << " thread cleanup for F" << msg.addr.fid.lfid; break;
        default:
            out << GetRemoteRegisterTypeString(msg.addr.type) << " register "
                << msg.addr.reg.str()
                << " in F" << msg.addr.fid.lfid
                << " from previous processor";
            break;
        }
        out << endl;
    }
    
    out << endl;

    out << "Token:" << endl;
    if (m_hasToken.IsSet())  out << "* Processor has token (in use: " << boolalpha << m_tokenBusy.IsSet() << ")" << endl;
    if (m_wantToken.IsSet()) out << "* Processor wants token" << endl;
    out << endl;

    out << "Families and threads:" << endl;
    if (!m_synchronizedFamily.out.Empty()) out << "* Sending family synchronization of F"  << m_synchronizedFamily.out.Read() << endl;
    if (!m_createResult      .out.Empty()) out << "* Sending create result for F"          << m_createResult      .out.Read().fid_parent << endl;
    if (!m_synchronizedFamily.in .Empty()) out << "* Received family synchronization of F" << m_synchronizedFamily.in .Read() << endl;
    if (!m_createResult      .in .Empty()) out << "* Received create result for F"         << m_createResult      .in .Read().fid_parent << endl;
    out << endl;
        
    const struct {
        const char*                      name;
        const Register<DelegateMessage>& reg;
    } Registers[2] = {
        {"Outgoing", m_delegateOut},
        {"Incoming", m_delegateIn}
    };
    
    for (size_t i = 0; i < 2; ++i)
    {
        out << Registers[i].name << " delegation network:" << endl;
        if (!Registers[i].reg.Empty()) {
            const DelegateMessage& msg = Registers[i].reg.Read();
            switch (msg.type)
            {
            case DelegateMessage::MSG_ALLOCATE:
                out << "Allocate on CPU" << msg.dest << endl;
                break;
                
            case DelegateMessage::MSG_SET_PROPERTY:
                out << "Setting family property for F" << msg.property.fid.lfid << " on CPU" << msg.dest
                    << " (Capability: 0x" << hex << msg.property.fid.capability << ")" << endl;
                break;
                
            case DelegateMessage::MSG_CREATE:
                out << "Create for F" << msg.create.fid.lfid << " on CPU" << msg.dest << " at 0x" << hex << msg.create.address
                    << " (Capability: 0x" << hex << msg.create.fid.capability << ")" << endl;
                break;
                
            case DelegateMessage::MSG_SYNC:
                out << "Sync for F" << msg.sync.fid.lfid << " on CPU" << msg.dest << " to R" << msg.sync.reg << " on CPU" << msg.src
                    << " (Capability: 0x" << hex << msg.sync.fid.capability << ")" << endl;
                break;

            case DelegateMessage::MSG_DETACH:
                out << "Detach for F" << msg.detach.fid.lfid << " on CPU" << msg.dest
                    << " (Capability: 0x" << hex << msg.detach.fid.capability << ")" << endl;
                break;
                
            case DelegateMessage::MSG_REGISTER:
                assert(msg.reg.addr.fid.pid == msg.dest);
                out << "Register for "
                    << GetRemoteRegisterTypeString(msg.reg.addr.type) << " register "
                    << msg.reg.addr.reg.str()
                    << " in F" << msg.reg.addr.fid.lfid
                    << " from CPU" << msg.reg.addr.fid.pid << endl;
                break;
                
            default:
                assert(false);
                break;
            }
        } else {
            out << "Empty" << endl;
        }
        out << endl;
    }
}

}
