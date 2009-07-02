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

BankedMemory::Bank::Bank()
    : busy(false)
{
}

void BankedMemory::RegisterListener(PSize /*pid*/, IMemoryCallback& callback)
{
    m_caches.insert(&callback);
}

void BankedMemory::UnregisterListener(PSize /*pid*/, IMemoryCallback& callback)
{
    m_caches.erase(&callback);
}

size_t BankedMemory::GetBankFromAddress(MemAddr address) const
{
    // We work on whole cache lines
    return (size_t)((address / m_cachelineSize) % m_banks.size());
}

void BankedMemory::AddRequest(Pipeline& queue, const Request& request, bool data)
{
    // Get the initial delay, independent of message size
    CycleNo now  = GetKernel()->GetCycleNo();
    CycleNo done = now + (CycleNo)(log((double)m_banks.size()) / log(2.0));
    
    Pipeline::reverse_iterator p = queue.rbegin();
    if (p != queue.rend() && done < p->first)
    {
        // We're waiting for another message to be sent, so skip the initial delay
        // (pretend our data comes after the data of the previous message)
        done = p->first;
    }
    
    if (data)
    {
        // This delay is the time is takes for the message body to traverse the network
        done += (request.data.size + m_sizeOfLine - 1) / m_sizeOfLine;
    }
    
    // At least one size for the 'header'
    done++;
    
    queue.insert(make_pair(done, request));
}

Result BankedMemory::Read(IMemoryCallback& callback, MemAddr address, void* /*data*/, MemSize size, MemTag tag)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    Pipeline& queue = m_banks[ GetBankFromAddress(address) ].incoming;
	if (m_bufferSize == INFINITE || queue.size() < m_bufferSize)
    {
        COMMIT
        {
            Request request;
            request.address   = address;
            request.callback  = &callback;
            request.data.size = size;
            request.data.tag  = tag;
            request.write     = false;
            AddRequest(queue, request, false);
        }
        return DELAYED;
    }
    return FAILED;
}

Result BankedMemory::Write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    Pipeline& queue = m_banks[ GetBankFromAddress(address) ].incoming;
    if (m_bufferSize == INFINITE || queue.size() < m_bufferSize)
    {
		assert(tag.fid != INVALID_LFID);
		
        Request request;
        request.address   = address;
        request.callback  = &callback;
        request.data.size = size;
        request.data.tag  = tag;
        request.write     = true;
        memcpy(request.data.data, data, (size_t)size);

        // Broadcast the snoop data
        for (set<IMemoryCallback*>::iterator p = m_caches.begin(); p != m_caches.end(); ++p)
        {
            if (!(*p)->OnMemorySnooped(request.address, request.data))
            {
                return FAILED;
            }
        }

        COMMIT
        {
            // Calculate completion time
            AddRequest(queue, request, true);
        }
        return DELAYED;
    }
    return FAILED;
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

Result BankedMemory::OnCycleWritePhase(unsigned int stateIndex)
{
    CycleNo now = GetKernel()->GetCycleNo();
    if (stateIndex < 2 * m_banks.size())
    {
        // Process a queue
        bool incoming = (stateIndex % 2) != 0;
        int  index    = (stateIndex / 2);
        Bank&     bank  = m_banks[index];
        Pipeline& queue = (incoming) ? bank.incoming : bank.outgoing;
    
        Pipeline::iterator p = queue.begin();
        if (p == queue.end())
        {
            // Empty queue, nothing to do
            return DELAYED;
        }
        
        if (p->first <= now)
        {
            // This request has arrived, process it
            Request& request = p->second;
    
            if (incoming)
            {
                // Incoming pipeline, send it to the bank
                if (bank.busy)
                {
                    return FAILED;
                }
                    
                COMMIT
                {
                    // Calculate bank read/write delay
                    CycleNo delay = m_baseRequestTime + m_timePerLine * (request.data.size + m_sizeOfLine - 1) / m_sizeOfLine;
                            
                    bank.request = request;
                    bank.busy    = true;
                    bank.done    = now + delay;
                }
            }
            else
            {
                // Outgoing pipeline, send it to the callback
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
            COMMIT{ queue.erase(p); }
        }
    }
    else
    {
        // Process a bank
        size_t index = stateIndex - 2 * m_banks.size();
        Bank& bank = m_banks[index];
        if (!bank.busy)
        {
            // Nothing to do
            return DELAYED;
        }
        
        if (bank.done <= now)
        {
            // This bank is done serving the request
            if (bank.request.write) {
                VirtualMemory::Write(bank.request.address, bank.request.data.data, bank.request.data.size);
            } else {
                VirtualMemory::Read(bank.request.address, bank.request.data.data, bank.request.data.size);
            }

            // Move it to the outgoing queue
            COMMIT
            {
                AddRequest(bank.outgoing, bank.request, !bank.request.write);

                bank.busy = false;
            }
        }
    }             
    return SUCCESS;
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
        states << "out" << i << "|" <<
                  "in"  << i << "|";
    }
    for (size_t i = 0; i < numBanks; ++i)
    {
        states << "bank" << i << "|";
    }
    string ret = states.str();
    if (!ret.empty()) {
        ret.erase(ret.end() - 1, ret.end());
    }
    return ret;
}

BankedMemory::BankedMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config) :
    IComponent(parent, kernel, name, CreateStateNames(config)),
    m_banks          (GetNumBanks(config)),
    m_baseRequestTime(config.getInteger<CycleNo>   ("MemoryBaseRequestTime", 1)),
    m_timePerLine    (config.getInteger<CycleNo>   ("MemoryTimePerLine", 1)),
    m_sizeOfLine     (config.getInteger<size_t>    ("MemorySizeOfLine", 8)),
    m_bufferSize     (config.getInteger<BufferSize>("MemoryBufferSize", INFINITE)),
    m_cachelineSize  (config.getInteger<size_t>    ("CacheLineSize", 64))
{
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

/*static*/ void BankedMemory::PrintRequest(ostream& out, char prefix, const Request& request, CycleNo done)
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
    out << " | " << setw(8) << dec << done << " | ";
    
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
        const Bank& bank = m_banks[i];

        out << "Bank " << dec << i << ":" << endl;
        out << "        Address       | Size |  CID  |    Type    |   Done   | Source" << endl;
        out << "----------------------+------+-------+------------+----------+----------------" << endl;

        for (Pipeline::const_iterator p = bank.incoming.begin(); p != bank.incoming.end(); ++p)
        {
            PrintRequest(out, '>', p->second, p->first);
        }
        if (bank.busy) {
            PrintRequest(out, '*', bank.request, bank.done);
        } else {
            out << "*                     |      |       |            |          |" << endl;
        }
        for (Pipeline::const_iterator p = bank.outgoing.begin(); p != bank.outgoing.end(); ++p)
        {
            PrintRequest(out, '<', p->second, p->first);
        }
        out << endl;
    }
}

}
