#ifndef SIMPLEMEMORY_H
#define SIMPLEMEMORY_H

#include <queue>
#include <set>
#include "Memory.h"
#include "kernel.h"
#include "VirtualMemory.h"

namespace Simulator
{

class SimpleMemory : public IComponent, public IMemory, public IMemoryAdmin, public VirtualMemory
{
public:
	struct Config
	{
        BufferSize bufferSize;
        CycleNo    baseRequestTime;
        CycleNo    timePerLine;
        size_t     sizeOfLine;
	};

    class Request
    {
        void release()
        {
            if (refcount != NULL && --*refcount == 0) {
                delete[] (char*)data.data;
                delete refcount;
            }
        }

    public:
        unsigned long*      refcount;
        bool                write;
        CycleNo             done;
        MemAddr             address;
        MemData             data;
        IMemoryCallback*    callback;

        Request& operator =(const Request& req)
        {
            release();
            refcount  = req.refcount;
            write     = req.write;
            done      = req.done;
            address   = req.address;
            data      = req.data;
            callback  = req.callback;
            ++*refcount;
            return *this;
        }

        Request(const Request& req) : refcount(NULL) { *this = req; }
        Request() { refcount = new unsigned long(1); data.data = NULL; }
        ~Request() { release(); }
    };

    SimpleMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config);

    CycleNo getTotalWaitTime() const { return m_totalWaitTime; }

    // Component
    Result onCycleWritePhase(unsigned int stateIndex);

    // IMemory
    void registerListener  (IMemoryCallback& callback);
    void unregisterListener(IMemoryCallback& callback);
    Result read (IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
    Result write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
	bool checkPermissions(MemAddr address, MemSize size, int access) const;

    // IMemoryAdmin
    void read (MemAddr address, void* data, MemSize size);
    void write(MemAddr address, const void* data, MemSize size);

    const std::queue<Request>& getRequests() const {
        return m_requests;
    }

private:
    std::set<IMemoryCallback*>  m_caches;
    std::queue<Request>         m_requests;
    Config                      m_config;
    CycleNo                     m_totalWaitTime;
};

}
#endif

