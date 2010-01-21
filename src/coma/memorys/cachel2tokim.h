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
    
    CacheL2TOKIM(sc_module_name nm, sc_clock& clock, unsigned int nset, unsigned int nassoc, unsigned int nlinesize, INJECTION_POLICY nIP=IP_NONE, UINT latency = 5)
        : CacheL2TOK(nm, clock, nset, nassoc, nlinesize, nIP, latency)
    {
    }

protected:
    // transactions handler

    // initiative request handlers
    void OnLocalRead(ST_request*);
    void OnLocalWrite(ST_request*);
    void OnInvalidateRet(ST_request*);

    // passive request handlers
    // passive request handlers
    void OnAcquireTokenRem(ST_request*);
    void OnAcquireTokenRet(ST_request*);
    void OnAcquireTokenDataRem(ST_request*);
    void OnAcquireTokenDataRet(ST_request*);
    void OnDisseminateTokenData(ST_request*);
    void OnDirNotification(ST_request*);

    void OnPostAcquirePriorityToken(cache_line_t*, ST_request*);

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
    cache_line_t* GetReplacementLineEx(__address_t);
};

}

#endif  // header
