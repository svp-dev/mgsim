#ifndef _CACHEST_H
#define _CACHEST_H

#include "predef.h"
#include "setassociativeprop.h"

#define WAIT_INVALIDATE_INNER_CACHE

#define CACHE_MASKED_ADDR(addr) ((addr >> s_nLineBits) << s_nLineBits)

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class CacheST : public sc_module, public CacheState, public SetAssociativeProp, virtual public SimObj
{
public:

//protected:
//    static unsigned int s_nLineSize;
//    static unsigned int s_nLineBits;

protected:
    //sc_fifo<ST_request*> *m_pfifoReqIn;

    unsigned int m_nSet;            // number of set
    unsigned int m_nAssociativity;  // the associativity
    //unsigned int m_nLineSize;       // the number of bytes in a cache line
    REPLACE_POLICY m_policyReplace;
    unsigned char m_policyWrite;

    __address_t m_nStartAddress;
    __address_t m_nEndAddress;

    unsigned int m_nSetBits;
    //unsigned int m_nLineBits;
    cache_set_t *m_pSet;

#ifdef SIMULATE_DATA_TRANSACTION
    char * m_pBufData;              // data buffer
#endif

    //__address_t m_nAddress;
    //UINT m_nValue;
    UINT m_nLatency;

    UINT  m_nWaitCountINI;  // initiative, request from the processor side
    UINT  m_nWaitCountPAS;  // passive, request from memory side

    vector<ST_request*> m_vRedirectedRequest;

public:

    CacheST(sc_module_name nm, unsigned int nset, unsigned int nassoc, unsigned int nlinesize, REPLACE_POLICY policyR = RP_LRU, unsigned char policyW = (WP_WRITE_THROUGH|WP_WRITE_AROUND), __address_t startaddr=0, __address_t endaddr= MemoryState::MEMORY_SIZE, UINT nLatency = 2) 
        : sc_module(nm), SetAssociativeProp(nlinesize), m_nSet(nset), m_nAssociativity(nassoc), m_policyReplace(policyR), m_policyWrite(policyW), m_nStartAddress(startaddr), m_nEndAddress(endaddr), m_nLatency(nLatency)
    {

        m_nSetBits = lg2(m_nSet);

        // initialize the class in child class

        // initialize parameters
        m_nWaitCountINI = 0;
        m_nWaitCountPAS = 0;

        // inherited from SAP
        m_bDirectory = false;
    }

    virtual void InitializeCacheLines();
    ~CacheST();

    virtual cache_line_t* LocateLine(__address_t) = 0;
    virtual cache_line_t* GetReplacementLine(__address_t) = 0;

    // check data consistency according to the mask or the state
    // if bfull is true, the state + bitmask is going to be used. 
    // otherwise only the line bitmask is going to be used. 
    // for instance, WPM state contains the data, but the bitmask is masked for newly written bits. 
    // thus bfull should be specified in this case. 
    virtual bool CheckDataConsistency(cache_line_t* line, ST_request* req, bool bfull=true);

    // inline cache set index function
    virtual unsigned int CacheIndex(__address_t address)
    {
        uint64 addr = address;
        return (unsigned int)( (addr>>s_nLineBits) & (m_nSet-1) );
    }

    // inline cache line tag function
    virtual uint64 CacheTag(__address_t address)
    {
        uint64 addr = address;
        return (uint64)((addr) >> (s_nLineBits + m_nSetBits));
    }

    virtual __address_t AlignAddress4Cacheline(__address_t address)
    {
        // align address to the starting of the cacheline
        __address_t retaddr = (address>>s_nLineBits)<<s_nLineBits;
        return retaddr;
    }

    //////////////////////////////////////////////////////////////////////////
    // for checking data in the line
    virtual cache_line_t* GetSetLines(unsigned int nset)
    {
        return m_pSet[nset].lines;
    }

};

//////////////////////////////
//} memory simulator namespace
}

#endif
