#include "ParallelMemory.h"
#include "config.h"
#include <cassert>
#include <sstream>
#include <cstring>
#include <iomanip>
using namespace std;

namespace Simulator
{

struct ParallelMemory::Request
{
    bool    write;
    MemAddr address;
    MemData data;
};

struct ParallelMemory::Port
{
    Buffer<Request>  m_requests;
    CycleNo          m_nextdone;
    IMemoryCallback* m_callback;

    Port(Kernel& kernel, IComponent& component, int state, BufferSize buffersize)
        : m_requests(kernel, buffersize), m_nextdone(0), m_callback(NULL)
    {
        m_requests.Sensitive(component, state);
    }
};

void ParallelMemory::RegisterListener(PSize /*pid*/, IMemoryCallback& callback, const ArbitrationSource* /*sources*/)
{
	size_t index = m_clients.size();
	assert(index < m_ports.size());
    m_ports[index]->m_callback = &callback;
    m_clients.insert(make_pair(&callback, m_ports[index]));
}

void ParallelMemory::UnregisterListener(PSize /*pid*/, IMemoryCallback& callback)
{
    m_clients.erase(&callback);
}

bool ParallelMemory::AddRequest(IMemoryCallback& callback, const Request& request)
{
	map<IMemoryCallback*, Port*>::iterator p = m_clients.find(&callback);
	assert(p != m_clients.end());

	if (!p->second->m_requests.Push(request))
	{
	    return false;
	}
	return true;
}

bool ParallelMemory::Read(IMemoryCallback& callback, MemAddr address, MemSize size, MemTag tag)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    Request request;
    request.address   = address;
    request.data.size = size;
    request.data.tag  = tag;
    request.write     = false;
    
    if (!AddRequest(callback, request))
    {
        return false;
    }
    return true;
}

bool ParallelMemory::Write(IMemoryCallback& callback, MemAddr address, const void* data, MemSize size, MemTag tag)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    Request request;
    request.address   = address;
    request.data.size = size;
    request.data.tag  = tag;
    request.write     = true;
    memcpy(request.data.data, data, (size_t)size);

    // Broadcast the snoop data
    for (map<IMemoryCallback*, Port*>::iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (!p->first->OnMemorySnooped(request.address, request.data))
        {
            return false;
        }
    }

    if (!AddRequest(callback, request))
    {
        return false;
    }
    return true;
}

bool ParallelMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
	return VirtualMemory::CheckPermissions(address, size, access);
}

Result ParallelMemory::OnCycle(unsigned int stateIndex)
{
    Port& port = *m_ports[stateIndex];
    assert(!port.m_requests.Empty());
    
    const Request& request = port.m_requests.Front();
    const CycleNo  now     = GetKernel()->GetCycleNo();

    if (port.m_nextdone > 0)
    {
        // There is a request active
        if (now >= port.m_nextdone)
        {
    	    // The current request has completed
            if (request.write) {
                VirtualMemory::Write(request.address, request.data.data, request.data.size);
                if (!port.m_callback->OnMemoryWriteCompleted(request.data.tag))
                {
                    return FAILED;
                }
                COMMIT {
                    ++m_nwrites;
                    m_nwrite_bytes += request.data.size;
                }
            } else {
                MemData data(request.data);
                VirtualMemory::Read(request.address, data.data, data.size);
                if (!port.m_callback->OnMemoryReadCompleted(data))
                {
                    return FAILED;
                }
                COMMIT {
                    ++m_nreads;
                    m_nread_bytes += request.data.size;
                }
            }
            port.m_requests.Pop();
            COMMIT{ port.m_nextdone = 0; }
        }
    }
    else
    {
        // A new request is ready to be handled
        COMMIT
        {
            // Time the request
            CycleNo requestTime = m_baseRequestTime + m_timePerLine * (request.data.size + m_sizeOfLine - 1) / m_sizeOfLine;
            port.m_nextdone = now + requestTime;
    	}
    }
    return SUCCESS;
}

void ParallelMemory::Reserve(MemAddr address, MemSize size, int perm)
{
    return VirtualMemory::Reserve(address, size, perm);
}

void ParallelMemory::Unreserve(MemAddr address)
{
    return VirtualMemory::Unreserve(address);
}

bool ParallelMemory::Allocate(MemSize size, int perm, MemAddr& address)
{
    return VirtualMemory::Allocate(size, perm, address);
}
    
