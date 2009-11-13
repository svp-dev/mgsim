#ifndef _DIRECTORYTOK_H
#define _DIRECTORYTOK_H

// normal directory

#include "predef.h"
#include "setassociativeprop.h"
#include "busst_slave_if.h"
#include "busst_master.h"
#include "ringnode.h"

#include "networkbelow_if.h"
#include "networkabove_if.h"

#include "suspendedrequestqueue.h"

#include "evicteddirlinebuffer.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


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


class DirectoryTOK : public sc_module, public CacheState, public NetworkBelow_if, public NetworkAbove_if, public RingNode, public SetAssociativeProp
{
public:
	sc_in<bool> port_clk;

    // queue constant numbers
    static const unsigned int EOQ;
    static const unsigned int REQUESTQUEUESIZE;
    static const unsigned int LINEQUEUESIZE;

protected:
    //////////////////////////////////////////////////////////////////////////
    // cache parameters
	//sc_fifo<ST_request*> *m_pfifoReqIn;

	unsigned int m_nSet;
	unsigned int m_nAssociativity;
	//unsigned int m_nLineSize;
	REPLACE_POLICY m_policyReplace;
	unsigned char m_policyWrite;

	__address_t m_nStartAddress;
	__address_t m_nEndAddress;

	unsigned int m_nSetBits;
	//unsigned int m_nLineBits;
	dir_set_t *m_pSet;

	__address_t m_nAddress;
	__address_t m_nValue;
	UINT m_nLatency;

	vector<ST_request*> m_vRedirectedRequest;

    //////////////////////////////////////////////////////////////////////////
    // queue structure
    //SuspendedRequestQueue m_srqSusReqQ;


#ifdef UNIVERSAL_ACTIVE_QUEUE_MODE
    unsigned int m_nActiveQueueHead;   // 0xffffffff
    unsigned int m_nActiveQueueTail;
#endif

#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
    unsigned int m_nRepeatLatency;      // the latency for repeating the same address
    // still use m_nWaitCountPAS for counting
    ST_request* m_pReqResidue;             // the residue request picked up
    CACHE_LINE_STATE m_nResidueState;   // save the state a line supposed to be
    // so that the next request to be processed
    // will continue from current state directly
#endif

    //////////////////////////////////////////////////////////////////////////
    // statistics on different components
//    ST_request* m_pStatCurReqBEL;                   // backup the current request BEL for statistics
//    ST_request* m_pStatCurReqABO;                   // backup the current request ABO for statistics
//
//    unsigned int m_nStatReqNo;                      // total request number
//    static const int s_nStatSetTraceNo = 0x40;
//
//    map<double, stat_stru_size_t>              *m_pmmStatRequestNo;
//    map<double, stat_stru_request_t>           *m_pmmStatProcessingBEL;
//    map<double, stat_stru_set_t>               *m_pmmStatSetTrace;


    //////////////////////////////////////////////////////////////////////////
    // directory parameters

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    // evicted dirline buffer
    EvictedDirLineBuffer m_evictedlinebuffer;
#endif

    // delay count
    UINT m_nWaitCountABO;
    UINT m_nWaitCountBEL;

    // current request
    ST_request* m_pPrefetchDeferredReq;
    ST_request* m_pReqCurABO;
    ST_request* m_pReqCurBEL;


    ST_request* m_pReqCurABO2Above;
    ST_request* m_pReqCurABO2Below;
    ST_request* m_pReqCurBEL2Above;
    ST_request* m_pReqCurBEL2Below;

    // state
    enum STATE_ABOVE{
        STATE_ABOVE_PROCESSING,
        STATE_ABOVE_RETRY
//        STATE_ABOVE_IDLE,
//        STATE_ABOVE_WAIT,
//        STATE_ABOVE_RETRY_ABOVE,
//        STATE_ABOVE_RETRY_BELOW
    };

    enum STATE_BELOW{
//        STATE_BELOW_IDLE,
//        STATE_BELOW_WAIT,
//        STATE_BELOW_RETRY_ABOVE,
//        STATE_BELOW_RETRY_BELOW
        STATE_BELOW_PROCESSING,
        STATE_BELOW_RETRY
    };

    STATE_ABOVE m_nStateABO;
    STATE_BELOW m_nStateBEL;

    // pipeline
    pipeline_t *m_pPipelineABO;
    pipeline_t *m_pPipelineBEL;

