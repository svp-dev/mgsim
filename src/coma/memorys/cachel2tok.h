#ifndef _CACHEL2_TOK_H
#define _CACHEL2_TOK_H

#include "cachest.h"
#include "busst_slave_if.h"
#include "ringnode.h"

#include "networkbelow_if.h"

#include "processortok.h"

#include "fabuffer.h"

#include "mergestorebuffer.h"
#include <queue>
using namespace std;

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


// $$$ optimization - sort global fifo on reverse push :
// when a reverse push is happening on the global fifo, 
// the requests to the same line might be on pipeline or the global fifo
// to sort them together and pack them to the bottom of the fifo, 
// will help other request reach the pipeline faster. 
// + : this might be useful when many requests are in the fifo or pipeline, 
// - : sort might not be easy to perform
// - : the ready data might be evicted before the bottom requests can reach them
//#define OPTIMIZATION_SORT_GLOBAL_FIFO_ON_RPUSH

// $$$ optimization - read reply flush :
// the optimization is focusing on bypassing the some of the pipeline stages in the pipeline, 
// when a number of requests (probably from different processors) are reading the same line
// when a data for the first request is read, the following ones will be updated immediately 
// to READ_REPLY type. In a result when the request is being processed, they can be returned instantly.
// even if any request in the middle evict the returned line, those bypassed line will not be affected.
// + : comply with location consistency
// + : help reduce the cache pressure on continuous requests on the same line
//#define OPTIMIZATION_READ_REPLY_FLUSHING

// $$$ optimization - victim buffer
// the optimization provides a victim (evicted line) matching buffer
// it help resolve the constant eviction problem 
// especially when the eviction is due to the insufficient associativity.
// the optimization provide temporary associativity which can help reduce the conflict within a set
// with continuous certain stride access, the victim buffer is especially effective
// + : provide extra associativity temporarily
// - : additional cost on area and control 
#define OPTIMIZATION_VICTIM_BUFFER

// $$$ optimization - non-existing buffer
// the optimization provides a simple non-existing fully associative matching buffer 
// the buffer is used to fast identify a miss that can happen in the cache. 
// by identifying the miss, the network request can be bypassed directly to the next node 
// without going through the network pipeline. 
//#define  OPTIMIZATION_NON_EXIST_LINE_BYPASS_BUFFER


#define INSERT_SUSPEND_ENTRY(entry) if (line->queuehead == EOQ)\
                                    {\
                                        line->queuehead = line->queuetail = entry;\
                                    }\
                                    else\
                                    {\
                                        line->queuetail = entry;\
                                    }

//#define DIRECT_FORWARD      // direct forward for INVALIDATE and READ_REPLY request

//#define UNIVERSAL_ACTIVE_QUEUE_MODE   // a universal active queue for the whole cache
                                        // might make the processing slower, but give better fairness for the processing

//#define IMMEDIATE_ACTIVATE_QUEUE_MODE   // immediately active the suspended request in the queue asssociated with the cacheline
                                        // when a reply comes back 
                                        // the suspended request on the queue get reactivated immediately in the passive side of the cache
                                        // in this mode, only the TRANSIENT STATES will have suspended request.


//#define CACHE_SRQ		// special SUSPENDED REQUEST QUEUE STRUCTURE

class CacheL2TOK : public CacheST, public BusST_Slave_if, public NetworkBelow_if, public RingNode
{
public:
	sc_in<bool> port_clk;

    static vector<CacheL2TOK*>   s_vecPtrCaches;

protected:
    // constant numbers
    static const unsigned int EOQ;
    static const unsigned int QueueBufferSize;

    // queue structure
    typedef struct _queue_entry
    {
        ST_request* request;
        unsigned int next;              // index of the next entry in a queue
    } queue_entry;

    queue_entry* m_pQueueBuffer;
    unsigned int m_nEmptyQueueHead;     // 0xffffffff
    //ST_request* m_pQueueRegister;       // this will save the top request in global request queue
    //                                    // actually the top will be popped and saved into this register 
    //                                    // the register will only be cleaned 
    //                                    // when the request inside is successfully executed
    //                                    // * the most recent request will always be saved 
    //                                    // * in the register or the top of the FIFO
    //                                    // * request fetch process will always try to 
    //                                    // * fetch request from input FIFO or this register
    //                                    // * when this register is empty, the register will be loaded
    //                                    // * with the top request in global queue if any. 

    //bool m_bProcessingQueueRequest;

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

    // current request
    ST_request* m_pReqCurINI;
    ST_request* m_pReqCurPAS;

    ST_request* m_pReqCurINIasNodeDB;   // the one needs double retry
    ST_request* m_pReqCurINIasNode;
    ST_request* m_pReqCurINIasSlaveX;
    queue<ST_request*> m_queReqINIasSlave;

    ST_request* m_pReqCurPASasNodeX;
    queue<ST_request*> m_queReqPASasNode;
    //ST_request* m_pReqCurPASasSlaveCombined;
    ST_request* m_pReqCurPASasSlaveX;
    queue<ST_request*> m_queReqPASasSlave;


