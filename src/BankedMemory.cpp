#include "BankedMemory.h"
#include <sstream>
#include <cassert>
#include <cmath>
#include <cstring>

using namespace Simulator;
using namespace std;

BankedMemory::Bank::Bank()
    : busy(false)
{
}

void BankedMemory::Request::release()
{
    if (refcount != NULL && --*refcount == 0) {
        delete[] (char*)data.data;
        delete refcount;
    }
}

BankedMemory::Request& BankedMemory::Request::operator =(const Request& req)
{
    release();
    refcount  = req.refcount;
    callback  = req.callback;
    write     = req.write;
    address   = req.address;
    data      = req.data;
    ++*refcount;
    return *this;
}

BankedMemory::Request::Request(const Request& req) : refcount(NULL) { *this = req; }
BankedMemory::Request::Request() { refcount = new unsigned long(1); data.data = NULL; }
BankedMemory::Request::~Request() { release(); }

void BankedMemory::RegisterListener(IMemoryCallback& callback)
{
    m_caches.insert(&callback);
}

void BankedMemory::UnregisterListener(IMemoryCallback& callback)
{
    m_caches.erase(&callback);
}

size_t BankedMemory::GetBankFromAddress(MemAddr address) const
{
    // We work on whole cache lines
    address >>= 6;

    uint64_t hash = (31 * address / m_banks.size() / 32);

    return (size_t)((hash + address) % m_banks.size());
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
        done += (request.data.size + m_config.sizeOfLine - 1) / m_config.sizeOfLine;
    }
    
    // At least one size for the 'header'
    done++;
    
    queue.insert(make_pair(done, request));
}

Result BankedMemory::Read(IMemoryCallback& callback, MemAddr address, void* /* data */, 
                          MemSize size, MemTag tag)
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

    Pipeline& queue = m_banks[ GetBankFromAddress(address) ].incoming;
	if (m_config.bufferSize == INFINITE || queue.size() < m_config.bufferSize)
    {
        COMMIT
        {
            Request request;
            request.address   = address;
            request.callback  = &callback;
            request.data.data = new char[ (size_t)size ];
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
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

    Pipeline& queue = m_banks[ GetBankFromAddress(address) ].incoming;
    if (m_config.bufferSize == INFINITE || queue.size() < m_config.bufferSize)
    {
		assert(tag.fid != INVALID_LFID);
		
        Request request;
        request.address   = address;
        request.callback  = &callback;
        request.data.data = new char[ (size_t)size ];
        request.data.size = size;
        request.data.tag  = tag;
        request.write     = true;
        memcpy(request.data.data, data, size);

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
                    CycleNo delay = m_config.baseRequestTime + m_config.timePerLine * (request.data.size + m_config.sizeOfLine - 1) / m_config.sizeOfLine;
                            
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

static string CreateStateNames(size_t numBanks)
{
    stringstream states;
    for (size_t i = 0; i < numBanks; ++i)
    {
        states << "in"  << i << "|" <<
                  "out" << i << "|";
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

BankedMemory::BankedMemory(Object* parent, Kernel& kernel, const std::string& name, 
                           const Config& config, size_t /* nProcs */) :
    IComponent(parent, kernel, name, CreateStateNames(config.numBanks)),
    m_config(config), m_banks(config.numBanks)
{
}
