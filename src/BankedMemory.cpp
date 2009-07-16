#include "BankedMemory.h"
#include "config.h"
#include <sstream>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iomanip>
using namespace std;

namespace Simulator
{

struct BankedMemory::Request
{
    IMemoryCallback* callback;
    bool             write;
    MemAddr          address;
    MemData          data;
    CycleNo          done;
};

struct BankedMemory::Bank
{
    ArbitratedService p_incoming;
    Buffer<Request>   incoming;
    Buffer<Request>   outgoing;
    SensitiveFlag     busy;
    Request           request;

    Bank(Kernel& kernel, IComponent& component, int s_in, int s_bank, int s_out, BufferSize buffersize)
        : p_incoming(component, "incoming"),
          incoming  (kernel, component, s_in,   buffersize),
          outgoing  (kernel, component, s_out,  buffersize),
          busy      (kernel, component, s_bank, false)
    {
    }
};

void BankedMemory::RegisterListener(PSize pid, IMemoryCallback& callback, const ArbitrationSource* sources)
{
    for (; sources->first != NULL; ++sources)
    {
        for (size_t i = 0; i < m_banks.size(); ++i)
        {
            Bank& bank = *m_banks[i];
            bank.p_incoming.AddSource(*sources);
        }
    }
    
    ArbitratedService* &service = m_clients.insert(ClientMap::value_type(&callback, NULL)).first->second;
    if (service == NULL)
    {
        stringstream name;
        name << "client-" << pid << endl;
        service = new ArbitratedService(*this, name.str());
        
        // Add all outgoing bank processes as sources
        for (size_t i = 0; i < m_banks.size(); ++i)
        {
            service->AddSource(ArbitrationSource(this, 3*i+2));
        }
    }
}

void BankedMemory::UnregisterListener(PSize /*pid*/, IMemoryCallback& callback)
{
    m_clients.erase(&callback);
}

size_t BankedMemory::GetBankFromAddress(MemAddr address) const
{
    // We work on whole cache lines
    return (size_t)((address / m_cachelineSize) % m_banks.size());
}

bool BankedMemory::AddRequest(Buffer<Request>& queue, Request& request, bool data)
{
    // Get the initial delay, independent of message size
    COMMIT
    {
        const CycleNo now  = GetKernel()->GetCycleNo();
        request.done = now + (CycleNo)(log((double)m_banks.size()) / log(2.0));
    
        Buffer<Request>::const_reverse_iterator p = queue.rbegin();
        if (p != queue.rend() && request.done < p->done)
        {
            // We're waiting for another message to be sent, so skip the initial delay
            // (pretend our data comes after the data of the previous message)
            request.done = p->done;
        }
    
        if (data)
        {
            // This delay is the time is takes for the message body to traverse the network
            request.done += (request.data.size + m_sizeOfLine - 1) / m_sizeOfLine;
        }
    
        // At least one size for the 'header'
        request.done++;
    }
    
    if (!queue.Push(request))
    {
        return false;
    }
    return true;
}

bool BankedMemory::Read(IMemoryCallback& callback, MemAddr address, MemSize size, MemTag tag)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    Bank& bank = *m_banks[ GetBankFromAddress(address) ];    
    if (!bank.p_incoming.Invoke())
    {
        return false;
    }
    
    Request request;
    request.address   = address;
    request.callback  = &callback;
    request.data.size = size;
    request.data.tag  = tag;
    request.write     = false;
    
    if (!AddRequest(bank.incoming, request, false))
    {
        return false;
    }
    return true;
}

bool BankedMemory::Write(IMemoryCallback& callback, MemAddr address, const void* data, MemSize size, MemTag tag)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    Bank& bank = *m_banks[ GetBankFromAddress(address) ];    
    if (!bank.p_incoming.Invoke())
    {
        return false;
    }

    Request request;
    request.address   = address;
    request.callback  = &callback;
    request.data.size = size;
    request.data.tag  = tag;
    request.write     = true;
    memcpy(request.data.data, data, (size_t)size);

    // Broadcast the snoop data
    for (ClientMap::iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (!p->first->OnMemorySnooped(request.address, request.data))
        {
            return false;
        }
    }

    if (!AddRequest(bank.incoming, request, true))
    {
        return false;
    }
    return true;
}

