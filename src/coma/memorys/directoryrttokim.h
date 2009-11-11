#ifndef _DIRECTORYRT_TOK_IM_H
#define _DIRECTORYRT_TOK_IM_H

// root directory

#include "directoryrttok.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class DirectoryRTTOKIM : public DirectoryRTTOK, virtual public SimObj //public sc_module, public CacheState/*,public BusST_Slave_if*/, public NetworkBelow_if, public BusST_Master
{
private:

public:
    // directory should be defined large enough to hold all the information in the hierarchy below
    SC_HAS_PROCESS(DirectoryRTTOKIM);
    DirectoryRTTOKIM(sc_module_name nm, unsigned int nset, unsigned int nassoc, unsigned int nlinesize, INJECTION_POLICY nIP=IP_NONE, unsigned int nSplitMode = 0x100,  __address_t startaddr=0, __address_t endaddr= MemoryState::MEMORY_SIZE, UINT latency = 5, REPLACE_POLICY policyR = RP_LRU, unsigned char policyW = (WP_WRITE_THROUGH|WP_WRITE_AROUND)) 
        : SimObj(nm), DirectoryRTTOK(nm, nset, nassoc, nlinesize, nIP, nSplitMode, policyR, policyW, startaddr, endaddr, latency)
    {
    }

    // transactions
    virtual void OnNETAcquireToken(ST_request *);
    virtual void OnNETAcquireTokenData(ST_request *);
    virtual void OnNETDisseminateTokenData(ST_request *);

//    virtual void OnNETReadReply(ST_request*);
//    virtual void OnNETInvalidate(ST_request*);
//    virtual void OnNETLineReplacement(ST_request*);
//    //virtual void OnNETRemoteRead(ST_request*);	// share data
//    virtual void OnNETDataExclusive(ST_request*);
//    virtual void OnNETRemoteReadShared(ST_request*);	    
//    virtual void OnNETRemoteReadExclusive(ST_request*);	
//    virtual void OnNETRemoteSharedReadReply(ST_request*);
//    virtual void OnNETRemoteExclusiveReadReply(ST_request*);
//    virtual void OnNetEvict(ST_request*);         // eviction request will terminate at directory
//    virtual void OnNetWriteBack(ST_request*);     // write-back request will be delivered to the main memory
//
    // deal with A-A indicator in the request and Adjust the counter value and directory state
    void AAIndicatorHandler(ST_request* req, dir_line_t* line);

    //virtual void OnBUSReadReply(ST_request*);
    virtual void OnBUSSharedReadReply(ST_request*);
    virtual void OnBUSExclusiveReadReply(ST_request*);

    void ReviewState(REVIEW_LEVEL rev);

    void MonitorAddress(ostream& ofs, __address_t addr);

};

//////////////////////////////
//} memory simulator namespace
}

#endif  // header
