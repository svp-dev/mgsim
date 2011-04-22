#include "NullIO.h"
#include "sim/except.h"
#include <iomanip>

using namespace std;
namespace Simulator
{


    NullIO::NullIO(const std::string& name, Object& parent, Clock& clock)
        : Object(name, parent, clock)
    {
    }
    
    void NullIO::CheckEndPoints(IODeviceID from, IODeviceID to) const
    {
        if (from >= m_clients.size() || m_clients[from] == NULL)
        {
            throw exceptf<SimulationException>(*this, "Read by non-existent device %u", (unsigned)from);
        }

        if (to >= m_clients.size() || m_clients[to] == NULL)
        {
            throw exceptf<SimulationException>(*this, "Read by device %u to non-existence device %u", (unsigned)from, (unsigned)to);
        }

    }
  
    bool NullIO::RegisterClient(IODeviceID id, IIOBusClientCallback& client)
    {
        if (id >= m_clients.size())
        {
            m_clients.resize(id + 1, NULL);
        }
        if (m_clients[id] != NULL)
        {
            throw exceptf<InvalidArgumentException>(*this, "Device number %zu is already registered", id);
        }
        m_clients[id] = &client;
        return true;
    }

    bool NullIO::SendReadRequest(IODeviceID from, IODeviceID to, MemAddr address, MemSize size)
    {
        CheckEndPoints(from, to);

        return m_clients[to]->OnReadRequestReceived(from, address, size);
    }

    bool NullIO::SendReadResponse(IODeviceID from, IODeviceID to, const IOData& data)
    {
        CheckEndPoints(from, to);

        return m_clients[to]->OnReadResponseReceived(from, data);
    }

    bool NullIO::SendWriteRequest(IODeviceID from, IODeviceID to, MemAddr address, const IOData& data)
    {
        CheckEndPoints(from, to);
        return m_clients[to]->OnWriteRequestReceived(from, address, data);
    }

    bool NullIO::SendInterruptRequest(IODeviceID from, IODeviceID to)
    {
        CheckEndPoints(from, to);
        return m_clients[to]->OnInterruptRequestReceived(from);
    }

    bool NullIO::SendInterruptAck(IODeviceID from, IODeviceID to)
    {
        CheckEndPoints(from, to);
        return m_clients[to]->OnInterruptAckReceived(from);
    }

    void NullIO::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
    {
        out << "   ID   | Name" << endl
            << "--------+------------------" << endl;
        for (size_t i = 0; i < m_clients.size(); ++i)
        {
            if (m_clients[i] != NULL)
            {
                out << setw(6) << setfill(' ') << i 
                    << " | " 
                    << m_clients[i]->GetIODeviceName()
                    << endl;
            }
        }
    }

}