void BankedMemory::Reserve(MemAddr address, MemSize size, int perm)
{
    return VirtualMemory::Reserve(address, size, perm);
}

void BankedMemory::Unreserve(MemAddr address)
{
    return VirtualMemory::Unreserve(address);
}

bool BankedMemory::Allocate(MemSize size, int perm, MemAddr& address)
{
    return VirtualMemory::Allocate(size, perm, address);
}

void BankedMemory::Read(MemAddr address, void* data, MemSize size)
{
    return VirtualMemory::Read(address, data, size);
}

void BankedMemory::Write(MemAddr address, const void* data, MemSize size)
{
	return VirtualMemory::Write(address, data, size);
}

bool BankedMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
	return VirtualMemory::CheckPermissions(address, size, access);
}

Result BankedMemory::OnCycle(unsigned int stateIndex)
{
    const CycleNo now = GetKernel()->GetCycleNo();
    Bank& bank = *m_banks[stateIndex / 3];
    switch (stateIndex % 3)
    {
    case 0:
    case 2:
    {
        // Process the incoming or outgoing queue
        const bool     incoming = (stateIndex % 3 == 0);
        Buffer<Request>&  queue = (incoming) ? bank.incoming : bank.outgoing;   
        assert(!queue.Empty());
    
        const Request& request = queue.Front();
        if (now >= request.done)
        {
            // This request has arrived, process it
            if (incoming)
            {
                // Incoming pipeline, send it to the bank
                if (bank.busy.IsSet())
                {
                    return FAILED;
                }
                    
                // Calculate bank read/write delay
                const CycleNo delay = m_baseRequestTime + m_timePerLine * (request.data.size + m_sizeOfLine - 1) / m_sizeOfLine;
                            
                bank.request = request;
                bank.request.done = now + delay;
                bank.busy.Set();
            }
            else
            {
                // Outgoing pipeline, send it to the callback
                ClientMap::const_iterator p = m_clients.find(request.callback);
                assert(p != m_clients.end());
                if (!p->second->Invoke())
                {
                    return FAILED;
                }
                
                if (request.write) {
                    if (!request.callback->OnMemoryWriteCompleted(request.data.tag)) {
                        return FAILED;
                    }
                } else {
                    if (!request.callback->OnMemoryReadCompleted(request.data)) {
                        return FAILED;
                    }
                }
            }
            queue.Pop();
        }
        return SUCCESS;
    }
            
    case 1:
        // Process the bank itself
        assert(bank.busy.IsSet());
        if (now >= bank.request.done)
        {
            // This bank is done serving the request
            if (bank.request.write) {
                VirtualMemory::Write(bank.request.address, bank.request.data.data, bank.request.data.size);
            } else {
                VirtualMemory::Read(bank.request.address, bank.request.data.data, bank.request.data.size);
            }

            // Move it to the outgoing queue
            if (!AddRequest(bank.outgoing, bank.request, !bank.request.write))
            {
                return FAILED;
            }
            
            bank.busy.Clear();
        }
        return SUCCESS;
    }
    return DELAYED;
}

static size_t GetNumBanks(const Config& config)
{
    const vector<PSize> placeSizes = config.getIntegerList<PSize>("NumProcessors");
    PSize numProcessors = 0;
    for (size_t i = 0; i < placeSizes.size(); ++i) {
        numProcessors += placeSizes[i];
    }
    return config.getInteger<size_t>("MemoryBanks", numProcessors);
}

static string CreateStateNames(const Config& config)
{
    const size_t numBanks = GetNumBanks(config);
    stringstream states;
    for (size_t i = 0; i < numBanks; ++i)
    {
        states << "in"   << i << "|"
               << "bank" << i << "|"
               << "out"  << i << "|";
    }
    
    string ret = states.str();
    if (!ret.empty()) {
        ret.erase(ret.end() - 1, ret.end());
    }
    return ret;
}

