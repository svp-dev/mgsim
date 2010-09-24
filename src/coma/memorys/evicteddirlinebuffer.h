#ifndef ZLCOMA_EVICTEDDIRLINEBUFFER_H
#define ZLCOMA_EVICTEDDIRLINEBUFFER_H

#include "COMA.h"
#include <map>
#include <utility>

namespace Simulator
{

class EvictedDirLineBuffer
{
    struct EDL_Content
    {
        unsigned int nrequestin;
        unsigned int ntokenrem;
    };

    std::map<MemAddr, EDL_Content> m_mapEDL;

public:
    void AddEvictedLine(MemAddr lineaddr, unsigned int requestin, unsigned int tokenrem)
    {
        EDL_Content ec = {requestin, tokenrem};
        m_mapEDL.insert(std::pair<MemAddr, EDL_Content>(lineaddr, ec));
    }

    bool FindEvictedLine(MemAddr lineaddr, unsigned int& requestin, unsigned int& tokenrem) const
    {
        std::map<MemAddr, EDL_Content>::const_iterator iter = m_mapEDL.find(lineaddr);
        if (iter != m_mapEDL.end())
        {
            requestin = iter->second.nrequestin;
            tokenrem  = iter->second.ntokenrem;
            return true;
        }
        return false;
    }

    bool FindEvictedLine(MemAddr lineaddr) const
    {
        return m_mapEDL.find(lineaddr) != m_mapEDL.end();
    }

    // incoming : true  - incoming request
    //            false - outgoing request
    bool UpdateEvictedLine(MemAddr lineaddr, bool incoming, unsigned int reqtoken, bool eviction=false)
    {
        std::map<MemAddr, EDL_Content>::iterator iter = m_mapEDL.find(lineaddr);
        if (iter != m_mapEDL.end())
        {
            if (incoming)
            {
                if (!eviction)
                    iter->second.nrequestin++;
                iter->second.ntokenrem += reqtoken;
            }
            else
            {
                if (!eviction)
                    iter->second.nrequestin--;
                iter->second.ntokenrem -= reqtoken;
            }
    
            if (iter->second.nrequestin == 0 && iter->second.ntokenrem == 0)
            {
                m_mapEDL.erase(iter);
            }
            return true;
        }
        return false;
    }

    bool DumpEvictedLine2Line(MemAddr lineaddr, unsigned int& requestin, unsigned int& tokenrem)
    {
        std::map<MemAddr, EDL_Content>::iterator iter = m_mapEDL.find(lineaddr);
        if (iter != m_mapEDL.end())
        {
            requestin = iter->second.nrequestin;
            tokenrem  = iter->second.ntokenrem;

            m_mapEDL.erase(iter);
            return true;
        }
        return false;
    }
};

}

#endif