    // states
    enum STATE_INI{
        //STATE_INI_IDLE,
        //STATE_INI_WAIT,
        STATE_INI_PROCESSING,
        STATE_INI_RETRY,
        STATE_INI_BUFFER_PRI            // if the global buffer has a priority, no more request will processed 
                                        // until the buffer reached a certain threshold. 
                                        // JNEWXXX *** !!!
    };

    enum STATE_PAS{
        //STATE_PAS_IDLE,
        //STATE_PAS_WAIT,
        STATE_PAS_PROCESSING,
        STATE_PAS_POSTPONE,
        STATE_PAS_RETRY,
#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
        STATE_PAS_RESIDUE       // JNEWXXX
#endif
    };

//    enum STATE_INI{
//        //STATE_INI_IDLE,
//        //STATE_INI_WAIT,
//        STATE_INI_PROCESSING,
//        STATE_INI_DB_RETRY_AS_NODE,     // DOUBLE retry as node, for instance needs to send EVICT and WRITEBACK
//        STATE_INI_RETRY_AS_NODE,
//        STATE_INI_RETRY_AS_SLAVE,
//        STATE_INI_RETRY_AS_BOTH,
//        STATE_INI_BUFFER_PRI            // if the global buffer has a priority, no more request will processed 
//                                        // until the buffer reached a certain threshold. 
//    };

//    enum STATE_PAS{
//        //STATE_PAS_IDLE,
//        //STATE_PAS_WAIT,
//        STATE_PAS_PROCESSING,
//        STATE_PAS_POSTPONE,
//        STATE_PAS_RETRY_AS_NODE,
//        STATE_PAS_RETRY_AS_SLAVE,
//        STATE_PAS_RETRY_AS_BOTH,     // INVALIDATION TO BROADCAST
//#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
//        STATE_PAS_RESIDUE
//#endif
//    };

    STATE_INI m_nStateINI;
    STATE_PAS m_nStatePAS;

    // pipeline
    pipeline_t  *m_pPipelineINI;
    pipeline_t  *m_pPipelinePAS;

    //////////////////////////////////////////////////////////////////////////
    // the requests stored in the global FIFO should be unrelated to each other
    // thus the order how those requests are organized are not critical
    // request can be popped out and pushed back to tail again without affecting the program result
    // the global queue has two threshold, UPPER threshold and LOWER threshold
    // the two threshold are calculated with buffersize and UPPER and LOWER margines
    // margin should be set larger than 1 for each case.
    // when the UPPER threshold is met in the cache buffer,
    // the buffer priority flag will be set and will not process any incoming requests
    // and will lead to the stall of the cache quite soon. the cache will then only deal with buffered requests.
    // when the number of the request in the buffer reached the LOWER threshold
    // the flag of buffer priority will be reset.
    // in simple case, the UPPER and LOWER threshold can be set to a same number
    // normally, upper threshold should be larger than lower threshold (upper margin should be smaller than lower margin)
    //////////////////////////////////////////////////////////////////////////
    bool    m_bBufferPriority;
	UINT32	  m_nGlobalFIFOSize;
	//sc_fifo<ST_request*> *m_pGlobalFIFO;
    list<ST_request*>   *m_pGlobalFIFOLIST;

    static unsigned int s_nGlobalFIFOUpperMargin;     // UPPER Margin (corresponding to size since it will compared with free buffer size)
    static const unsigned int s_nGlobalFIFOLowerMargin;     // LOWER Margin (corresponding to size since it will compared with free buffer size)

    // fully associative Invalidation matching buffer for BR requests
    // BR request are sent to L1 cache or processor for Backward Broadcast Invalidation. 
    // With BBI_LAZY method, sometimes a number of backward invalidations might happen to the same line continuously
    // this might slow down the L1/Processor or saturate the shared bus connecting L2 cache
    // With the matching buffer, the some latest BR requests can be buffered and cleared according to the situation:
    // a BR is added to the buffer when a BR is sent to L1/Processor
    // a BR is removed from the buffer when a RR is delivered to the L1/Processor

    // Fully Associative Invalidation Matching Buffer for BR requests
    static const unsigned int s_nInvalidationMatchingBufferSize = 0x20;

    // the data is not necessary in this invalidation buffer, use char or anytype to save spaces
    FABuffer<__address_t, char> m_fabInvalidation;


    // fully associative Victim line matching buffer for LR requests
    // currently this is to investigate the effectiveness of a victim cache or eviction buffer
    static const unsigned int s_nVictimLineMatchingBufferSize = 0x40;

    // evicted lines matching buffer
    FABuffer<__address_t, char> m_fabEvictedLine;

    // non-existing line matching buffer    // ??? maybe miss is a better word?
    static const unsigned int s_nNonExistLineMatchingBufferSize = 0x10;
    FABuffer<__address_t, char> m_fabNonExistLine;

	//// semaphore for FIFO buffer retry
	//// semaphore can make increments when a reply has been received
	//// when semaphore is larger than 0, the buffer always has higher priority than the input. 
	//// thus the global FIFO will be processed. 
	//// otherwise, the FIFO is only processed when the processor is idle (the input buffer from processor is empty)
	//UINT32    m_nSmprFIFORetry;

