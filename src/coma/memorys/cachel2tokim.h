#ifndef _CACHEL2_TOK_IM_H
#define _CACHEL2_TOK_IM_H

#include "cachel2tok.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class CacheL2TOKIM : public CacheL2TOK, virtual public SimObj
{
public:

    SC_HAS_PROCESS(CacheL2TOKIM);
    CacheL2TOKIM(sc_module_name nm, unsigned int nset, unsigned int nassoc, unsigned int nlinesize, INJECTION_POLICY nIP=IP_NONE, REPLACE_POLICY policyR = RP_LRU, unsigned char policyW = (WP_WRITE_THROUGH|WP_WRITE_AROUND), __address_t startaddr=0, __address_t endaddr= MemoryState::MEMORY_SIZE, UINT latency = 5
        , BACKWARD_BRAODCAST_INVALIDATION nBBIPolicy = BBI_LAZY
#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
        , UINT nResidueDelay = 2
#endif
        ) 
        : SimObj(nm), CacheL2TOK(nm, nset, nassoc, nlinesize, nIP, policyR, policyW, startaddr, endaddr, latency
        , nBBIPolicy 
#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
        ,nResidueDelay
#endif
        )
    {
    }

    virtual void ReviewState(REVIEW_LEVEL rev);
    virtual void MonitorAddress(ostream& ofs, __address_t addr);

protected:
    // transactions handler

    // initiative request handlers
    void OnLocalRead(ST_request*);
    void OnLocalWrite(ST_request*);
#ifdef WAIT_INVALIDATE_INNER_CACHE
    void OnInvalidateRet(ST_request*);
#endif

    // passive request handlers
    // passive request handlers
    virtual void OnAcquireTokenRem(ST_request*);
    virtual void OnAcquireTokenRet(ST_request*);
    virtual void OnAcquireTokenDataRem(ST_request*);
    virtual void OnAcquireTokenDataRet(ST_request*);
    virtual void OnDisseminateTokenData(ST_request*);
    //virtual void OnDisseminateTokenDataRem(ST_request*);
    //virtual void OnDisseminateTokenDataRet(ST_request*);
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    virtual void OnDirNotification(ST_request*);
#endif

    virtual void OnPostAcquirePriorityToken(cache_line_t*, ST_request*);

    // replacement 
    // return   NULL    : if all the lines are occupied by locked states
    //          !NULL   : if any normal line is found
    //                    in case the line found is not empty,
    //                    then additional request will be sent out
    //                    as EVICT request and WRITEBACK request in caller function.
    //                    fail to send any of those request will leave 
    //                    the cache in DB_RETRY_AS_NODE state in the function.
    //                    success in sending the request will change the 
    //                    state of the cache to RETRY_AS_NODE in the function. 
    //                    the caller will need to prepare the next request to send, 
    //                    but no sending action should be taken.
    //                    In summary the sending action should only be taken in caller func. 
    virtual cache_line_t* GetReplacementLineEx(__address_t);

#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
    void OnLocalReadResidue(ST_request*);
    void OnLocalWriteResidue(ST_request*);
#endif

};

//////////////////////////////
//} memory simulator namespace
}

#endif  // header
