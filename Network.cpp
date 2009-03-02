#include "Network.h"
#include "Allocator.h"
#include "Processor.h"
#include "FamilyTable.h"
#include <cassert>
using namespace Simulator;
using namespace std;

Network::Network(Processor& parent, const std::string& name, Allocator& alloc, RegisterFile& regFile, FamilyTable& familyTable) :
    IComponent(&parent, parent.GetKernel(), name, 8),
    m_parent(parent), m_regFile(regFile), m_familyTable(familyTable), m_allocator(alloc),
    m_prev(NULL), m_next(NULL),
	
	m_createLocal(parent.GetKernel()), m_createRemote(parent.GetKernel()), m_createState(CS_PROCESSING_NONE),
	m_global(parent.GetKernel()),

    m_reservation(parent.GetKernel()), m_unreservation(parent.GetKernel()),
    m_completedFamily(parent.GetKernel()), m_completedThread(parent.GetKernel()), m_cleanedUpThread(parent.GetKernel()),
    
	m_hasToken(parent.GetKernel(), parent.GetPID() == 0), // CPU #0 starts out with the token
	m_wantToken(parent.GetKernel(), false), m_nextWantsToken(parent.GetKernel(), false), m_requestedToken(parent.GetKernel(), false)
{
    parent.GetKernel().RegisterComponent(*this, 1);
    m_lockToken            = 0;
	m_global.count         = 0;
	m_global.local.m_state = RST_INVALID;
}

void Network::Initialize(Network& prev, Network& next)
{
    m_prev = &prev;
    m_next = &next;
}

bool Network::SendThreadCleanup   (GFID fid) { return m_next->OnThreadCleanedUp(fid); }
bool Network::SendThreadCompletion(GFID fid) { return m_prev->OnThreadCompleted(fid); }
bool Network::SendFamilyCompletion(GFID fid) { return m_next->OnFamilyCompleted(fid); }

bool Network::SendShared(GFID fid, bool parent, const RegAddr& addr, const RegValue& value)
{
    assert(value.m_state == RST_FULL);
    if (m_sharedResponse.fid == INVALID_GFID)
    {
        COMMIT
        {
            m_sharedResponse.fid    = fid;
            m_sharedResponse.parent = parent;
            m_sharedResponse.addr   = addr;
            m_sharedResponse.value  = value;
        }
        return true;
    }
    return false;
}

bool Network::RequestShared(GFID fid, const RegAddr& addr, bool parent)
{
    if (m_sharedRequest.fid == INVALID_GFID)
    {
        COMMIT
        {
            m_sharedRequest.fid           = fid;
            m_sharedRequest.parent        = parent;
            m_sharedRequest.addr          = addr;
            m_sharedRequest.value.m_state = RST_INVALID;
        }
        return true;
    }
    return false;
}

Result Network::OnSharedRequested(const SharedInfo& sharedInfo)
{
    if (m_sharedResponse.fid == INVALID_GFID)
    {	
		// If the family hasn't been created yet, we just ignore the request
		if (m_familyTable.TranslateFamily(sharedInfo.fid) != INVALID_LFID)
		{
		    DebugSimWrite("Shared request G%u:%s stored", sharedInfo.fid, sharedInfo.addr.str().c_str());
			COMMIT
			{
				m_sharedResponse = sharedInfo;
				m_sharedResponse.value.m_state = RST_INVALID;
			}
		}
		else
		{
            DebugSimWrite("Shared request G%u:%s ignored", sharedInfo.fid, sharedInfo.addr.str().c_str());
		}
        return SUCCESS;
    }
    return FAILED;
}

Result Network::OnSharedReceived(const SharedInfo& sharedInfo)
{
    if (m_sharedReceived.fid == INVALID_GFID)
    {
		assert(sharedInfo.value.m_state == RST_FULL);

		// If the family hasn't been created yet, we just ignore the request
		if (m_familyTable.TranslateFamily(sharedInfo.fid) != INVALID_LFID)
		{
			COMMIT{ m_sharedReceived = sharedInfo; }
		}
		return SUCCESS;
    }
    return FAILED;
}

