#ifndef _SET_ASSOCIATIVEPROP_H
#define _SET_ASSOCIATIVEPROP_H

#include "predef.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class SetAssociativeProp : virtual public SimObj
{
public:
    static unsigned int s_nLineSize;
    static unsigned int s_nLineBits;

protected:
    bool    m_bDirectory;

public:
    SetAssociativeProp(unsigned int nlinesize)
    {
        if (nlinesize <= 0)
            cerr << ERR_HEAD_OUTPUT << "illegal cacheline size" << endl;

        // update the linesize
        if (s_nLineSize == 0)
        {
            s_nLineSize = nlinesize;
            s_nLineBits = lg2(nlinesize);
            ST_request::s_nRequestAlignedSize = nlinesize;
        }
        else if (s_nLineSize != nlinesize)
        {
            cerr << ERR_HEAD_OUTPUT << "the cacheline size is inconsistent with other caches" << endl;
        }

        m_bAssoModule = true;
    }

    bool IsDirectory(){return m_bDirectory;};

    virtual void MonitorAddress(ostream& ofs, __address_t addr){};
};

//////////////////////////////
//} memory simulator namespace
}

#endif