    // Merge Store Buffer Implementation
    // 0. pending line can lock the line itself itself by further access (any further accesses or specific access -- decided by IsLineLocked TBD)
    // 1. read on write pending lines (the corresponding merged line should not be locked) with tokens, even locked tokens, can be performed on the line directly and return.
    // 2. read on read pending line (the corresponding merged line should not be locked )with tokens, even ghost tokens, can be perfomed on the line directly and return.
    // 3. write on write pending lines with priority token can be performed directly according to the following rules
    //    a. it can be performed on the merged store line but no return, if the AT request was from a different family.
    //    b. if the AT request was from the same family, the request can be performed directly on the line and return immediately.  [N/A yet]
    //    *. if no knowledge about the family, then the request should be performed on the MSB without return.
    // 4. write on read pending line with priority token can be performed on the line directly and no immediate return,
    //    but the merged AT/AD needs to be send out and also change the state to writepending immediately 
    //    (additional line state needs to remember the outgoing AT request) [N/A yet]
    // 5. write on writepending lines without priority tokens may write to merge store buffer if availablei and no immediate return.
    //    a. write to unlocked merged lines can proceed directly with no immediate return.
    //    b. write to locked merged lines will be blocked (or proceeded according to previous request initiators on the merge buffer TBD)
    // 6. read on the writepending lines without priority tokens are more delicate, since read always needs to read the whole line
    //    a. if a merged buffer is not locked, it can proceeed, otherwise it will be blocked to suspend in the normal queue
    //    b. read might need to combine both line and merged store buffer, as long as the data are available, it can read and return;
    //       merged store buffer has the priority, if both line and merged buffer has the data, line data is ignored
    //    c. when a read fails, the buffer slot/line will be set as locked (or a bitmapping for the read initiator is updated to improve the performance TBD) 
    //       and preventing others (illegal ones) from access the data
    // 7. write on read pending line without priority token will have to write to merge buffer (or maybe just suspend, since it might waste too much MSB TBD) [N/A yet]
    //    a. write can proceed with no immediate reply, 
    //    b. update on the merged request is of course automatic
    // 8. whenever the writepending line gets the priority token, the line will directly merge with the corresponding merge store buffer line
    //    the merged request for the merge store line will be eliminated, 
    //    and all the following write requests suspended on the merged line will be directly returend without processing.
    //    this can happen also after the AD/AT request returns
    // 9. whenever read pending line get priority token, (T cannot possibly get priority token, only R can), merge write buffer will be directly merged with the line 
    //    and change the state to write pending state, merged request has to be sent out to invalidate others.    [N/A yet]
    //
    // *  any request suspended on the merge store line will be queued and a merged request will gather all the data in those request
    // *  any unavailablility of the merge store buffer will cause the incoming request to suspend on normal queues
    // *  merge buffer slot/line are locked or not is decided by the IsLineLocked() function
    // *  all request without immediate reply to the processor, will have to be queued in the merged line queue. 
    //    the merged line queue will be served before any other requests suspended on the line or even in the pipeline
    // *  change in handling AD/RS/SR return on write pending state

    // Merge Store buffer module provide the merge capability on stores on the pending lines
    MergeStoreBuffer m_msbModule;



    // processor topology 
    int m_nPidMin;
    int m_nPidMax;

	// Outstanding request structure
	// later will be replaced by a pending request cache
	// currently a vector is used instead
	// the map is sorted with aligned addresses in the cachelines
	map<__address_t, ST_request*>	m_mapPendingRequests;

    // the backward invalidation policy
    BACKWARD_BRAODCAST_INVALIDATION m_nBBIPolicy;

    // Injection policy
    INJECTION_POLICY m_nInjectionPolicy;

#ifdef MEM_MODULE_STATISTICS
    //////////////////////////////////////////////////////////////////////////
    // statistics on different components
    ST_request* m_pStatCurReqINI;                   // backup the current request INI for statistics
    ST_request* m_pStatCurReqNET;                   // backup the current request PAS for statistics
    ST_request* m_pStatCurSendNodeINI;
    ST_request* m_pStatCurSendNodeNET;
    bool        m_bStatCleansingINI;                // flag to indicating cleansing
    ST_request* m_pStatTestINI;
    ST_request* m_pStatTestNET;
    unsigned int m_nStatReqNo;                      // total request number

    map<double, stat_stru_size_t>              *m_pmmStatRequestNo;
    map<double, stat_stru_request_t>           *m_pmmStatProcessingINI;
    map<double, stat_stru_request_t>           *m_pmmStatProcessingNET;
    map<double, stat_stru_request_list_t>      *m_pmmStatPipelineINI;
    map<double, stat_stru_request_list_t>      *m_pmmStatPipelineNET;
    map<double, stat_stru_request_t>           *m_pmmStatCleansingINI;
    map<double, stat_stru_request_t>           *m_pmmStatSendNodeINI;
    map<double, stat_stru_request_t>           *m_pmmStatSendNodeNET;
    map<double, stat_stru_size_t>              *m_pmmStatIncomingFIFOINI;
    map<double, stat_stru_size_t>              *m_pmmStatIncomingFIFONET;
    //map<double, stat_stru_request_list_t>    *m_pmmStatTest;
    //map<double, stat_stru_request_t>         *m_pmmStatTest;
    map<double, stat_stru_size_t>              *m_pmmStatTest;
#endif