bool Network::OnThreadCleanedUp(GFID fid)
{
    assert(fid != INVALID_GFID);
    if (m_cleanedUpThread.IsEmpty())
    {
        DebugSimWrite("Received OnThreadCleanup for G%u", fid);
        m_cleanedUpThread.Write(fid);
        return true;
    }
    return false;
}

bool Network::OnThreadCompleted(GFID fid)
{
    assert(fid != INVALID_GFID);
    if (m_completedThread.IsEmpty())
    {
        DebugSimWrite("Received OnThreadCompleted for G%u", fid);
        m_completedThread.Write(fid);
        return true;
    }
    return false;
}

bool Network::OnFamilyCompleted(GFID fid)
{
    assert(fid != INVALID_GFID);
    if (m_completedFamily.IsEmpty())
    {
        DebugSimWrite("Received OnFamilyCompleted for G%u", fid);
        m_completedFamily.Write(fid);
        return true;
    }
    return false;
}

bool Network::SendFamilyCreate(LFID fid)
{
	if (!m_createLocal.IsFull())
    {
        assert(m_hasToken.Read());
        COMMIT
        {
            // Buffer the family information
			const Family& family = m_familyTable[fid];

            CreateMessage message;
            message.infinite      = family.infinite;
			message.fid           = family.gfid;
            message.start         = family.start;
			message.step          = family.step;
			message.nThreads      = family.nThreads;
			message.virtBlockSize = family.virtBlockSize;
			message.physBlockSize = family.physBlockSize;
			message.address       = family.pc;
			message.parent        = family.parent;

            for (RegType i = 0; i < NUM_REG_TYPES; i++)
            {
				message.regsNo[i] = family.regs[i].count;
			}

			m_createLocal.Write(make_pair(fid, message));
			DebugSimWrite("Broadcasting create for G%u (F%u)", message.fid, fid);
        }
        return true;
    }
    return false;
}

bool Network::OnFamilyCreateReceived(const CreateMessage& msg)
{
    if (msg.parent.pid != m_parent.GetPID())
    {
		if (m_createRemote.IsFull())
		{
			return false;
		}

		m_createRemote.Write(msg);
		DebugSimWrite("Received create for G%u", msg.fid);
    }
	else
	{
		// Create has come full circle
		COMMIT{ m_lockToken--; }
	}
    return true;
}

