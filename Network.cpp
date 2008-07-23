#include <cassert>
#include "Network.h"
#include "Allocator.h"
#include "Processor.h"
#include "FamilyTable.h"
using namespace Simulator;
using namespace std;

Network::Network(Processor& parent, const std::string& name, Allocator& alloc, RegisterFile& regFile, FamilyTable& familyTable) :
    IComponent(&parent, parent.getKernel(), name, 8),
    m_parent(parent), m_regFile(regFile), m_familyTable(familyTable), m_allocator(alloc),
    m_prev(NULL), m_next(NULL),
	
	m_createLocal(parent.getKernel()), m_createRemote(parent.getKernel()), m_createState(CS_PROCESSING_NONE),
	m_global(parent.getKernel()),

    m_reservation(parent.getKernel()), m_unreservation(parent.getKernel()),
    m_completedFamily(parent.getKernel()), m_completedThread(parent.getKernel()), m_cleanedUpThread(parent.getKernel()),
    
	m_hasToken(parent.getKernel(), parent.getPID() == 0), // CPU #0 starts out with the token
	m_wantToken(parent.getKernel(), false), m_nextWantsToken(parent.getKernel(), false), m_requestedToken(parent.getKernel(), false)
{
    parent.getKernel().registerComponent(*this, 1);
    m_lockToken            = 0;
	m_global.count         = 0;
	m_global.local.m_state = RST_INVALID;
}

void Network::initialize(Network& prev, Network& next)
{
    m_prev = &prev;
    m_next = &next;
}

bool Network::sendThreadCleanup   (GFID fid) { return m_next->onThreadCleanedUp(fid); }
bool Network::sendThreadCompletion(GFID fid) { return m_prev->onThreadCompleted(fid); }
bool Network::sendFamilyCompletion(GFID fid) { return m_next->onFamilyCompleted(fid); }

bool Network::sendShared(GFID fid, bool parent, const RegAddr& addr, const RegValue& value)
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

bool Network::requestShared(GFID fid, const RegAddr& addr, bool parent)
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

Result Network::onSharedRequested(const SharedInfo& sharedInfo)
{
    if (m_sharedResponse.fid == INVALID_GFID)
    {	
		// If the family hasn't been created yet, we just ignore the request
		if (m_familyTable.TranslateFamily(sharedInfo.fid) != INVALID_LFID)
		{
			DebugSimWrite("Shared request stored\n");
			COMMIT
			{
				m_sharedResponse = sharedInfo;
				m_sharedResponse.value.m_state = RST_INVALID;
			}
		}
		else
		{
			DebugSimWrite("Shared request ignored\n");
		}
        return SUCCESS;
    }
    return FAILED;
}

Result Network::onSharedReceived(const SharedInfo& sharedInfo)
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

bool Network::onThreadCleanedUp(GFID fid)
{
    assert(fid != INVALID_GFID);
    if (m_cleanedUpThread.empty())
    {
		DebugSimWrite("Received OnThreadCleanup\n");
        m_cleanedUpThread.write(fid);
        return true;
    }
    return false;
}

bool Network::onThreadCompleted(GFID fid)
{
    assert(fid != INVALID_GFID);
    if (m_completedThread.empty())
    {
		DebugSimWrite("Received OnThreadCompleted\n");
        m_completedThread.write(fid);
        return true;
    }
    return false;
}

bool Network::onFamilyCompleted(GFID fid)
{
    assert(fid != INVALID_GFID);
    if (m_completedFamily.empty())
    {
		DebugSimWrite("Received OnFamilyCompleted\n");
        m_completedFamily.write(fid);
        return true;
    }
    return false;
}

bool Network::sendFamilyCreate(LFID fid)
{
	if (!m_createLocal.full())
    {
        assert(m_hasToken.read());
        COMMIT
        {
            // Buffer the family information
			const Family& family = m_familyTable[fid];

            CreateMessage message;
			message.fid           = family.gfid;
            message.start         = family.start;
			message.step          = family.step;
			message.lastThread    = family.lastThread;
			message.virtBlockSize = family.virtBlockSize;
			message.address       = family.pc;
			message.parent        = family.parent;

            for (RegType i = 0; i < NUM_REG_TYPES; i++)
            {
				message.regsNo[i] = family.regs[i].count;
			}

			m_createLocal.write(make_pair(fid, message));
			DebugSimWrite("Broadcasting create for G%u (F%u)\n", message.fid, fid);
        }
        return true;
    }
    return false;
}