void ParallelMemory::Read (MemAddr address, void* data, MemSize size)
{
	return VirtualMemory::Read(address, data, size);
}

void ParallelMemory::Write(MemAddr address, const void* data, MemSize size)
{
	return VirtualMemory::Write(address, data, size);
}

static size_t GetNumProcessors(const Config& config)
{
    const vector<PSize> places = config.getIntegerList<PSize>("NumProcessors");
    PSize numProcs = 0;
    for (size_t i = 0; i < places.size(); ++i) {
        numProcs += places[i];
    }
    return numProcs;    
}

static string CreateStateNames(size_t numProcs)
{
    stringstream states;
    for (size_t i = 0; i < numProcs; ++i)
    {
        states << "port" << i << "|";
    }
    string ret = states.str();
    if (!ret.empty()) {
        ret.erase(ret.end() - 1, ret.end());
    }
    return ret;
}

ParallelMemory::ParallelMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config) :
    IComponent(parent, kernel, name, CreateStateNames(GetNumProcessors(config))),
    m_baseRequestTime(config.getInteger<CycleNo>("MemoryBaseRequestTime", 1)),
    m_timePerLine    (config.getInteger<CycleNo>("MemoryTimePerLine", 1)),
    m_sizeOfLine     (config.getInteger<size_t> ("MemorySizeOfLine", 8)),
    m_nreads         (0),
    m_nread_bytes    (0),
    m_nwrites        (0),
    m_nwrite_bytes   (0)
{
    const BufferSize buffersize = config.getInteger<BufferSize>("MemoryBufferSize", INFINITE);
    m_ports.resize(GetNumProcessors(config));
    for (size_t i = 0; i < m_ports.size(); ++i)
    {
        m_ports[i] = new Port(kernel, *this, i, buffersize);
    }
}

ParallelMemory::~ParallelMemory()
{
    for (size_t i = 0; i < m_ports.size(); ++i)
    {
        delete m_ports[i];
    }
}

void ParallelMemory::Cmd_Help(ostream& out, const vector<string>& /*arguments*/) const
{
    out <<
    "The Parallel Memory is an extension on the Ideal Memory in that every CPU has a\n"
    "dedicated port into memory. Internally, there are no conflicts between ports so\n"
    "every CPU can issue memory operations fully independent of all other CPUs.\n\n"
    "Supported operations:\n"
    "- info <component>\n"
    "  Displays the currently reserved and allocated memory ranges\n\n"
    "- read <component> <start> <size>\n"
    "  Reads the specified number of bytes of raw data from memory from the\n"
    "  specified address\n\n"
    "- read <component> requests\n"
    "  Reads the ports' requests buffers\n";
}

void ParallelMemory::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    if (arguments.empty() || arguments[0] != "requests")
    {
        return VirtualMemory::Cmd_Read(out, arguments);
    }

    for (map<IMemoryCallback*, Port*>::const_iterator q = m_clients.begin(); q != m_clients.end(); ++q)
    {
        Port& port = *q->second;
        if (!port.m_requests.Empty())
        {
            out << "Port for: ";
            Object* obj = dynamic_cast<Object*>(q->first);
            if (obj == NULL) {
                out << "???";
            } else {
                out << obj->GetFQN();
            }
            out << endl;
        
            out << "      Address       | Size |  CID  |    Type    " << endl;
            out << "--------------------+------+-------+------------" << endl;

            for (Buffer<Request>::const_iterator p = port.m_requests.begin(); p != port.m_requests.end(); ++p)
            {
                out << hex << setfill('0') << right
                    << " 0x" << setw(16) << p->address << " | "
                    << setfill(' ') << setw(4) << dec << p->data.size << " | ";

                if (p->data.tag.cid == INVALID_CID) {
                    out << " N/A  | ";
                } else {
                    out << setw(5) << p->data.tag.cid << " | ";
                }

                if (p->write) {
                    out << "Data write";
                } else if (p->data.tag.cid != INVALID_CID) {
                    out << "Cache-line";
                } else {
                    out << "Data read ";
                }
                
                out << endl;
            }
            
            out << endl << "First request done at: ";
            if (port.m_nextdone == 0) {
                out << "N/A";
            } else {
                out << dec << port.m_nextdone;
            }
            out << endl << endl;
        }
    }
}

}
