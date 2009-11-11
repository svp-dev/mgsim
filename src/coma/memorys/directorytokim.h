#ifndef _DIRECTORYTOK_IM_H
#define _DIRECTORYTOK_IM_H

// normal directory

#include "directorytok.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class DirectoryTOKIM : public DirectoryTOK, virtual public SimObj //public sc_module, public CacheState, public NetworkBelow_if, public NetworkAbove_if, public SimObj//, public BusST_Master
{
private:

public:
	// directory should be defined large enough to hold all the information in the hierarchy below
	SC_HAS_PROCESS(DirectoryTOKIM);
	DirectoryTOKIM(sc_module_name nm, unsigned int nset, unsigned int nassoc, unsigned int nlinesize, INJECTION_POLICY nIP, __address_t startaddr=0, __address_t endaddr= MemoryState::MEMORY_SIZE, UINT latency = 5, REPLACE_POLICY policyR = RP_LRU, unsigned char policyW = (WP_WRITE_THROUGH|WP_WRITE_AROUND)) 
        : SimObj(nm), DirectoryTOK(nm, nset, nassoc, nlinesize, nIP, policyR, policyW, startaddr, endaddr, latency)
	{
	}

	// transactions
	virtual void OnABOAcquireToken(ST_request *);
    virtual void OnABOAcquireTokenData(ST_request *);
    virtual void OnABODisseminateTokenData(ST_request *);

    virtual void OnBELAcquireToken(ST_request *);
    virtual void OnBELAcquireTokenData(ST_request *);
    virtual void OnBELDisseminateTokenData(ST_request *);
    virtual void OnBELDirNotification(ST_request *);

    // virtual methods
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    void ReviewState(REVIEW_LEVEL rev);

    void MonitorAddress(ostream& ofs, __address_t addr);
#endif
};


//////////////////////////////
//} memory simulator namespace
}

#endif