bool Network::onFamilyCreateReceived(const CreateMessage& msg)
{
    if (msg.parent.pid != m_parent.getPID())
    {
		if (m_createRemote.full())
		{
			return false;
		}

		m_createRemote.write(msg);
		DebugSimWrite("Received create for G%u\n", msg.fid);
    }
	else
	{
		// Create has come full circle
		COMMIT{ m_lockToken--; }
	}
    return true;
}

bool Network::requestToken()
{
    // Called by the Allocator when it wants to do a global create
    m_wantToken.write(true);
    return true;
}

bool Network::onTokenReceived()
{
    m_hasToken.write(true);
	m_requestedToken.write(false);
    return true;
}

bool Network::onRemoteTokenRequested()
{
    m_nextWantsToken.write(true);
    return true;
}

bool Network::sendFamilyReservation(GFID fid)
{
    if (!m_reservation.isLocalFull())
    {
        m_reservation.writeLocal(RemoteFID(fid, m_parent.getPID()));
        return true;
    }
    return false;
}

bool Network::onFamilyReservationReceived(const RemoteFID& rfid)
{
	if (rfid.pid != m_parent.getPID())
    {
		DebugSimWrite("Received reservation for G%u\n", rfid.fid);
		if (m_reservation.isRemoteFull())
		{
			return false;
        }
		m_reservation.writeRemote(rfid);
    }
	else
	{
		m_allocator.onReservationComplete();
	}
	return true;
}

bool Network::sendFamilyUnreservation(GFID fid)
{
    if (!m_unreservation.isLocalFull())
    {
        m_unreservation.writeLocal(RemoteFID(fid, m_parent.getPID()));
        return true;
    }
    return false;
}

bool Network::onFamilyUnreservationReceived(const RemoteFID& rfid)
{
    if (rfid.pid != m_parent.getPID())
	{
		if (m_unreservation.isRemoteFull())
		{
	        return false;
        }
        m_unreservation.writeRemote(rfid);
    }
    return true;
}

bool Network::onGlobalReceived(PID parent, const RegValue& value)
{
	assert(value.m_state == RST_FULL);
	if (m_parent.getPID() != parent)
	{
		if (m_createState != CS_PROCESSING_REMOTE || m_global.value.isRemoteFull())
		{
			return false;
		}
		m_global.value.writeRemote(make_pair(parent, value));
	}
	return true;
}

Result Network::onCycleReadPhase(unsigned int stateIndex)
{
    if (stateIndex == 0)
    {
        // We can only read one thing per cycle:
        // Either a shared to send, or a global to broadcast.
        RegAddr addr = INVALID_REG;
        if (m_sharedResponse.fid != INVALID_GFID && m_sharedResponse.value.m_state != RST_FULL)
        {
			addr = m_allocator.getSharedAddress( (m_sharedResponse.parent) ? ST_PARENT : ST_LAST, m_sharedResponse.fid, m_sharedResponse.addr);
            if (addr.valid())
            {
                // The thread of the shared has been allocated, read it
                if (!m_regFile.p_asyncR.read(*this))
                {
                    return FAILED;
                }

				RegValue value;
                if (!m_regFile.readRegister(addr, value))
                {
                    return FAILED;
                }
				
                DebugSimWrite("Read shared from %s: %016llx\n", addr.str().c_str(), value.m_integer);
                
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
					DebugSimWrite("Discarding shared request: shared not yet written\n");
				}
            }
			else
			{
				DebugSimWrite("Discarding shared request: shared not yet allocated\n");
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
			if (!m_regFile.p_asyncR.read(*this))
			{
				return FAILED;
			}

			RegAddr  addr = MAKE_REGADDR(m_global.addr.type, m_globalsBase[m_global.addr.type] + m_global.addr.index);
			RegValue value;
			if (!m_regFile.readRegister(addr, value))
			{
				return FAILED;
			}
			assert(value.m_state == RST_FULL);
			COMMIT{ m_global.local = value; }
			DebugSimWrite("Read global %u: %d\n", addr.index, m_global.local.m_state);
			return SUCCESS;
        }
    }
    return DELAYED;
}