    // $ optimization $ non-exist line bypass
#ifdef  OPTIMIZATION_NON_EXIST_LINE_BYPASS_BUFFER
    ST_request* m_pReqNonExistBypass;
#endif

public:

	SC_HAS_PROCESS(CacheL2TOK);
    CacheL2TOK(sc_module_name nm, unsigned int nset, unsigned int nassoc, unsigned int nlinesize, INJECTION_POLICY nIP=IP_NONE, REPLACE_POLICY policyR = RP_LRU, unsigned char policyW = (WP_WRITE_THROUGH|WP_WRITE_AROUND), __address_t startaddr=0, __address_t endaddr= MemoryState::MEMORY_SIZE, UINT latency = 5, 
#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
        UINT nResidueDelay = 2,
#endif
        BACKWARD_BRAODCAST_INVALIDATION nBBIPolicy = BBI_LAZY, 
		UINT32 nGlobalFIFOSize = 0x100, 
        bool bBlock = true
        ) 
        : CacheST(nm, nset, nassoc, nlinesize, policyR, policyW, startaddr, endaddr, latency), RingNode()
#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
        , m_nRepeatLatency(nResidueDelay)
#endif
        , m_fabInvalidation(s_nInvalidationMatchingBufferSize)
        , m_fabEvictedLine(s_nVictimLineMatchingBufferSize)
        , m_fabNonExistLine(s_nNonExistLineMatchingBufferSize)
        , m_msbModule(3), m_nBBIPolicy(nBBIPolicy)
        , m_nInjectionPolicy(nIP)
	{
        // process for requests got from the initiators
        SC_METHOD(BehaviorIni);
        sensitive << port_clk.pos();
        dont_initialize();

        SC_METHOD(BehaviorNet);
        sensitive << port_clk.pos();
        dont_initialize();

        // pipeline
        m_pPipelineINI = new pipeline_t(latency-1);
        m_pPipelinePAS = new pipeline_t(latency);

        assert(latency-1>0);

        // JXXX rewrite upper margin
        s_nGlobalFIFOUpperMargin = latency+2;

		// save the size of Global Queue 
		m_nGlobalFIFOSize = nGlobalFIFOSize;
        // cout << nGlobalFIFOSize << " " << s_nGlobalFIFOLowerMargin << endl;
        assert(nGlobalFIFOSize>=s_nGlobalFIFOLowerMargin);
        assert(nGlobalFIFOSize>s_nGlobalFIFOUpperMargin);
        assert(s_nGlobalFIFOUpperMargin<=s_nGlobalFIFOLowerMargin);

        // initialize the global queue
        //m_pGlobalFIFO = new sc_fifo<ST_request*>(nGlobalFIFOSize);
        m_pGlobalFIFOLIST = new list<ST_request*>();
        m_bBufferPriority = false;

        InitializeCacheLines();

        // initialize parameters
        m_nStateINI = STATE_INI_PROCESSING;
        m_nStatePAS = STATE_PAS_PROCESSING;

		//// initialize FIFO retry semaphore
		//m_nSmprFIFORetry = 0;

        m_nPidMin = -1;
        m_nPidMax = -1;

        // add the cache into static cache vector
        s_vecPtrCaches.push_back(this);

#ifdef MEM_MODULE_STATISTICS
        // statistics
        m_pStatCurReqINI = NULL;
        m_pStatCurReqNET = NULL;
        m_pStatCurSendNodeINI = NULL;
        m_pStatCurSendNodeNET = NULL;
        m_pmmStatRequestNo = NULL;
        m_pmmStatProcessingINI = NULL;
        m_pmmStatProcessingNET = NULL;
        m_pmmStatPipelineINI = NULL;
        m_pmmStatPipelineNET = NULL;
        m_pmmStatCleansingINI = NULL;
        m_pmmStatIncomingFIFOINI = NULL;
        m_pmmStatIncomingFIFONET = NULL;
        m_pmmStatSendNodeINI = NULL;
        m_pmmStatSendNodeNET = NULL;
        m_pmmStatTest = NULL;
        m_nStatReqNo = 0;
#endif
	}
    ~CacheL2TOK(){
        delete m_pPipelineINI;
        delete m_pPipelinePAS;
        delete m_pGlobalFIFOLIST;
        free(m_pQueueBuffer);
    }

	virtual void InitializeLog(const char* logName, LOG_LOCATION ll, VERBOSE_LEVEL verbose = VERBOSE_DEFAULT)
    {
        m_msbModule.InitializeLog(logName, ll, verbose);
        SimObj::InitializeLog(logName, ll, verbose);
    }

    virtual void InitializeCacheLines();

	virtual void BehaviorIni();
	virtual void BehaviorNet();

    virtual void BindProcTopology(ProcessorTOK &proc);

protected:
    // transactions handler
    // initiative
    virtual void ProcessInitiative();
    //virtual void SendAsNodeINIDB();
    //virtual void SendAsNodeINI();
    //virtual void SendAsSlaveINI();

    virtual bool SendAsNodeINI(ST_request* req);
    virtual bool SendAsSlaveINI(ST_request* req);

    //virtual void SendAsBothINI();