BankedMemory::BankedMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config) :
    IComponent(parent, kernel, name, CreateStateNames(config)),
    m_baseRequestTime(config.getInteger<CycleNo>   ("MemoryBaseRequestTime", 1)),
    m_timePerLine    (config.getInteger<CycleNo>   ("MemoryTimePerLine", 1)),
    m_sizeOfLine     (config.getInteger<size_t>    ("MemorySizeOfLine", 8)),
    m_cachelineSize  (config.getInteger<size_t>    ("CacheLineSize", 64))
{
    const BufferSize buffersize = config.getInteger<BufferSize>("MemoryBufferSize", INFINITE);
    
    m_banks.resize(GetNumBanks(config));
    for (size_t i = 0; i < m_banks.size(); ++i)
    {
        m_banks[i] = new Bank(kernel, *this, 3*i+0, 3*i+1, 3*i+2, buffersize);
    }
}

BankedMemory::~BankedMemory()
{
    for (size_t i = 0; i < m_banks.size(); ++i)
    {
        delete m_banks[i];
    }
}

void BankedMemory::Cmd_Help(ostream& out, const vector<string>& /*arguments*/) const
{
    out <<
    "The Banked Memory represents a switched memory network between P processors and N\n"
    "memory banks. Requests are sequentialized on each bank and the cache line-to-bank\n"
    "mapping is a simple modulo.\n\n"
    "Supported operations:\n"
    "- info <component>\n"
    "  Displays the currently reserved and allocated memory ranges\n\n"
    "- read <component> <start> <size>\n"
    "  Reads the specified number of bytes of raw data from memory from the\n"
    "  specified address\n\n"
    "- read <component> requests\n"
    "  Reads the banks' requests buffers and queues\n";
}

/*static*/ void BankedMemory::PrintRequest(ostream& out, char prefix, const Request& request)
{
    out << prefix << " "
        << hex << setfill('0') << right
        << " 0x" << setw(16) << request.address << " | "
        << setfill(' ') << setw(4) << dec << request.data.size << " | ";

    if (request.data.tag.cid == INVALID_CID) {
        out << " N/A  | ";
    } else {
        out << setw(5) << request.data.tag.cid << " | ";
    }

    if (request.write) {
        out << "Data write";
    } else if (request.data.tag.data) {
        out << "Data read ";
    } else if (request.data.tag.cid != INVALID_CID) {
        out << "Cache-line";
    }
    out << " | " << setw(8) << dec << request.done << " | ";
    
    Object* obj = dynamic_cast<Object*>(request.callback);
    if (obj == NULL) {
        out << "???";
    } else {
        out << obj->GetFQN();
    }
    out << endl;
}

void BankedMemory::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    if (arguments.empty() || arguments[0] != "requests")
    {
        return VirtualMemory::Cmd_Read(out, arguments);
    }

    for (size_t i = 0; i < m_banks.size(); ++i)
    {
        const Bank& bank = *m_banks[i];

        out << "Bank " << dec << i << ":" << endl;
        out << "        Address       | Size |  CID  |    Type    |   Done   | Source" << endl;
        out << "----------------------+------+-------+------------+----------+----------------" << endl;

        for (Buffer<Request>::const_reverse_iterator p = bank.incoming.rbegin(); p != bank.incoming.rend(); ++p)
        {
            PrintRequest(out, '>', *p);
        }
        if (bank.busy.IsSet()) {
            PrintRequest(out, '*', bank.request);
        } else {
            out << "*                     |      |       |            |          |" << endl;
        }
        for (Buffer<Request>::const_reverse_iterator p = bank.outgoing.rbegin(); p != bank.outgoing.rend(); ++p)
        {
            PrintRequest(out, '<', *p);
        }
        out << endl;
    }
}

}
