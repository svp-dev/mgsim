#ifndef EVICTED_DIR_LINE_BUFFER_H
#define EVICTED_DIR_LINE_BUFFER_H

#include "predef.h"
#include <map>
#include <ostream>
using namespace std;

// designed for local directory
// the matching buffer is for locating the evicted lines with incoming request within the local group
// check predef::dir_line_t::requestin and directortok for detail

namespace MemSim
{

class EvictedDirLineBuffer
{
    struct EDL_Content
    {
        unsigned int nrequestin;
        unsigned int ntokenrem;
        bool grouppriority;
    };

    std::map<__address_t, EDL_Content> m_mapEDL;

public:
    void AddEvictedLine(__address_t lineaddr, unsigned int requestin, unsigned int tokenrem, bool grouppriority)
    {
        EDL_Content ec = {requestin, tokenrem, grouppriority};
        m_mapEDL.insert(pair<__address_t, EDL_Content>(lineaddr, ec));
    }

    bool FindEvictedLine(__address_t lineaddr, unsigned int& requestin, unsigned int& tokenrem, bool& grouppriority) const
    {
        map<__address_t, EDL_Content>::const_iterator iter = m_mapEDL.find(lineaddr);
        if (iter != m_mapEDL.end())
        {
            requestin     = iter->second.nrequestin;
            tokenrem      = iter->second.ntokenrem;
            grouppriority = iter->second.grouppriority;
            return true;
        }
        return false;
    }

    bool FindEvictedLine(__address_t lineaddr) const
    {
        return m_mapEDL.find(lineaddr) != m_mapEDL.end();
    }

    // incoming : true  - incoming request
    //            false - outgoing request
    bool UpdateEvictedLine(__address_t lineaddr, bool incoming, unsigned int reqtoken, bool reqpriority, bool eviction=false)
    {
        map<__address_t, EDL_Content>::iterator iter = m_mapEDL.find(lineaddr);
        if (iter != m_mapEDL.end())
        {
            if (incoming)
            {
                if (!eviction)
                    iter->second.nrequestin++;
                iter->second.ntokenrem += reqtoken;
                iter->second.grouppriority |= reqpriority;
            }
            else
            {
                if (!eviction)
                    iter->second.nrequestin--;
                iter->second.ntokenrem -= reqtoken;
                iter->second.grouppriority &= (!reqpriority);
            }
    
            if ((iter->second.nrequestin == 0) && (iter->second.ntokenrem == 0))
            {
                assert(iter->second.grouppriority == false);
                m_mapEDL.erase(iter);
            }
            return true;
        }
        return false;
    }

    bool DumpEvictedLine2Line(__address_t lineaddr, dir_line_t* line)
    {
        map<__address_t, EDL_Content>::iterator iter = m_mapEDL.find(lineaddr);
        if (iter != m_mapEDL.end())
        {
            line->nrequestin += iter->second.nrequestin;
            line->ntokenrem += iter->second.ntokenrem;
            line->grouppriority |= iter->second.grouppriority;

            // remote the buffer slot
            m_mapEDL.erase(iter);
            return true;
        }
        return false;
    }
};


}


#endif
