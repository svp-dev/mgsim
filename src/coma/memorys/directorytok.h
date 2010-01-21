#ifndef _DIRECTORYTOK_H
#define _DIRECTORYTOK_H

// normal directory

#include "predef.h"
#include "busst_slave_if.h"
#include "busst_master.h"
#include "networkbelow_if.h"
#include "networkabove_if.h"
#include "suspendedrequestqueue.h"
#include "evicteddirlinebuffer.h"

namespace MemSim
{

// directory implementation has many choices, 
// 1. naive solutioin:
//    with this method, no queue structure is used to store lines form the same location. 
//    so that all the request will go up to the second level ring, 
//    however, root directory will queue to reduce the requests sent out from the chip IO pins
//
//    naive method basically unzips the topology from local directories. 
//    or make a shortcut sometime by bypassing the subring from the local directories
//    everything else works exactly as the single-level ring network
//
//    in this solution, the tokencount in local directory will always remain 0
//
//    1.a with request couting policy
//        in this policy, all the remote requests coming in will report it's tokens to the local directory
//        during entry and departure of the local group
//
// 2. use suspended request queues...


class DirectoryTOK : public sc_module, public CacheState, public NetworkBelow_if, public NetworkAbove_if
{
public:
    // queue constant numbers
    static const unsigned int EOQ;
    static const unsigned int REQUESTQUEUESIZE;
    static const unsigned int LINEQUEUESIZE;

private:
    unsigned int m_nLineSize;
	unsigned int m_nSet;
	unsigned int m_nAssociativity;
	unsigned int m_nSetBits;
	dir_set_t   *m_pSet;

    // evicted dirline buffer
    EvictedDirLineBuffer m_evictedlinebuffer;

    // current request
    ST_request* m_pPrefetchDeferredReq;
    ST_request* m_pReqCurABO;
    ST_request* m_pReqCurBEL;

    list<ST_request*>   m_lstReqB2a;
    list<ST_request*>   m_lstReqB2b;
    list<ST_request*>   m_lstReqA2a;
    list<ST_request*>   m_lstReqA2b;

    ST_request* m_pReqCurABO2a;
    ST_request* m_pReqCurABO2b;
    ST_request* m_pReqCurBEL2a;
    ST_request* m_pReqCurBEL2b;

    // state
    enum STATE_ABOVE{
        STATE_ABOVE_PROCESSING,
        STATE_ABOVE_RETRY
    };

    enum STATE_BELOW{
        STATE_BELOW_PROCESSING,
        STATE_BELOW_RETRY
    };
    
    STATE_ABOVE m_nStateABO;
    STATE_BELOW m_nStateBEL;

    // pipeline
    pipeline_t m_pPipelineABO;
    pipeline_t m_pPipelineBEL;

    void InitializeDirLines();

    ST_request* FetchRequestNet(bool below);

	void BehaviorBelowNet();
	void BehaviorAboveNet();

    void ProcessRequestBEL();
    void ProcessRequestABO();

    bool SendRequestBELtoBelow(ST_request*);
    bool SendRequestBELtoAbove(ST_request*);
    void SendRequestFromBEL();

    bool SendRequestABOtoBelow(ST_request*);
    bool SendRequestABOtoAbove(ST_request*);
    void SendRequestFromABO();

    // whether a local request should go upper level 
    bool ShouldLocalReqGoGlobal(ST_request*req, dir_line_t* line);

    // update dirline and request
    void UpdateDirLine(dir_line_t* line, ST_request* req, DIR_LINE_STATE state, unsigned int tokencount, int tokenline, int tokenrem, bool priority, bool grouppriority, bool reserved);

    void UpdateRequest(ST_request* req, dir_line_t* line, unsigned int tokenacquired, bool priority);
    void Update_RequestRipsLineTokens(bool requestbelongstolocal, bool requestfromlocal, bool requesttolocal, ST_request* req, dir_line_t* line, int increqin=0, int increqout=0);
    void PostUpdateDirLine(dir_line_t* line, ST_request* req, bool belongstolocal, bool fromlocal, bool tolocal);

    //////////////////////////////////////////////////////////////////////////
	// transaction handler
    void OnABOAcquireToken(ST_request *);
    void OnABOAcquireTokenData(ST_request *);
    void OnABODisseminateTokenData(ST_request *);

    void OnBELAcquireToken(ST_request *);
    void OnBELAcquireTokenData(ST_request *);
    void OnBELDisseminateTokenData(ST_request *);
    void OnBELDirNotification(ST_request *);

    // judge whether the request is from local ring or upper level
    bool IsRequestLocal(ST_request* req, bool recvfrombelow);
    
    //////////////////////////////////////////////////////////////////////////
    // cache interfaces
    dir_line_t* LocateLine(__address_t);
    dir_line_t* GetReplacementLine(__address_t);
    unsigned int DirIndex(__address_t);
    uint64 DirTag(__address_t);

public:
	// directory should be defined large enough to hold all the information in the hierarchy below
	SC_HAS_PROCESS(DirectoryTOK);
	
	DirectoryTOK(sc_module_name nm, sc_clock& clock, unsigned int nset, unsigned int nassoc, unsigned int nlinesize, UINT latency = 5) 
      : sc_module(nm),
	    m_nLineSize(nlinesize),
	    m_nSet(nset),
	    m_nAssociativity(nassoc),
        m_nStateABO(STATE_ABOVE_PROCESSING),
        m_nStateBEL(STATE_BELOW_PROCESSING),
        m_pPipelineABO(latency),
        m_pPipelineBEL(latency)
	{
	    ST_request::s_nRequestAlignedSize = nlinesize;
	    
        // handle below interface
		SC_METHOD(BehaviorBelowNet);
		sensitive << clock.posedge_event();
		dont_initialize();

        // handle above interface
		SC_METHOD(BehaviorAboveNet);
		sensitive << clock.posedge_event();
		dont_initialize();

		// allocate space for all cache lines
		InitializeDirLines();
	}

    ~DirectoryTOK(){
        for (unsigned int i=0;i<m_nSet;i++)
            free(m_pSet[i].lines);
        free(m_pSet);
    }

	NetworkBelow_if& GetBelowIF() { return *this; };
	NetworkAbove_if& GetAboveIF() { return *this; };
};

}
#endif
