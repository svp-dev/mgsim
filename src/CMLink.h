#ifndef CACHELINK_H
#define CACHELINE_H

#include <queue>
#include <set>
#include "Memory.h"
#include "kernel.h"
//#include "VirtualMemory.h"
#include "config.h"

#include "coma/simlink/linkmgs.h"

#include "coma/simlink/memorydatacontainer.h"

#include "Processor.h"

#include <fstream>

namespace Simulator
{

    class CMLink : public Object, public IMemoryAdmin
    {
    public:
        struct Request
        {
            bool                write;
            CycleNo             done;
            MemAddr             address;
            MemData             data;
            IMemoryCallback*    callback;
            CycleNo             starttime;  // MESMX debug
            bool                bconflict;   
        };
            
        CMLink(const std::string& name, Object& parent, const Config& config, LinkMGS* linkmgs, MemoryDataContainer* mdc=NULL);

        ~CMLink(){
        }

        static std::vector<CMLink*> *s_pLinks;

        void RegisterClient(PSize pid, IMemoryCallback& callback, const Process* processes[]);
        void UnregisterClient(PSize pid);

        void Reserve(MemAddr address, MemSize size, int perm);
        void Unreserve(MemAddr address);

        bool Read (PSize pid, MemAddr address, MemSize size, MemTag tag);
        bool Write(PSize pid, MemAddr address, const void* data, MemSize size, MemTag tag);

        // IMemory
//        Result Read (IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
//        Result Write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
		bool CheckPermissions(MemAddr address, MemSize size, int access) const;
		
		Process p_Requests;

        Result DoRequests();

        // IMemoryAdmin
        bool Allocate(MemSize size, int perm, MemAddr& address);
        void Read (MemAddr address, void* data, MemSize size);
        void Write(MemAddr address, const void* data, MemSize size);

        void GetMemoryStatistics(uint64_t&, uint64_t&, uint64_t&, uint64_t&) const;


//        virtual const std::queue<Request>& GetRequests() {	// MESMX
//			// duplicate the request to queue
//			std::set<Request*>::iterator iter;
//			while(!m_requests.empty())
//			{
//				m_requests.front();
//				//m_requests.pop();
//			}
//
//			for (iter = m_setrequests.begin();iter != m_setrequests.end();iter++)
//				m_requests.push(**iter);
//
//            return m_requests;
//        }

        void SetProcessor(Processor* proc){m_pProcessor = proc;}
        Processor* GetProcessor(){return m_pProcessor;}

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
        bool OnMemorySnoopRead(uint64_t address, void *data, unsigned int size, bool dcache);
        bool OnMemorySnoopWrite(uint64_t address, void *data, unsigned int size);
        bool OnMemorySnoopIB(uint64_t address, void* data, unsigned int size);
#endif

    private:
        Buffer<Request>               m_requests;
        std::vector<IMemoryCallback*> m_clients;
        std::set<Request*>            m_setrequests;     // MESMX 
        unsigned int                  m_nTotalReq;

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
        std::queue<Request>           m_snoopreads;   // MESMX
#endif

        LinkMGS*                      m_linkmgs;     // MESMX

#ifndef MEM_CACHE_LEVEL_ONE_SNOOP
        IMemoryCallback*              m_pimcallback;
#endif

        Processor*                    m_pProcessor;

        static MemoryDataContainer *s_pMemoryDataContainer;
    };

}
#endif
