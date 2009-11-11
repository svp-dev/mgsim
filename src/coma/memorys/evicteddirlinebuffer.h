#ifndef EVICTED_DIR_LINE_BUFFER_H
#define EVICTED_DIR_LINE_BUFFER_H

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
#include "predef.h"
#include <map>
#include <ostream>
using namespace std;

// designed for local directory
// the matching buffer is for locating the evicted lines with incoming request within the local group
// check predef::dir_line_t::requestin and directortok for detail

namespace MemSim{

class EvictedDirLineBuffer : virtual public SimObj
{
public:
    typedef struct _EDL_content_t{
        unsigned int nrequestin;
        unsigned int ntokenrem;
        bool grouppriority;
    } EDL_Content;

private:
    // date container
    map<__address_t, EDL_Content>   m_mapEDL;

    unsigned int m_nSize;

public:
    EvictedDirLineBuffer(unsigned int size) : m_nSize(size)
    {
        
    }

    virtual ~EvictedDirLineBuffer()
    {
    }


    void AddEvictedLine(__address_t lineaddr, unsigned int requestin, unsigned int tokenrem, bool grouppriority)
    {
        EDL_Content ec = {requestin, tokenrem, grouppriority};
        m_mapEDL.insert(pair<__address_t, EDL_Content>(lineaddr, ec));

//        if (m_mapEDL.size() > m_nSize)
//            assert(false);
    }

    bool FindEvictedLine(__address_t lineaddr, unsigned int& requestin, unsigned int& tokenrem, bool& grouppriority)
    {
        map<__address_t, EDL_Content>::iterator iter;

        iter = m_mapEDL.find(lineaddr);

        if (iter == m_mapEDL.end())
            return false;

        requestin = (*iter).second.nrequestin;
        tokenrem = (*iter).second.ntokenrem;
        grouppriority = (*iter).second.grouppriority;

        return true;
    }

    bool FindEvictedLine(__address_t lineaddr)
    {
        map<__address_t, EDL_Content>::iterator iter;

        iter = m_mapEDL.find(lineaddr);

        if (iter == m_mapEDL.end())
            return false;

        return true;
    }

    // incoming : true  - incoming request
    //            false - outgoing request
    bool UpdateEvictedLine(__address_t lineaddr, bool incoming, unsigned int reqtoken, bool reqpriority, bool eviction=false)
    {
        map<__address_t, EDL_Content>::iterator iter;

        iter = m_mapEDL.find(lineaddr);

        if (iter == m_mapEDL.end())
            return false;

        if (incoming)
        {
            if (!eviction)
                (*iter).second.nrequestin++;
            (*iter).second.ntokenrem += reqtoken;
            (*iter).second.grouppriority |= reqpriority;
        }
        else
        {
            if (!eviction)
                (*iter).second.nrequestin--;
            (*iter).second.ntokenrem -= reqtoken;
            (*iter).second.grouppriority &= (!reqpriority);
        }
    
        CheckEvictedLine(iter);

        return true;
    }

    bool DumpEvictedLine2Line(__address_t lineaddr, dir_line_t* line)
    {
        map<__address_t, EDL_Content>::iterator iter;

        iter = m_mapEDL.find(lineaddr);

        if (iter == m_mapEDL.end())
            return false;

        line->nrequestin += iter->second.nrequestin;
        line->ntokenrem += iter->second.ntokenrem;
        line->grouppriority |= iter->second.grouppriority;

        // remote the buffer slot
        m_mapEDL.erase(iter);

        return true;
    }

    void CheckStatus(ostream& ofs)
    {
        ofs << "Evicted Line Buffer:" << endl;
        map<__address_t, EDL_Content>::iterator iter;

        for (iter=m_mapEDL.begin();iter!=m_mapEDL.end();iter++)
        {
            ofs << hex << "0x" << iter->first << " #RI " << iter->second.nrequestin << " TR " << iter->second.ntokenrem << " GP(" << iter->second.grouppriority << ")" << endl;
        }
    }


    void CheckStatus(ostream& ofs, __address_t addr)
    {
        map<__address_t, EDL_Content>::iterator iter;

        iter = m_mapEDL.find(addr);

        if (iter == m_mapEDL.end())
        {
            ofs << hex << "$X|X|X";
            return;
        }

        ofs << "$" << iter->second.nrequestin << "|" << iter->second.ntokenrem << "|" << (iter->second.grouppriority?"T":"F");
         
    }

private:

    void CheckEvictedLine(map<__address_t, EDL_Content>::iterator iter)
    {
        if (((*iter).second.nrequestin == 0) && ((*iter).second.ntokenrem == 0))
        {
            assert((*iter).second.grouppriority == false);

            // remove 
            m_mapEDL.erase(iter);
        }
    }

    void RemoveEvictedLine()
    {
    
    }
};


}


#endif
#endif