    virtual void SendFromINI();

    virtual ST_request* FetchRequestINI();
    // cleansing pipeline requests, if any of them are seeking the same line 
    virtual void CleansingPipelineINI(ST_request* req);
    // cleansing pipeline and insert request into queue
    virtual void CleansingAndInsert(ST_request* req);       // ONLY FOR INI
    //virtual void SetAvailableINI();
    //virtual void FinishCycleINI();
    // virtual void SendAsBothINIDB();     // send as slave first, then double request from the node

    // passive
    virtual void ProcessPassive();
    //virtual void SendAsNodePAS();
    //virtual void SendAsSlavePAS();
    //virtual void SendAsBothPAS();

    virtual bool SendAsNodePAS(ST_request* req);
    virtual bool SendAsSlavePAS(ST_request* req);
    virtual void SendFromPAS();
    //virtual void SetAvailablePAS();
    virtual ST_request* FetchRequestPAS();
    virtual void FinishCyclePAS();
    // skip this cycle without shifting the pipeline
    virtual void Postpone();

    //////////////////////////////////////////////////////////////////////////
    // pure virtual handler
    // initiative request handlers
    virtual void OnLocalRead(ST_request*) = 0;
    virtual void OnLocalWrite(ST_request*) = 0;
#ifdef WAIT_INVALIDATE_INNER_CACHE
    virtual void OnInvalidateRet(ST_request*) = 0;
#endif

    // facilitating functions
private:
    void Modify2AcquireTokenRequest(ST_request*, unsigned int);
    // transfer ntoken tokens from line to req, transfer tokens, transfer priority to the req if line has any.
    ST_request* NewDisseminateTokenRequest(ST_request*, cache_line_t*, unsigned int, bool bpriority);
public:
    void Modify2AcquireTokenRequestRead(ST_request*);
    void Modify2AcquireTokenRequestWrite(ST_request*, bool reqdate);
    void Modify2AcquireTokenRequestWrite(ST_request*, cache_line_t*, bool reqdate);
    ST_request* NewDisseminateTokenRequest(ST_request*, cache_line_t*);
    void PostDisseminateTokenRequest(cache_line_t*, ST_request*);
    void AcquireTokenFromLine(ST_request*, cache_line_t*, unsigned int);


    // passive request handlers
    virtual void OnAcquireTokenRem(ST_request*) = 0;
    virtual void OnAcquireTokenRet(ST_request*) = 0;
    virtual void OnAcquireTokenDataRem(ST_request*) = 0;
    virtual void OnAcquireTokenDataRet(ST_request*) = 0;
    virtual void OnDisseminateTokenData(ST_request*) = 0;
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    virtual void OnDirNotification(ST_request* req) = 0;
#endif

    // decide whether to invalidate by the hash function
    // true: invalida the line, 
    // false: keep the line, and the line should grab the tokens from the request
    virtual bool RacingHashInvalidation(int pid);

#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
    virtual void OnLocalReadResidue(ST_request*) = 0;   // local read at residue mode (suspended)
    virtual void OnLocalWriteResidue(ST_request*) = 0;  // local write at residue mode (suspended)
#endif

    //////////////////////////////////////////////////////////////////////////
    // common procedures
//    virtual bool DirectForward(ST_request* req);
    virtual bool MayHandle(ST_request* req);
    virtual bool CanHandleNetRequest(ST_request*);

    cache_line_t* LocateLine(__address_t);
    cache_line_t* LocateLineEx(__address_t);
    cache_line_t* GetReplacementLine(__address_t);
    cache_line_t* GetReplacementLineEx(__address_t);

    cache_line_t* GetEmptyLine(__address_t);

public:
	//virtual bool request(ST_request *req);
	virtual __address_t StartAddress() const {return m_nStartAddress;};
	virtual __address_t EndAddress() const {return m_nEndAddress;};

	virtual ST_request* PickRequest(ST_request* req);

    // cacheline, request update method
    virtual void UpdateCacheLine(cache_line_t* line, ST_request* req, CACHE_LINE_STATE state, unsigned int token, bool pending, bool invalidated, bool priority, bool tlock, LINE_UPDATE_METHOD lum, bool bupdatetime = true);
    virtual void IVUpdateCacheLine(cache_line_t* line, ST_request* req, CACHE_LINE_STATE state, unsigned int token, bool pending, bool invalidated, bool priority, bool tlock, LINE_UPDATE_METHOD lum, bool bupdatetime = true);
    virtual void UpdateRequest(ST_request* req, cache_line_t* line, MemoryState::REQUEST requesttype, __address_t address, bool bdataavailable=false, bool bpriority=false, bool btransient=false, unsigned int ntokenacquired=0xffff, REQUEST_UPDATE_METHOD rum = RUM_ALL);

    virtual char* GenerateMaskFromRequest(ST_request* req){return NULL;};
    virtual bool CheckLineValidness(cache_line_t* line, bool bwholeline = true, char* pmask = NULL);   // check the details into the bit-mask rather states
    //virtual void UpdateRequestPartial();