bool Network::RequestToken()
{
    // Called by the Allocator when it wants to do a global create
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

bool Network::SendFamilyReservation(GFID fid)
{
    if (!m_reservation.IsLocalFull())
    {
        m_reservation.WriteLocal(RemoteFID(fid, m_parent.GetPID()));
        return true;
    }
    return false;
}

bool Network::OnFamilyReservationReceived(const RemoteFID& rfid)
{
	if (rfid.pid != m_parent.GetPID())
    {
        DebugSimWrite("Received reservation for G%u", rfid.fid);
		if (m_reservation.IsRemoteFull())
		{
			return false;
        }
		m_reservation.WriteRemote(rfid);
    }
	else
	{
		m_allocator.OnReservationComplete();
	}
	return true;
}

bool Network::SendFamilyUnreservation(GFID fid)
{
    if (!m_unreservation.IsLocalFull())
    {
        m_unreservation.WriteLocal(RemoteFID(fid, m_parent.GetPID()));
        return true;
    }
    return false;
}

bool Network::OnFamilyUnreservationReceived(const RemoteFID& rfid)
{
    if (rfid.pid != m_parent.GetPID())
	{
		if (m_unreservation.IsRemoteFull())
		{
	        return false;
        }
        m_unreservation.WriteRemote(rfid);
    }
    return true;
}

bool Network::OnGlobalReceived(PID parent, const RegValue& value)
{
	assert(value.m_state == RST_FULL);
	if (m_parent.GetPID() != parent)
	{
		if (m_createState != CS_PROCESSING_REMOTE || m_global.value.IsRemoteFull())
		{
			return false;
		}
		m_global.value.WriteRemote(make_pair(parent, value));
	}
	return true;
}

Result Network::OnCycleReadPhase(unsigned int stateIndex)
{
    if (stateIndex == 0)
    {
        // We can only read one thing per cycle:
        // Either a shared to send, or a global to broadcast.
        RegAddr addr = INVALID_REG;
        if (m_sharedResponse.fid != INVALID_GFID && m_sharedResponse.value.m_state != RST_FULL)
        {
			addr = m_allocator.GetSharedAddress( (m_sharedResponse.parent) ? ST_PARENT : ST_LAST, m_sharedResponse.fid, m_sharedResponse.addr);
            if (addr.valid())
            {
                // The thread of the shared has been allocated, read it
                if (!m_regFile.p_asyncR.Read(*this))
                {
                    return FAILED;
                }

				RegValue value;
                if (!m_regFile.ReadRegister(addr, value))
                {
                    return FAILED;
                }

                DebugSimWrite("Read shared G%u:%s from %s: 0x%016llx", m_sharedResponse.fid, m_sharedResponse.addr.str().c_str(), addr.str().c_str(), (uint64_t)value.m_integer);
                
				if (value.m_state == RST_FULL)
				{
					COMMIT
					{
						m_sharedResponse.value  = value;
						m_sharedResponse.parent = false;
					}
				}
				else
				{
				    DebugSimWrite("Discarding shared G%u:%s request: shared not yet written", m_sharedResponse.fid, m_sharedResponse.addr.str().c_str());
				}
            }
			else
			{
			    DebugSimWrite("Discarding shared G%u:%s request: shared not yet allocated", m_sharedResponse.fid, m_sharedResponse.addr.str().c_str());
			}

            if (m_sharedResponse.value.m_state != RST_FULL)
            {
				// If it hasn't been read, ignore it.
                COMMIT{ m_sharedResponse.fid = INVALID_GFID; }
            }
			return SUCCESS;
        }

		if (m_createState == CS_PROCESSING_LOCAL && m_global.count > 0 && m_global.local.m_state == RST_INVALID)
        {
            // We didn't (try to) read a shared, so we're clear to read a global for the outgoing CREATE message
			if (!m_regFile.p_asyncR.Read(*this))
			{
				return FAILED;
			}

			RegAddr  addr = MAKE_REGADDR(m_global.addr.type, m_globalsBase[m_global.addr.type] + m_global.addr.index);
			RegValue value;
			if (!m_regFile.ReadRegister(addr, value))
			{
				return FAILED;
			}
			assert(value.m_state == RST_FULL);
			COMMIT{ m_global.local = value; }
			DebugSimWrite("Read global #%u from %s", m_global.addr.index, addr.str().c_str());
			return SUCCESS;
        }
    }
    return DELAYED;
}

Result Network::OnCycleWritePhase(unsigned int stateIndex)
{
    switch (stateIndex)
    {
    case 0:
        // Incoming shareds channel
        if (m_sharedReceived.fid != INVALID_GFID)
        {
			const Family& family = m_familyTable[m_familyTable.TranslateFamily(m_sharedReceived.fid)];
            if (m_sharedReceived.parent && family.parent.pid != m_parent.GetPID())
            {
                // This is not the CPU the parent thread is located on. Forward it
                if (!SendShared(m_sharedReceived.fid, m_sharedReceived.parent, m_sharedReceived.addr, m_sharedReceived.value))
                {
                    return FAILED;
                }
            }
            else
            {
                DebugSimWrite("Writing shared G%u:%s", m_sharedReceived.fid, m_sharedReceived.addr.str().c_str());
                RegAddr addr = m_allocator.GetSharedAddress(m_sharedReceived.parent ? ST_PARENT : ST_FIRST, m_sharedReceived.fid, m_sharedReceived.addr);
                if (addr.valid())
                {
                    // Write it
                    if (!m_regFile.p_asyncW.Write(*this, addr))
                    {
                        return FAILED;
                    }
                    
                    if (!m_regFile.WriteRegister(addr, m_sharedReceived.value, *this))
                    {
                        return FAILED;
                    }
                    
    				if (m_sharedReceived.parent)
	    			{
		    			if (!m_allocator.DecreaseFamilyDependency(m_familyTable.TranslateFamily(m_sharedReceived.fid), FAMDEP_OUTSTANDING_SHAREDS))
			    		{
				    		return FAILED;
					    }
    				}
                }
            }

            COMMIT{ m_sharedReceived.fid = INVALID_GFID; }
			return SUCCESS;
        }
        break;

    case 1:
        // Outgoing shareds channel
        if (m_sharedResponse.fid != INVALID_GFID && m_sharedResponse.value.m_state == RST_FULL)
        {
            // Response
            DebugSimWrite("Sending shared G%u:%s; parent shared: %s", m_sharedResponse.fid, m_sharedResponse.addr.str().c_str(), m_sharedResponse.parent ? "true" : "false");
			Result res = m_next->OnSharedReceived(m_sharedResponse);
			if (res == FAILED)
            {
                return FAILED;
            }

			if (res == SUCCESS)
			{
				if (m_sharedResponse.parent)
				{
					if (!m_allocator.DecreaseFamilyDependency(m_familyTable.TranslateFamily(m_sharedResponse.fid), FAMDEP_OUTSTANDING_SHAREDS))
					{
						return FAILED;
					}
				}
			}

			if (res != DELAYED || !m_sharedResponse.parent)
			{
				// The family doesn't exist yet, but we can ignore this write if we're
				// not writing to the parent shareds; the family will later request it.
				COMMIT{ m_sharedResponse.fid = INVALID_GFID; }
			}
			return SUCCESS;
		}
        
		if (m_sharedRequest.fid != INVALID_GFID)
        {
            // Request
            if (m_prev->OnSharedRequested(m_sharedRequest) == FAILED)
            {
                return FAILED;
            }
            COMMIT{ m_sharedRequest.fid = INVALID_GFID; }
			return SUCCESS;
        }
        break;

    case 2:
        // Thread and family completion channel
        if (m_cleanedUpThread.IsFull())
        {
            if (!m_allocator.OnRemoteThreadCleanup(m_familyTable.TranslateFamily(m_cleanedUpThread.Read())))
            {
                return FAILED;
            }
            m_cleanedUpThread.Clear();
			return SUCCESS;
        }
        
		if (m_completedThread.IsFull())
        {
            if (!m_allocator.OnRemoteThreadCompletion(m_familyTable.TranslateFamily(m_completedThread.Read())))
            {
                return FAILED;
            }
            m_completedThread.Clear();
			return SUCCESS;
        }
        
		if (m_completedFamily.IsFull())
        {
			if (!m_allocator.DecreaseFamilyDependency(m_familyTable.TranslateFamily(m_completedFamily.Read()), FAMDEP_PREV_TERMINATED))
            {
                return FAILED;
            }
            m_completedFamily.Clear();
			return SUCCESS;
        }
        break;

    case 3:
		if (m_createState == CS_PROCESSING_NONE)
		{
			// We're not processing a create, check if there is a remote or local create
			if (m_createRemote.IsFull())
			{
				// Process the received create
				const CreateMessage& msg = m_createRemote.Read();

				// Forward the create
				if (!m_next->OnFamilyCreateReceived(msg))
				{
					return FAILED;
				}

				LFID fid = m_allocator.AllocateFamily(msg);
				if (fid == INVALID_LFID)
				{
					return FAILED;
				}

				const Family& family = m_familyTable[fid];

				m_global.count = 0;
				for (RegType i = 0; i < NUM_REG_TYPES; i++)
				{
					m_globalsBase[i] = family.regs[i].globals;
					if (m_global.count == 0 && msg.regsNo[i].globals > 0)
					{
						m_global.count = msg.regsNo[i].globals;
						m_global.addr  = MAKE_REGADDR(i, 0);
					}
				}
				
				DebugSimWrite("Allocated family F%u for G%u: %u globals left initially", fid, msg.fid, m_global.count);
				if (m_global.count > 0)
				{
					// There are still globals to process, otherwise we can 
					// process another create next cycle
					DebugSimWrite("Processing remote create");
					COMMIT{ m_createState = CS_PROCESSING_REMOTE; }
				}
				else if (!m_allocator.ActivateFamily(fid))
				{
					return FAILED;
				}
				COMMIT{ m_createRemote.Clear(); }
				COMMIT{ m_createFid = fid; }
				return SUCCESS;
			}

			if (m_createLocal.IsFull())
			{
				const pair<LFID, CreateMessage>& create = m_createLocal.Read();
				const CreateMessage& msg = create.second;

				const Family& family = m_familyTable[create.first];

				m_global.count = 0;
				for (RegType i = 0; i < NUM_REG_TYPES; i++)
				{
					m_globalsBase[i] = family.regs[i].globals;
					if (m_global.count == 0 && msg.regsNo[i].globals > 0)
					{
						m_global.count = msg.regsNo[i].globals;
						m_global.addr  = MAKE_REGADDR(i, 0);
					}
				}

				// Send the create
				if (!m_next->OnFamilyCreateReceived(msg))
				{
					return FAILED;
				}

                DebugSimWrite("Sent local family create G%u: %u globals left initially (type %d)", msg.fid, m_global.count, m_global.addr.type);
				if (m_global.count > 0)
				{
					// There are still globals to process, otherwise we can 
					// process another create next cycle
					DebugSimWrite("Processing local create");
					COMMIT{ m_createState = CS_PROCESSING_LOCAL; }
				}
				COMMIT{ m_createFid = create.first; }
				COMMIT{ m_createLocal.Clear(); }
				return SUCCESS;
			}
		}
		else
		{
			const Family* family = NULL;
			
			if (m_createState == CS_PROCESSING_LOCAL)
			{
				if (!m_global.value.IsLocalFull() && m_global.local.m_state != RST_INVALID)
				{
					// Forward the read global
					assert(m_global.local.m_state == RST_FULL);
					DebugSimWrite("Sent global %s", m_global.addr.str().c_str());

					family = &m_familyTable[ m_createFid ];

					m_global.value.WriteLocal(make_pair(family->parent.pid, m_global.local));
					COMMIT{ m_global.local.m_state = RST_INVALID; }
				}
			}
			else if (m_createState == CS_PROCESSING_REMOTE)
			{
				if (m_global.value.IsRemoteFull() && !m_global.value.IsRemoteProcessed())
				{
					// We've received a global, write it
					RegAddr addr = MAKE_REGADDR(m_global.addr.type, m_globalsBase[m_global.addr.type] + m_global.addr.index);
					if (!m_regFile.p_asyncW.Write(*this, addr))
					{
						return FAILED;
					}

                    DebugSimWrite("Writing global %s to %s", m_global.addr.str().c_str(), addr.str().c_str());
					if (!m_regFile.WriteRegister(addr, m_global.value.ReadRemote().second))
					{
						return FAILED;
					}

					m_global.value.SetRemoteProcessed();
					family = &m_familyTable[ m_createFid ];
				}
			}

			if (family != NULL)
			{
				// Reset and increment for next global
				bool done = false;
				if (m_global.count == 1)
				{
					// Last global of this type, go to the next type
					done = true;
					COMMIT{ m_global.count = 0; }
					for (RegType i = m_global.addr.type + 1; i < NUM_REG_TYPES; i++)
					{
						if (family->regs[i].count.globals > 0)
						{
							COMMIT{ m_global.count = family->regs[i].count.globals; }
							COMMIT{ m_global.addr  = MAKE_REGADDR(i, 0); }
							done = false;
							break;
						}
					}
				}
				else
				{
					COMMIT{ m_global.count--; }
					COMMIT{ m_global.addr.index++; }
				}

				if (done)
				{
				    DebugSimWrite("Done with creation for F%u; remote: %s", m_createFid, m_createState == CS_PROCESSING_REMOTE ? "true" : "false");
					for (RegType i = 0; i < NUM_REG_TYPES; i++)
					{
					    DebugSimWrite("#Globals: %u, #Shareds: %u", family->regs[i].globals, family->regs[i].shareds);
					}

					// We've read or written all globals for this create
					if (m_createState == CS_PROCESSING_REMOTE)
					{
						// For remote creates we can now activate the family (start creating threads)
						if (!m_allocator.ActivateFamily(m_createFid))
						{
							return FAILED;
						}
					}
					
					DebugSimWrite("Processing no create");
					COMMIT{ m_createState = CS_PROCESSING_NONE; }
				}
		    	return SUCCESS;
        	}
		}
        break;

	case 7:
		if (m_global.value.IsSendingFull())
		{
			// Forward a global
			const pair<PID,RegValue>& p = m_global.value.ReadSending();
			if (!m_next->OnGlobalReceived(p.first, p.second))
			{
				return FAILED;
			}
			m_global.value.SetSendingForwarded();
			return SUCCESS;
		}
		break;

    case 4:
        if (m_unreservation.IsRemoteFull() && !m_unreservation.IsRemoteProcessed())
        {
            // Process the unreservation
            if (!m_familyTable.UnreserveGlobal(m_unreservation.ReadRemote().fid))
            {
                return FAILED;
            }
            m_unreservation.SetRemoteProcessed();
			return SUCCESS;
        }
        break;

    case 5:
        if (m_reservation.IsRemoteFull() && !m_reservation.IsRemoteProcessed())
        {
            // Process the reservation
            if (!m_familyTable.ReserveGlobal(m_reservation.ReadRemote().fid))
            {
                return FAILED;
            }
            m_reservation.SetRemoteProcessed();
			return SUCCESS;
        }
        break;

    case 6:
        if (m_unreservation.IsSendingFull())
        {
            if (!m_next->OnFamilyUnreservationReceived(m_unreservation.ReadSending()))
            {
                return FAILED;
            }
            m_unreservation.SetSendingForwarded();
			return SUCCESS;
        }
        
		if (m_reservation.IsSendingFull())
        {
            if (!m_next->OnFamilyReservationReceived(m_reservation.ReadSending()))
            {
                return FAILED;
            }
            m_reservation.SetSendingForwarded();
			return SUCCESS;
        }
		
		if (m_hasToken.Read())
		{
			// We have the token
			if (m_wantToken.Read())
			{
				// We want it as well, approve the Create
				if (!m_allocator.OnTokenReceived())
				{
					return FAILED;
				}
				m_wantToken.Write(false);
				COMMIT{ m_lockToken++; }	// We will send a create, so lock the token
				return SUCCESS;
			}

			if (m_nextWantsToken.Read() && m_lockToken == 0)
			{
				// Pass the token to the next CPU
				DebugSimWrite("Sending token to next processor");
				if (!m_next->OnTokenReceived())
				{
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
				return FAILED;
			}
			// Set a flag to prevent us from spamming the previous CPU
			m_requestedToken.Write(true);
			return SUCCESS;
		}
        break;
    }

	// Nothing to do
    return DELAYED;
}

