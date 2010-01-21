#ifndef _CACHEST_H
#define _CACHEST_H

#include "predef.h"

namespace MemSim
{

class CacheST : public sc_module, public CacheState, virtual public SimObj
{
protected:
    unsigned int   m_nLineSize;
    unsigned int   m_nSet;            // number of set
    unsigned int   m_nAssociativity;  // the associativity
    cache_set_t   *m_pSet;
    char *         m_pBufData;              // data buffer

public:
    CacheST(sc_module_name nm, unsigned int nset, unsigned int nassoc, unsigned int nlinesize);   
    ~CacheST();

    // inline cache set index function
    virtual unsigned int CacheIndex(__address_t address)
    {
        return (address / m_nLineSize) % m_nSet;
    }

    // inline cache line tag function
    virtual uint64 CacheTag(__address_t address)
    {
        return (address / m_nLineSize) / m_nSet;
    }

    virtual __address_t AlignAddress4Cacheline(__address_t address)
    {
        // align address to the starting of the cacheline
        return (address / m_nLineSize) * m_nLineSize;
    }
};

}
#endif
