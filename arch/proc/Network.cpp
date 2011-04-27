#include "Processor.h"
#include <cassert>
#include <iostream>
using namespace std;

namespace Simulator
{

template <typename T>
static bool IsPowerOfTwo(const T& x)
{
    return (x & (x - 1)) == 0;
}

Processor::Network::Network(
    const std::string&        name,
    Processor&                parent,
    Clock&                    clock,
    const vector<Processor*>& grid,
    Allocator&                alloc,
    RegisterFile&             regFile,
    FamilyTable&              familyTable
) :
    Object(name, parent, clock),
    
    m_parent     (parent),
    m_regFile    (regFile),
    m_familyTable(familyTable),
    m_allocator  (alloc),
    
    m_prev(NULL),
    m_next(NULL),
    m_grid(grid),

#define CONSTRUCT_REGISTER(name) name(*this, #name)
    CONSTRUCT_REGISTER(m_delegateOut),
    CONSTRUCT_REGISTER(m_delegateIn),
    CONSTRUCT_REGISTER(m_link),
    CONSTRUCT_REGISTER(m_allocResponse),
#undef CONTRUCT_REGISTER

    p_DelegationOut("delegation-out", delegate::create<Network, &Processor::Network::DoDelegationOut>(*this)),
    p_DelegationIn ("delegation-in",  delegate::create<Network, &Processor::Network::DoDelegationIn >(*this)),
    p_Link         ("link",           delegate::create<Network, &Processor::Network::DoLink         >(*this)),
    p_AllocResponse("alloc-response", delegate::create<Network, &Processor::Network::DoAllocResponse>(*this))
{
    m_delegateOut.Sensitive(p_DelegationOut);
    m_delegateIn .Sensitive(p_DelegationIn);
    
    m_link.in.Sensitive(p_Link);
    
    m_allocResponse.in.Sensitive(p_AllocResponse);
}

void Processor::Network::Initialize(Network* prev, Network* next)
{
    m_prev = prev;
    m_next = next;

#define INITIALIZE(object,dest) object.Initialize(dest->object)
    if (next != NULL) {
        INITIALIZE(m_link, next);
    }
    
    if (prev != NULL) {
        INITIALIZE(m_allocResponse, prev);
    }
#undef INITIALIZE
}

bool Processor::Network::SendMessage(const RemoteMessage& msg)
{
    assert(msg.type != RemoteMessage::MSG_NONE);
    
    // Delegated message
    DelegateMessage dmsg;
    (RemoteMessage&)dmsg = msg;
    dmsg.src = m_parent.GetPID();

    // Get destination
    switch (msg.type)
    {
    case RemoteMessage::MSG_ALLOCATE:     dmsg.dest = msg.allocate.place.pid; break;
    case RemoteMessage::MSG_SET_PROPERTY: dmsg.dest = msg.property.fid.pid; break;
    case RemoteMessage::MSG_CREATE:       dmsg.dest = msg.create.fid.pid; break;
    case RemoteMessage::MSG_DETACH:       dmsg.dest = msg.detach.fid.pid; break;
    case RemoteMessage::MSG_SYNC:         dmsg.dest = msg.sync.fid.pid; break;
    case RemoteMessage::MSG_RAW_REGISTER: dmsg.dest = msg.rawreg.pid; break;
    case RemoteMessage::MSG_FAM_REGISTER: dmsg.dest = msg.famreg.fid.pid; break;
    case RemoteMessage::MSG_BREAK:        dmsg.dest = msg.brk.pid; break;
    default:                              dmsg.dest = INVALID_PID; break;
    }
    
    assert(dmsg.dest != INVALID_PID);

    if (dmsg.dest == dmsg.src)
    {
        if (GetKernel()->GetActiveProcess() == &p_DelegationIn)
        {
           /*
            This response is meant for us as a result of an input message.
            To avoid having to go to the output buffer, and then into this
            input buffer again, we forcibly overwrite the contents of the input
            register with the response.
            This is also necessary to avoid a circular dependency on the output buffer.
            */
            m_delegateIn.Simulator::Register<DelegateMessage>::Write(dmsg);

            // Return here to avoid clearing the input buffer. We want to process this
            // response next cycle.
            return SUCCESS;
        }

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
    return true;
}

bool Processor::Network::SendMessage(const LinkMessage& msg)
{
    assert(m_next != NULL);
    if (!m_link.out.Write(msg))
    {
        DeadlockWrite("Unable to buffer link message");
        return false;
    }
    return true;
}

bool Processor::Network::SendAllocResponse(const AllocResponse& msg)
{
    if (!m_allocResponse.out.Write(msg))
    {
        return false;
    }
    return true;
}

Result Processor::Network::DoAllocResponse()
{
    assert(!m_allocResponse.in.Empty());
    AllocResponse msg = m_allocResponse.in.Read();
    
    const LFID lfid = msg.prev_fid;
    Family& family = m_familyTable[lfid];

    // Grab the previous FID from the link field
    msg.prev_fid = family.link;
    
    // Set the link field to the next FID (LFID_INVALID if failed)
    COMMIT{ family.link = msg.next_fid; }
    
    // Number of cores in the place up to, and including, this core
    const PSize numCores = (m_parent.GetPID() % family.placeSize) + 1;
    
    if (msg.numCores == 0 && !msg.exact && IsPowerOfTwo(numCores))
    {
        // We've unwinded the place to a power of two.
        // Stop unwinding and commit.
        msg.numCores = numCores;
        
        DebugSimWrite("Unwound allocation to %u cores", (unsigned)numCores);
    }
    
    if (msg.numCores == 0)
    {
        // Unwind the allocation by releasing the context
        m_allocator.ReleaseContext(lfid);
    }
    else
    {
        // Commit the allocation
        COMMIT{ family.numCores = msg.numCores; }
        msg.next_fid = lfid;
    }
    
    if (numCores == 1)
    {
        // We're back at the first core, acknowledge allocate or fail
        FID fid;
        if (msg.numCores == 0)
        {
            // We can only fail at the first core if we have the exact flag
            // (Cause otherwise we commit from the power of two, and 1 core
            // always succeeds).
            assert(msg.exact);
            
            fid.pid        = 0;
            fid.lfid       = 0;
            fid.capability = 0;

            DebugSimWrite("Exact allocation failed");
        }
        else
        {
            fid.pid        = m_parent.GetPID();
            fid.lfid       = lfid;
            fid.capability = family.capability;
            
            DebugSimWrite("Allocation succeeded: F%u@CPU%u", (unsigned)fid.lfid, (unsigned)fid.pid);
        }
        
        RemoteMessage fwd;
        fwd.type = RemoteMessage::MSG_RAW_REGISTER;
        fwd.rawreg.pid             = msg.completion_pid;
        fwd.rawreg.addr            = MAKE_REGADDR(RT_INTEGER, msg.completion_reg);
        fwd.rawreg.value.m_state   = RST_FULL;
        fwd.rawreg.value.m_integer = m_parent.PackFID(fid);

        if (!SendMessage(fwd))
        {
            DeadlockWrite("Unable to send remote allocation writeback");
            return FAILED;
        }
    }
    // Forward response
    else if (!m_allocResponse.out.Write(msg))
    {
        return FAILED;
    }
    m_allocResponse.in.Clear();
    return SUCCESS;
}

bool Processor::Network::ReadLastShared(LFID fid, const RegAddr& raddr, RegValue& value)
{
    const RegAddr addr = m_allocator.GetRemoteRegisterAddress(fid, RRT_LAST_SHARED, raddr);
    assert(addr != INVALID_REG);

    // The thread of the register has been allocated, read it
    if (!m_regFile.p_asyncR.Read())
    {
        return false;
    }

    if (!m_regFile.ReadRegister(addr, value))
    {
        return false;
    }

    if (value.m_state != RST_FULL)
    {
        /*
         It's possible that the last shared in a family hasn't been written.
         Print a warning, and return a dummy value.
        */
        DebugProgWrite("Reading unwritten %s register %s from last thread in F%u from %s",
            GetRemoteRegisterTypeString(RRT_LAST_SHARED),
            raddr.str().c_str(),
            (unsigned)fid,
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
            GetRemoteRegisterTypeString(RRT_LAST_SHARED),
            raddr.str().c_str(),
            (unsigned)fid,
            addr.str().c_str()
        );
    }
    return true;
}

bool Processor::Network::WriteRegister(LFID fid, RemoteRegType kind, const RegAddr& raddr, const RegValue& value)
{
    RegAddr addr = m_allocator.GetRemoteRegisterAddress(fid, kind, raddr);
    if (addr != INVALID_REG)
    {
        // Write it
        DebugSimWrite("Writing %s register %s in F%u to %s",
            GetRemoteRegisterTypeString(kind),
            raddr.str().c_str(),
            (unsigned)fid,
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


bool Processor::Network::OnSync(LFID fid, PID completion_pid, RegIndex completion_reg)
{
    Family& family = m_familyTable[fid];
    if (family.link != INVALID_LFID)
    {
        // Forward the sync to the last core
        LinkMessage fwd;
        fwd.type = LinkMessage::MSG_SYNC;
        fwd.sync.fid            = family.link;
        fwd.sync.completion_pid = completion_pid;
        fwd.sync.completion_reg = completion_reg;

        if (!SendMessage(fwd))
        {
            DeadlockWrite("Unable to forward sync onto link");
            return false;
        }
    }
    // We're the last core in the family
    else if (!family.sync.done)
    {
        // The family hasn't terminated yet, setup sync link
        COMMIT
        {
            family.sync.pid = completion_pid;
            family.sync.reg = completion_reg;
        }
    }
    else
    {
        // The family has already completed, send sync result back
        RemoteMessage ret;
        ret.type = RemoteMessage::MSG_RAW_REGISTER;
        ret.rawreg.pid             = completion_pid;
        ret.rawreg.addr            = MAKE_REGADDR(RT_INTEGER, completion_reg);
        ret.rawreg.value.m_state   = RST_FULL;
        ret.rawreg.value.m_integer = 0;

        if (!SendMessage(ret))
        {
            DeadlockWrite("Unable to send sync acknowledgement");
            return false;
        }
    }
    return true;
}

bool Processor::Network::OnDetach(LFID fid)
{
    if (!m_allocator.DecreaseFamilyDependency(fid, FAMDEP_DETACHED))
    {
        DeadlockWrite("Unable to mark family detachment of F%u", (unsigned)fid);
        return false;
    }

    Family& family = m_familyTable[fid];
    if (family.link != INVALID_LFID)
    {
        // Forward message on link
        LinkMessage msg;
        msg.type = LinkMessage::MSG_DETACH;
        msg.detach.fid = family.link;
        if (!SendMessage(msg))
        {
            return false;
        }
    }
    DebugSimWrite("Detached F%u", (unsigned)fid);
    return true;
}

bool Processor::Network::OnBreak(LFID fid)
{
    const Family& family = m_familyTable[fid];
    if (!family.dependencies.allocationDone)
    {
        if (!m_allocator.DecreaseFamilyDependency(fid, FAMDEP_ALLOCATION_DONE))
        {
            DeadlockWrite("Unable to mark allocation done of F%u", (unsigned)fid);
            return false;
        }
    }
    
    if (family.link != INVALID_LFID)
    {
        LinkMessage msg;
        msg.type    = LinkMessage::MSG_BREAK;
        msg.brk.fid = family.link;
		
        if (!SendMessage(msg))
        {
            DeadlockWrite("Unable to send break message");
            return false;
        }
    }
    DebugSimWrite("Broken F%u", (unsigned)fid);
    return true;
}

Result Processor::Network::DoDelegationOut()
{
    // Send outgoing message over the delegation network
    assert(!m_delegateOut.Empty());
    const DelegateMessage& msg = m_delegateOut.Read();
    assert(msg.src == m_parent.GetPID());
    assert(msg.dest != m_parent.GetPID());

    // Send to destination
    if (!m_grid[msg.dest]->GetNetwork().m_delegateIn.Write(msg))
    {
        DeadlockWrite("Unable to buffer outgoing delegation message into destination input buffer");
        return FAILED;
    }
    
    m_delegateOut.Clear();
    return SUCCESS;
}

Result Processor::Network::DoDelegationIn()
{
    // Handle incoming message from the delegation network
    // Note that we make a copy here, because we want to clear it before
    // we process it, because we may overwrite the entry during processing.
    assert(!m_delegateIn.Empty());
    const DelegateMessage msg = m_delegateIn.Read();
    m_delegateIn.Clear();
    assert(msg.dest == m_parent.GetPID());
    
    switch (msg.type)
    {
    case DelegateMessage::MSG_ALLOCATE:
        if (!m_allocator.QueueFamilyAllocation(msg, msg.src))
        {
            DeadlockWrite("Unable to process family allocation request");
            return FAILED;
        }
        break;
    
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
                case FAMPROP_BLOCK: family.physBlockSize = (TSize)msg.property.value; break;
                default: assert(false); break;
            }
        }
        
        if (family.link != INVALID_LFID)
        {
            // Forward message on link
            LinkMessage fwd;
            fwd.type           = LinkMessage::MSG_SET_PROPERTY;
            fwd.property.fid   = family.link;
            fwd.property.type  = msg.property.type;
            fwd.property.value = msg.property.value;
            
            if (!SendMessage(fwd))
            {
                return FAILED;
            }
        }
        break;
    }
        
    case DelegateMessage::MSG_CREATE:
        // Process the received delegated create
        if (!m_allocator.QueueCreate(msg, msg.src))
        {
            DeadlockWrite("Unable to process received delegation create");
            return FAILED;
        }
        break;
    
    case DelegateMessage::MSG_SYNC:
        // Authorize family access
        m_allocator.GetFamilyChecked(msg.sync.fid.lfid, msg.sync.fid.capability);
        if (!OnSync(msg.sync.fid.lfid, msg.src, msg.sync.completion_reg))
        {
            return FAILED;
        }
        break;
                
    case DelegateMessage::MSG_DETACH:
        // Authorize family access
        m_allocator.GetFamilyChecked(msg.detach.fid.lfid, msg.detach.fid.capability);
        if (!OnDetach(msg.detach.fid.lfid))
        {
            return FAILED;
        }
        break;
	
    case DelegateMessage::MSG_BREAK:
        // A break can only be sent from the family itself,
        // so no capability-verification has to be done.
        if (!OnBreak(msg.brk.fid))
        {
            return FAILED;
        }
        break;
        
    case DelegateMessage::MSG_RAW_REGISTER:
        // Remote register write.
        // No validation necessary; cannot be sent by user code.
        assert(msg.rawreg.value.m_state == RST_FULL);
          
        if (!m_regFile.p_asyncW.Write(msg.rawreg.addr))
        {
            DeadlockWrite("Unable to acquire port to write register response to %s", msg.rawreg.addr.str().c_str());
            return FAILED;
        }
                    
        if (!m_regFile.WriteRegister(msg.rawreg.addr, msg.rawreg.value, false))
        {
            DeadlockWrite("Unable to write register response to %s", msg.rawreg.addr.str().c_str());
            return FAILED;
        }
            
        DebugSimWrite("Written register response to %s", msg.rawreg.addr.str().c_str() );
        break;
        
    case DelegateMessage::MSG_FAM_REGISTER:
        switch (msg.famreg.kind)
        {
        case RRT_LAST_SHARED:
        {
            m_allocator.GetFamilyChecked(msg.famreg.fid.lfid, msg.famreg.fid.capability);
            
            // Create response and we're done
            DelegateMessage response;
            response.type        = DelegateMessage::MSG_RAW_REGISTER;
            response.src         = m_parent.GetPID();
            response.dest        = msg.src;
            response.rawreg.pid  = msg.src;
            response.rawreg.addr = MAKE_REGADDR(msg.famreg.addr.type, msg.famreg.completion_reg);

            if (!ReadLastShared(msg.famreg.fid.lfid, msg.famreg.addr, response.rawreg.value))
            {
                return FAILED;
            }

            if (!SendMessage(response))
            {
                DeadlockWrite("Unable to buffer outgoing remote register response");
                return FAILED;
            }
            break;
        }
        
        case RRT_FIRST_DEPENDENT:
            m_allocator.GetFamilyChecked(msg.famreg.fid.lfid, msg.famreg.fid.capability);
            if (!WriteRegister(msg.famreg.fid.lfid, msg.famreg.kind, msg.famreg.addr, msg.famreg.value))
            {
                return FAILED;
            }           
            break;

        case RRT_GLOBAL:
        {
            const Family& family = m_allocator.GetFamilyChecked(msg.famreg.fid.lfid, msg.famreg.fid.capability);
            if (!WriteRegister(msg.famreg.fid.lfid, msg.famreg.kind, msg.famreg.addr, msg.famreg.value))
            {
                return FAILED;
            }
            
            if (family.link != INVALID_LFID)
            {
                // Forward on link as well
                LinkMessage fwd;
                fwd.type         = LinkMessage::MSG_GLOBAL;
                fwd.global.fid   = family.link;
                fwd.global.addr  = msg.famreg.addr;
                fwd.global.value = msg.famreg.value;
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

    return SUCCESS;
}
    
Result Processor::Network::DoLink()
{
    // Handle incoming message from the link
    assert(!m_link.in.Empty());
    const LinkMessage& msg = m_link.in.Read();
    
    switch (msg.type)
    {
    case LinkMessage::MSG_ALLOCATE:
        if (!m_allocator.QueueFamilyAllocation(msg))
        {
            DeadlockWrite("Unable to process family allocation request");
            return FAILED;
        }
        break;
    
    case LinkMessage::MSG_SET_PROPERTY:
    {
        Family& family = m_familyTable[msg.property.fid];
        COMMIT
        {
            // Set property
            switch (msg.property.type)
            {
                case FAMPROP_START: family.start         = (SInteger)msg.property.value; break;
                case FAMPROP_LIMIT: family.limit         = (SInteger)msg.property.value; break;
                case FAMPROP_STEP:  family.step          = (SInteger)msg.property.value; break;
                case FAMPROP_BLOCK: family.physBlockSize = (TSize)msg.property.value; break;
                default: assert(false); break;
            }
        }
        
        if (family.link != INVALID_LFID)
        {
            // Forward message on link
            LinkMessage fwd(msg);
            fwd.property.fid = family.link;
            if (!SendMessage(fwd))
            {
                return FAILED;
            }
        }
        break;
    }
        
    case LinkMessage::MSG_CREATE:
    {
        Family& family = m_familyTable[msg.create.fid];
        
        if (msg.create.numCores == 0)
        {
            // Forward message and clean up context
            if (family.link != INVALID_LFID)
            {
                LinkMessage fwd(msg);
                fwd.create.fid = family.link;
                if (!SendMessage(fwd))
                {
                    DeadlockWrite("Unable to forward restrict message");
                    return FAILED;
                }
            }

            m_allocator.ReleaseContext(msg.create.fid);
            DebugSimWrite("Cleaned up F%u due to restrict", (unsigned)msg.create.fid);
        }
        // Process the received create.
        // This will forward the message.
        else if (!m_allocator.QueueCreate(msg))
        {
            DeadlockWrite("Unable to process received place create");
            return FAILED;
        }
        break;
    }
    
    case LinkMessage::MSG_DONE:
        if (!m_allocator.DecreaseFamilyDependency(msg.done.fid, FAMDEP_PREV_SYNCHRONIZED))
        {
            DeadlockWrite("Unable to mark family synchronization on F%u", (unsigned)msg.done.fid);
            return FAILED;
        }
        break;
        
    case LinkMessage::MSG_SYNC:
        if (!OnSync(msg.sync.fid, msg.sync.completion_pid, msg.sync.completion_reg))
        {
            return FAILED;
        }
        break;
    
    case LinkMessage::MSG_DETACH:
        if (!OnDetach(msg.detach.fid))
        {
            return FAILED;
        }
        break;
    
    case LinkMessage::MSG_GLOBAL:
    {
        const Family& family = m_familyTable[msg.global.fid];
        if (!WriteRegister(msg.global.fid, RRT_GLOBAL, msg.global.addr, msg.global.value))
        {
            return FAILED;
        }
            
        if (family.link != INVALID_LFID)
        {
            // Forward on link as well
            LinkMessage fwd(msg);
            fwd.global.fid = family.link;
            if (!SendMessage(fwd))
            {
                return FAILED;
            }           
        }
        break;
    }

    case LinkMessage::MSG_BREAK:
        if (!OnBreak(msg.brk.fid))
        {
            return FAILED;
        }
        break;

    default:
        assert(false);
        break;
    }

    m_link.in.Clear();
    return SUCCESS;
}

void Processor::Network::Cmd_Info(ostream& out, const vector<string>& /* arguments */) const
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

void Processor::Network::Cmd_Read(ostream& out, const vector<string>& /* arguments */) const
{
    const struct {
        const char*                      name;
        const Register<DelegateMessage>& reg;
    } Registers[2] = {
        {"Incoming", m_delegateIn},
        {"Outgoing", m_delegateOut}
    };
    
    for (size_t i = 0; i < 2; ++i)
    {
        out << Registers[i].name << " delegation network:" << endl;
        out << dec;
        if (!Registers[i].reg.Empty()) {
            const DelegateMessage& msg = Registers[i].reg.Read();
            switch (msg.type)
            {
            case DelegateMessage::MSG_ALLOCATE:
                out << "* Allocate on CPU" << msg.dest << endl;
                break;
                
            case DelegateMessage::MSG_SET_PROPERTY:
                out << "* Setting family property for F" << msg.property.fid.lfid << " on CPU" << msg.dest
                    << " (Capability: 0x" << hex << msg.property.fid.capability << ")" << endl;
                break;
                
            case DelegateMessage::MSG_CREATE:
                out << "* Create for F" << msg.create.fid.lfid << " on CPU" << msg.dest << " at 0x" << hex << msg.create.address
                    << " (Capability: 0x" << hex << msg.create.fid.capability << ")" << endl;
                break;
                
            case DelegateMessage::MSG_SYNC:
                out << "* Sync for F" << msg.sync.fid.lfid << " on CPU" << msg.dest << " to R" << msg.sync.completion_reg << " on CPU" << msg.src
                    << " (Capability: 0x" << hex << msg.sync.fid.capability << ")" << endl;
                break;

            case DelegateMessage::MSG_DETACH:
                out << "* Detach for F" << msg.detach.fid.lfid << " on CPU" << msg.dest
                    << " (Capability: 0x" << hex << msg.detach.fid.capability << ")" << endl;
                break;
                
            case DelegateMessage::MSG_RAW_REGISTER:
                out << "* Register write to " << msg.rawreg.addr.str() << " on CPU" << msg.dest << endl;
                break;

            case DelegateMessage::MSG_FAM_REGISTER:
                out << "* Register for "
                    << GetRemoteRegisterTypeString(msg.famreg.kind) << " register "
                    << msg.famreg.addr.str()
                    << " in F" << msg.famreg.fid.lfid
                    << " on CPU" << msg.famreg.fid.pid
                    << " (Capability: 0x" << hex << msg.famreg.fid.capability << ")" << endl;
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

    const struct {
        const char*                  name;
        const Register<LinkMessage>& reg;
    } LinkRegisters[2] = {
        {"Incoming", m_link.in},
        {"Outgoing", m_link.out}
    };
    
    for (size_t i = 0; i < 2; ++i)
    {
        out << LinkRegisters[i].name << " link:" << endl;
        out << dec;
        if (!LinkRegisters[i].reg.Empty()) {
            const LinkMessage& msg = LinkRegisters[i].reg.Read();
            switch (msg.type)
            {
            case LinkMessage::MSG_ALLOCATE:
                out << "* Allocate of " << msg.allocate.size << " cores (FID on prev core: F" << msg.allocate.prev_fid << ")" << endl;
                break;
                
            case LinkMessage::MSG_SET_PROPERTY:
                out << "* Setting family property for F" << msg.property.fid << endl;
                break;
                
            case LinkMessage::MSG_CREATE:
                out << "* Create for F" << msg.create.fid << " at 0x" << hex << msg.create.address << " on " << msg.create.numCores << " cores" << endl;
                break;
                
            case LinkMessage::MSG_DONE:
                out << "* Termination for F" << msg.done.fid << endl;
                break;
                
            case LinkMessage::MSG_SYNC:
                out << "* Sync for F" << msg.sync.fid << " to R" << msg.sync.completion_reg << " on CPU" << msg.sync.completion_pid << endl;
                break;

            case LinkMessage::MSG_DETACH:
                out << "* Detach for F" << msg.detach.fid << endl;
                break;
                
            case LinkMessage::MSG_GLOBAL:
                out << "* Register write for global " << msg.global.addr.str() << " in F" << msg.global.fid << endl;
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