    //////////////////////////////////////////////////////////////////////////
    // Global Queue
    // 1. when loading a request from the queue to process, always load from the top
    // 2. when loading a request from the input buffer, always check whether there are requests seeking the same line, 
    //    if yes, put the request in the end of the global queue and skip the cycle by returning NULL in request fetch
    // 3. when a request has to be queued again during processing, all the requests seeking the same line in the pipeline
    //    has to be reversely pushed back to the global queue   // JXXX this might be optimized by distinguishing read and write
    // 4. the previously queued requests have to be reversely pushed back (push at front) to the queue
    // 5. the previously non-queued requests should be pushed from the back 
    // JXXX potential optimization, queued request probably can bypass 

    // Number Free
    unsigned int GlobalFIFONumberFree()
    {
        assert(m_pGlobalFIFOLIST->size() < m_nGlobalFIFOSize);

        return m_nGlobalFIFOSize - m_pGlobalFIFOLIST->size();
    }

    unsigned int GlobalFIFONumberAvailable()
    {
        assert(m_pGlobalFIFOLIST->size() < m_nGlobalFIFOSize);

        return m_pGlobalFIFOLIST->size();
    }

    bool GlobalFIFOEmpty() {
        assert(m_pGlobalFIFOLIST->size() < m_nGlobalFIFOSize);
	return m_pGlobalFIFOLIST->empty();
    }

    bool GlobalFIFONBRead(ST_request* &req)
    {
        assert(m_pGlobalFIFOLIST->size() < m_nGlobalFIFOSize);

        if (m_pGlobalFIFOLIST->empty())
            return false;

        req = m_pGlobalFIFOLIST->front();
        m_pGlobalFIFOLIST->pop_front();

        return true;
    }

    bool GlobalFIFONBWrite(ST_request* req)
    {
        assert(m_pGlobalFIFOLIST->size() < m_nGlobalFIFOSize);

        if (GlobalFIFONumberFree() == 0)
            return false;

        m_pGlobalFIFOLIST->push_back(req);

        return true;
    }

    // ONLY USED TO PUSH PREVIOUSLY QUEUED REQUESTS
    bool GlobalFIFOReversePush(ST_request* req)
    {
        assert(m_pGlobalFIFOLIST->size() < m_nGlobalFIFOSize);
        assert(req->bqueued == true);

        if (GlobalFIFONumberFree() == 0)
            return false;

        m_pGlobalFIFOLIST->push_front(req);
        
        return true;
    }


    // Check Buffer Threshold
    void CheckThreshold4GlobalFIFO()
    {
        assert(m_pGlobalFIFOLIST->size() < m_nGlobalFIFOSize);

        if (GlobalFIFONumberFree() <= s_nGlobalFIFOUpperMargin)
        {
            m_bBufferPriority = true;
        }
        else if (GlobalFIFONumberFree() >= s_nGlobalFIFOLowerMargin)
        {
            m_bBufferPriority = false;
        }
    }

    // USED TO INSERT BOTH PREVIOUSLY QUEUED AND NON-QUEUED REQUESTS
	// operation insert about global FIFO
	// if the FIFO is full return false;
	// otherwise return true;
	bool InsertRequest2GlobalFIFO(ST_request* req)
	{
        assert(m_pGlobalFIFOLIST->size() < m_nGlobalFIFOSize);

        if (GlobalFIFONumberFree() == 0)
            return false;

        if (req->bqueued)
            return GlobalFIFOReversePush(req);
        
        // the request will be a queued request
        req->bqueued = true;
        m_pGlobalFIFOLIST->push_back(req);

		return true;
	}

    // $$$ optimization $$$
    // according to location consistency, the request can be reversely pushed deeper than just to the top
    // the request from the same location can be sorted to the bottom of the queue
    // the reversely pushed request will be packed with other requests for the same line sink to the bottom of the global queue
    // the sorting method should only be used after a cleansing (including reverse push is happend)
    void SortGlobalFIFOAfterReversePush(ST_request* req)
    {
        assert(m_pGlobalFIFOLIST->size() < m_nGlobalFIFOSize);

        __address_t addr = req->getlineaddress();
        list<ST_request*>::iterator iter;
        vector<ST_request*> vecreq, vecother;

        for (iter=m_pGlobalFIFOLIST->begin();iter!=m_pGlobalFIFOLIST->end();iter++)
        {
            ST_request* reqtemp = *iter;

            if (reqtemp->getlineaddress() == addr)
                vecreq.push_back(reqtemp);
            else
                vecother.push_back(reqtemp);
        }

        // clean list
        m_pGlobalFIFOLIST->clear();

        // push back other requests
        for (unsigned int i=0;i<vecother.size();i++)
            m_pGlobalFIFOLIST->push_back(vecother[i]);

        // push back all the requests matchs the current address
        for (unsigned int i=0;i<vecreq.size();i++)
            m_pGlobalFIFOLIST->push_back(vecreq[i]);
    }
    
    // check queued request, from the queue register or the queue
    ST_request* PoPQueuedRequest()      // the same as NBRead
    {
        assert(m_pGlobalFIFOLIST->size() < m_nGlobalFIFOSize);

        ST_request* ret; 

        if (m_pGlobalFIFOLIST->empty())
            return NULL;

        ret = m_pGlobalFIFOLIST->front();
        m_pGlobalFIFOLIST->pop_front();

        return ret;
    }