Result Network::onCycleWritePhase(unsigned int stateIndex)
{
    switch (stateIndex)
    {
    case 0:
        // Incoming shareds channel
        if (m_sharedReceived.fid != INVALID_GFID)
        {
			const Family& family = m_familyTable[m_familyTable.TranslateFamily(m_sharedReceived.fid)];
            if (m_sharedReceived.parent && family.parent.pid != m_parent.getPID())
            {
                // This is not the CPU the parent thread is located on. Forward it
                if (!sendShared(m_sharedReceived.fid, m_sharedReceived.parent, m_sharedReceived.addr, m_sharedReceived.value))
                {
                    return FAILED;
                }
            }
            else
            {
				DebugSimWrite("Writing shared %s for G%u\n", m_sharedReceived.addr.str().c_str(), m_sharedReceived.fid);
                RegAddr addr = m_allocator.getSharedAddress(m_sharedReceived.parent ? ST_PARENT : ST_FIRST, m_sharedReceived.fid, m_sharedReceived.addr);
                if (addr.valid())
                {
                    // Write it
                    if (!m_regFile.p_asyncW.write(*this, addr))
                    {
                        return FAILED;
                    }
                    
                    if (!m_regFile.writeRegister(addr, m_sharedReceived.value, *this))
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
			DebugSimWrite("Sending shared %s for G%u; parent: %d\n", m_sharedResponse.addr.str().c_str(), m_sharedResponse.fid, m_sharedResponse.parent);
			Result res = m_next->onSharedReceived(m_sharedResponse);
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
            if (m_prev->onSharedRequested(m_sharedRequest) == FAILED)
            {
                return FAILED;
            }
            COMMIT{ m_sharedRequest.fid = INVALID_GFID; }
			return SUCCESS;
        }
        break;

    case 2:
        // Thread and family completion channel
        if (m_cleanedUpThread.full())
        {
            if (!m_allocator.onRemoteThreadCleanup(m_familyTable.TranslateFamily(m_cleanedUpThread.read())))
            {
                return FAILED;
            }
            m_cleanedUpThread.clear();
			return SUCCESS;
        }
        
		if (m_completedThread.full())
        {
            if (!m_allocator.onRemoteThreadCompletion(m_familyTable.TranslateFamily(m_completedThread.read())))
            {
                return FAILED;
            }
            m_completedThread.clear();
			return SUCCESS;
        }
        
		if (m_completedFamily.full())
        {
			if (!m_allocator.DecreaseFamilyDependency(m_familyTable.TranslateFamily(m_completedFamily.read()), FAMDEP_PREV_TERMINATED))
            {
                return FAILED;
            }
            m_completedFamily.clear();
			return SUCCESS;
        }
        break;

    case 3:
		if (m_createState == CS_PROCESSING_NONE)
		{
			// We're not processing a create, check if there is a remote or local create
			if (m_createRemote.full())
			{
				// Process the received create
				const CreateMessage& msg = m_createRemote.read();

				// Forward the create
				if (!m_next->onFamilyCreateReceived(msg))
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
				
				DebugSimWrite("Allocated family F%u for G%u: %u globals left initially\n", fid, msg.fid, m_global.count);
				if (m_global.count > 0)
				{
					// There are still globals to process, otherwise we can 
					// process another create next cycle
					DebugSimWrite("Processing remote create\n");
					COMMIT{ m_createState = CS_PROCESSING_REMOTE; }
				}
				else if (!m_allocator.ActivateFamily(fid))
				{
					return FAILED;
				}
				COMMIT{ m_createRemote.clear(); }
				COMMIT{ m_createFid = fid; }
				return SUCCESS;
			}

			if (m_createLocal.full())
			{
				const pair<LFID, CreateMessage>& create = m_createLocal.read();
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
				if (!m_next->onFamilyCreateReceived(msg))
				{
					return FAILED;
				}

				DebugSimWrite("Sent local family create G%u: %u globals left initially (type %d)\n", msg.fid, m_global.count, m_global.addr.type);
				if (m_global.count > 0)
				{
					// There are still globals to process, otherwise we can 
					// process another create next cycle
					DebugSimWrite("Processing local create\n");
					COMMIT{ m_createState = CS_PROCESSING_LOCAL; }
				}
				COMMIT{ m_createFid = create.first; }
				COMMIT{ m_createLocal.clear(); }
				return SUCCESS;
			}
		}
		else
		{
			const Family* family = NULL;
			
			if (m_createState == CS_PROCESSING_LOCAL)
			{
				if (!m_global.value.isLocalFull() && m_global.local.m_state != RST_INVALID)
				{
					// Forward the read global
					assert(m_global.local.m_state == RST_FULL);
					DebugSimWrite("Sent global %s\n", m_global.addr.str().c_str());

					family = &m_familyTable[ m_createFid ];

					m_global.value.writeLocal(make_pair(family->parent.pid, m_global.local));
					COMMIT{ m_global.local.m_state = RST_INVALID; }
				}
			}
			else if (m_createState == CS_PROCESSING_REMOTE)
			{
				if (m_global.value.isRemoteFull() && !m_global.value.isRemoteProcessed())
				{
					// We've received a global, write it
					RegAddr addr = MAKE_REGADDR(m_global.addr.type, m_globalsBase[m_global.addr.type] + m_global.addr.index);
					if (!m_regFile.p_asyncW.write(*this, addr))
					{
						return FAILED;
					}

					DebugSimWrite("Writing global %s to %u\n", m_global.addr.str().c_str(), addr.index);
					if (!m_regFile.writeRegister(addr, m_global.value.readRemote().second))
					{
						return FAILED;
					}

					m_global.value.setRemoteProcessed();
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
					DebugSimWrite("Done with creation for F%u; remote=%d\n", m_createFid, m_createState == CS_PROCESSING_REMOTE);
					for (RegType i = 0; i < NUM_REG_TYPES; i++)
					{
						DebugSimWrite("Globals: %d, Shareds: %d\n", family->regs[i].globals, family->regs[i].shareds);
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
					
					DebugSimWrite("Processing no create\n");
					COMMIT{ m_createState = CS_PROCESSING_NONE; }
				}
		    	return SUCCESS;
        	}
		}
        break;

	case 7:
		if (m_global.value.isSendingFull())
		{
			// Forward a global
			const pair<PID,RegValue>& p = m_global.value.readSending();
			if (!m_next->onGlobalReceived(p.first, p.second))
			{
				return FAILED;
			}
			m_global.value.setSendingForwarded();
			return SUCCESS;
		}
		break;

    case 4:
        if (m_unreservation.isRemoteFull() && !m_unreservation.isRemoteProcessed())
        {
            // Process the unreservation
            if (!m_familyTable.UnreserveGlobal(m_unreservation.readRemote().fid))
            {
                return FAILED;
            }
            m_unreservation.setRemoteProcessed();
			return SUCCESS;
        }
        break;

    case 5:
        if (m_reservation.isRemoteFull() && !m_reservation.isRemoteProcessed())
        {
            // Process the reservation
            if (!m_familyTable.ReserveGlobal(m_reservation.readRemote().fid))
            {
                return FAILED;
            }
            m_reservation.setRemoteProcessed();
			return SUCCESS;
        }
        break;

    case 6:
        if (m_unreservation.isSendingFull())
        {
            if (!m_next->onFamilyUnreservationReceived(m_unreservation.readSending()))
            {
                return FAILED;
            }
            m_unreservation.setSendingForwarded();
			return SUCCESS;
        }
        
		if (m_reservation.isSendingFull())
        {
            if (!m_next->onFamilyReservationReceived(m_reservation.readSending()))
            {
                return FAILED;
            }
            m_reservation.setSendingForwarded();
			return SUCCESS;
        }
		
		if (m_hasToken.read())
		{
			// We have the token
			if (m_wantToken.read())
			{
				// We want it as well, approve the Create
				if (!m_allocator.onTokenReceived())
				{
					return FAILED;
				}
				m_wantToken.write(false);
				COMMIT{ m_lockToken++; }	// We will send a create, so lock the token
				return SUCCESS;
			}

			if (m_nextWantsToken.read() && m_lockToken == 0)
			{
				// Pass the token to the next CPU
				DebugSimWrite("Sending token to next\n");
				if (!m_next->onTokenReceived())
				{
					return FAILED;
				}
				m_nextWantsToken.write(false);
				m_hasToken.write(false);
				return SUCCESS;
			}
		}
		// We don't have the token
		else if ((m_wantToken.read() || m_nextWantsToken.read()) && !m_requestedToken.read())
		{
			// But we, or m_next, wants it, so request it.
			if (!m_prev->onRemoteTokenRequested())
			{
				return FAILED;
			}
			// Set a flag to prevent us from spamming the previous CPU
			m_requestedToken.write(true);
			return SUCCESS;
		}
        break;
    }

	// Nothing to do
    return DELAYED;
}