    INJECTION_POLICY m_nInjectionPolicy;

public:
	// directory should be defined large enough to hold all the information in the hierarchy below
	SC_HAS_PROCESS(DirectoryTOK);
	DirectoryTOK(sc_module_name nm, unsigned int nset, unsigned int nassoc, unsigned int nlinesize, INJECTION_POLICY nIP=IP_NONE, REPLACE_POLICY policyR = RP_LRU, unsigned char policyW = (WP_WRITE_THROUGH|WP_WRITE_AROUND), __address_t startaddr=0, __address_t endaddr= MemoryState::MEMORY_SIZE, UINT latency = 5) 
		: sc_module(nm), RingNode(), SetAssociativeProp(nlinesize), m_nSet(nset), m_nAssociativity(nassoc), m_policyReplace(policyR), m_policyWrite(policyW), m_nStartAddress(startaddr), m_nEndAddress(endaddr), m_nLatency(latency), m_evictedlinebuffer(0x20), m_nInjectionPolicy(nIP)//, m_srqSusReqQ(0x200, 0x100)
	{
        // handle below interface
		SC_METHOD(BehaviorBelowNet);
		sensitive << port_clk.pos();
		dont_initialize();

        // handle above interface
		SC_METHOD(BehaviorAboveNet);
		sensitive << port_clk.pos();
		dont_initialize();

		//m_pfifoReqIn = new sc_fifo<ST_request*>();

		m_nSetBits = lg2(m_nSet);
		//m_nLineBits = lg2(m_nLineSize);

		// allocate space for all cache lines
		InitializeDirLines();

        // initialize parameters
        m_nWaitCountABO = 0;
        m_nWaitCountBEL = 0;
        m_bDirectory = true;

        // initialize parameters
        m_nStateABO = STATE_ABOVE_PROCESSING;
        m_nStateBEL = STATE_BELOW_PROCESSING;

        //pipeline
        m_pPipelineABO = new pipeline_t(latency);
        m_pPipelineBEL = new pipeline_t(latency);

        assert(latency-1>0);
	}

    ~DirectoryTOK(){
        for (unsigned int i=0;i<m_nSet;i++)
            free(m_pSet[i].lines);
        free(m_pSet);
        delete m_pPipelineABO;
        delete m_pPipelineBEL;
    }

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


#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    // update dirline and request
     void UpdateDirLine(dir_line_t* line, ST_request* req, DIR_LINE_STATE state, unsigned int tokencount, int tokenline, int tokenrem, bool priority, bool grouppriority, bool reserved);

     void UpdateRequest(ST_request* req, dir_line_t* line, unsigned int tokenacquired, bool priority);
     void Update_RequestRipsLineTokens(bool requestbelongstolocal, bool requestfromlocal, bool requesttolocal, ST_request* req, dir_line_t* line, int increqin=0, int increqout=0);
    void PostUpdateDirLine(dir_line_t* line, ST_request* req, bool belongstolocal, bool fromlocal, bool tolocal);
#endif

    //////////////////////////////////////////////////////////////////////////
	// transaction handler
     virtual void OnABOAcquireToken(ST_request *) = 0;
    virtual   void OnABOAcquireTokenData(ST_request *) = 0;
 virtual      void OnABODisseminateTokenData(ST_request *) = 0;

 virtual      void OnBELAcquireToken(ST_request *) = 0;
 virtual      void OnBELAcquireTokenData(ST_request *) = 0;
 virtual      void OnBELDisseminateTokenData(ST_request *) = 0;
 virtual      void OnBELDirNotification(ST_request *) = 0;

    // judge whether the request is from local ring or upper level
     bool IsRequestLocal(ST_request* req, bool recvfrombelow);

    //////////////////////////////////////////////////////////////////////////
    // cache interfaces
    dir_line_t* LocateLine(__address_t);
    dir_line_t* LocateLineEx(__address_t);
    dir_line_t* GetReplacementLine(__address_t);
//    dir_line_t* GetEmptyLine(__address_t);
    unsigned int DirIndex(__address_t);
    uint64 DirTag(__address_t);

	//bool request(ST_request *req);
	 __address_t StartAddress() const {return m_nStartAddress;};
	 __address_t EndAddress() const {return m_nEndAddress;};

    //////////////////////////////////////////////////////////////////////////
    // network interfaces, Network_if
    // get network above and below interface
	NetworkBelow_if& GetBelowIF(){return *((NetworkBelow_if*)this);};
	NetworkAbove_if& GetAboveIF(){return *((NetworkAbove_if*)this);};
//     bool DirectForward(ST_request* req){abort();return true;};

    // handle both below and above network interface
     bool MayHandle(ST_request* req);		// handle both below and above
     bool CanHandleNetRequest(ST_request*);			// handle both below and above // alert alert
};


//////////////////////////////
//} memory simulator namespace
}

#endif