    // check whether the request can be found in the queue
    bool DoesFIFOSeekSameLine(ST_request* req)
    {
        assert(m_pGlobalFIFOLIST->size() < m_nGlobalFIFOSize);

        list<ST_request*>::iterator iter;
        for (iter = m_pGlobalFIFOLIST->begin();iter != m_pGlobalFIFOLIST->end();iter++)
        {
            ST_request* reqtemp = *iter;

            if (reqtemp->getlineaddress() == req->getlineaddress())
                return true;
        }

        return false;
    }


    //////////////////////////////////////////////////////////////////////////
    // $$$ optimization $$$
    // read reply flushing (local read bypassing)
    void ReadReplyFlushing(ST_request* req)
    {
        assert(req->type == REQUEST_READ_REPLY);

        // check the pipeline and directly update the type and data of the requests on the same line
        // check pipeline requests
        __address_t addr = req->getlineaddress();

	pipeline_t::lst_t::const_iterator iter;

        for (iter=m_pPipelineINI->getlst().begin();iter!=m_pPipelineINI->getlst().end();iter++)
        {
            ST_request* reqpipe = (*iter);

            if (reqpipe == NULL)
            {
                continue;
            }
            else if ( (reqpipe->getlineaddress()==addr)&&(reqpipe->type == REQUEST_WRITE) )
            {
                break;
            }
            else if ( (reqpipe->getlineaddress()==addr)&&(reqpipe->type == REQUEST_READ) )
            {
                reqpipe->type = req->type;
                assert(g_nCacheLineSize==64);
                memcpy(reqpipe->data, req->data, g_nCacheLineSize);
            }
            else if (reqpipe->getlineaddress()==addr)
            {
                assert(false);
            }
        }
    }
    //////////////////////////////////////////////////////////////////////////


    //////////////////////////////////////////////////////////////////////////
    // if bProcessing is set then m_pReqCurINI will be set
    // otherwise m_pReqCurINI will remain the same, and simply shift the pipeline
    void AdvancePipelineINI(bool bProcessing = false)
    {
        ST_request* req_incoming = FetchRequestINI();   // NULL or something

        if (req_incoming != NULL)
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "request to get in pipeline : " << FMT_ADDR(req_incoming->getlineaddress()) << endl;
            clog << "\t"; print_request(req_incoming);
            LOG_VERBOSE_END
        }

        if (bProcessing)    // get request from pipeline
            m_pReqCurINI = m_pPipelineINI->shift(req_incoming);
        else    // simply shift
            m_pPipelineINI->shift(req_incoming);
    }

    //////////////////////////////////////////////////////////////////////////

    unsigned int PopEmptyQueueEntry()
    {
        unsigned int head = m_nEmptyQueueHead;

        if (m_nEmptyQueueHead != EOQ)
        {
            m_nEmptyQueueHead = m_pQueueBuffer[head].next;
        }
        
        return head;
    }

    void PushEmptyQueueEntry(unsigned int nEntry)
    {
        m_pQueueBuffer[nEntry].next = m_nEmptyQueueHead;

        m_nEmptyQueueHead = nEntry;
    }

	// insert a request to the outstanding request structure
	bool InsertOutstandingRequest(ST_request* req);

	// remove a request from the outstanding request structure
	ST_request* RemoveOutstandingRequest(__address_t addr);

    // create new eviction or write back request: 
    // bINI: is creating EV/WB from INI interface
    ST_request* NewEVorWBRequest(ST_request* req, cache_line_t* pline, bool beviction, bool bINI=true);

    // $$$ optimization $$$
    // victim line implementation and optimization

    // THIS METHOD SHOULD BE PERFORMED BEFORE INVALIDATION CARRIED OUT !!!
    // supplement line invalidation, this function does not perform real invalidation action
    // it only perform additional actions in addition to the invalidation (probably including optimizations)
    // bremote represent whether the request is a remote request invalidates a cacheline at normal state (RE_rem, IV_rem, ER_rem), 
    // or reply request (RS_ret@RPI, RE_ret@WPE, SR_ret@RPI, ER_ret@WPE, DE@WPE)
    void LineInvalidationExtra(ST_request* req, bool bremote)
    {
        if (m_nBBIPolicy == BBI_EAGER)
        {
            if (bremote)    // remote
            {
                assert(m_pReqCurPASasSlaveX == NULL);
                ST_request *newreq = new ST_request(req);
                //req->address = (address>>m_nLineBits)<<m_nLineBits;  // alert
                //ADD_INITIATOR_NODE(newreq, this);
                ADD_INITIATOR_NODE(newreq, (void*)NULL);
                newreq->type = REQUEST_INVALIDATE_BR;
                InsertSlaveReturnRequest(false, newreq);
                //m_pReqCurPASasSlave = newreq;
            }
            else    // return
            {
                // add backward broadcast flag into reply request   JXXX ???
                if (req->type == REQUEST_READ_REPLY)
                    req->bprocessed = true;
                // send procedure will deal with the processed flag for WR
            }
        }

#ifdef OPTIMIZATION_VICTIM_BUFFER
        // $$$ optimization victime buffer $$$
        //m_fabEvictedLine.RemoveBufferItem(addr);
        m_fabEvictedLine.RemoveBufferItem(req->getlineaddress());
#endif

        //// $$$ optimization non-existing line $$$
        //m_fabNonExistLine.InsertItem2Buffer(req->getlineaddress());
    }

    // this should be performed with local write
    // it will invalidate the item in the victim buffer
    void LocalWriteExtra(__address_t addr)
    {
#ifdef OPTIMIZATION_VICTIM_BUFFER
        // $$$ optimization victim buffer $$$
        m_fabEvictedLine.RemoveBufferItem(addr);
#endif
    }

#ifdef OPTIMIZATION_VICTIM_BUFFER
    // $$$ optimization for victim buffer $$$
    // Load from the victim buffer
    bool OnLocalReadVictimBuffer(ST_request* req);
#endif

    // $$$ optimization $$$
    // missed line bypassing. fast identify the missed lines and bypass the request to next node.
    //  extra stuff to do when suffer a line miss
    void LineMissedExtra(ST_request* req)
    {
#ifdef OPTIMIZATION_NON_EXIST_LINE_BYPASS_BUFFER
        // $$$ optimization non-existing line $$$
        m_fabNonExistLine.InsertItem2Buffer(req->getlineaddress());
#endif
    }

#ifdef OPTIMIZATION_NON_EXIST_LINE_BYPASS_BUFFER
    // optimization for missing line bypass
    void TryMissingLineBypass();
#endif

    // insert Slave return request for either INIasSlave or PASasSlave request
    void InsertSlaveReturnRequest(bool ini, ST_request *req);

    // insert network request for PAS interface
    void InsertPASNodeRequest(ST_request* req);

    // get a slave request if none on the spot
    void AutoFillSlaveReturnRequest(bool ini)
    {
        if (ini) 
        {
            if (m_pReqCurINIasSlaveX == NULL)
            {
	      if (!m_queReqINIasSlave.empty())
                {
                    m_pReqCurINIasSlaveX = m_queReqINIasSlave.front();
                    m_queReqINIasSlave.pop();
                }
                else
                    m_pReqCurINIasSlaveX = NULL;
            }
        }
        else
        {
            if (m_pReqCurPASasSlaveX == NULL)
            {
                if (!m_queReqPASasSlave.empty())
                {
                    m_pReqCurPASasSlaveX = m_queReqPASasSlave.front();
                    m_queReqPASasSlave.pop();
                }
                else
                    m_pReqCurPASasSlaveX = NULL;
            }
        }

    }

    // get a pas node request if none on the spot
    void AutoFillPASNodeRequest();

#ifdef MEM_MODULE_STATISTICS
    //////////////////////////////////////////////////////////////////////////
    // statistics
    virtual void InitializeStatistics(unsigned int components);
    virtual void InitCycleStatINI();                // cycle initialization for statistics
    virtual void InitCycleStatNET();                // cycle initialization for statistics
    virtual void Statistics(STAT_LEVEL lev);
    virtual void DumpStatistics(ofstream& outfile, unsigned int component, unsigned int type);
#endif

    //////////////////////////////////////////////////////////////////////////
    // function to prefetch data to caches ONLY_TEST
    // the fill process will not overwrite anydata already filled in the cache
    // true:  fill in an empty line
    // false: no place available to fill the data in
    bool PreFill(__address_t lineaddr, char* data);
 

#ifdef UNIVERSAL_ACTIVE_QUEUE_MODE
    virtual bool IsActiveQueueEmpty()
    {
        if (m_nActiveQueueHead >= QueueBufferSize)
        {
            cerr << ERR_HEAD_OUTPUT << "queue overflow detected!" << endl;
            
            return true; // return it's empty anyway
        }

        return (m_nActiveQueueHead==EOQ);
    }

    virtual void AppendActiveQueue(unsigned int nEntry)
    {
        if (m_nActiveQueueHead == EOQ)
        {
            m_nActiveQueueHead = nEntry;
        }
        else
        {
            m_nActiveQueueTail = nEntry;
            m_pQueueBuffer[nEntry].next = EOQ;
        }
    }

    virtual unsigned int GetActiveQueueEntry()
    {
        unsigned int head = m_nActiveQueueHead;

        if (m_nActiveQueueHead != EOQ)
        {
            m_nActiveQueueHead = m_pQueueBuffer[head].next;
        }

        return head;
    }
#endif


#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
    virtual ST_request* GetSuspendedRequestAtLine(cache_line_t* line)
    {
        unsigned int head = line->queuehead;
        if (head == EOQ)
        {
            return NULL;
        }

        // remove the head
        line->queuehead = m_pQueueBuffer[head].next;

        return m_pQueueBuffer[head].request;
    }

    void SetStateToResiduePAS()
    {
        assert(false);
        m_nStatePAS = STATE_PAS_RESIDUE;
        m_nWaitCountPAS = 0;
        m_pReqCurPASasSlave = NULL;  // reset combined requests ???

        // set the pas current request
        m_pReqCurPAS = m_pReqResidue;

        // reset the residue request 
        m_pReqResidue = NULL;
    }
#endif
};

//////////////////////////////
//} memory simulator namespace
}

#endif  // header 

