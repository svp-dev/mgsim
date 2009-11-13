#ifndef _DIRECTORYRT_TOK_H
#define _DIRECTORYRT_TOK_H

// root directory

#include "predef.h"
#include "setassociativeprop.h"
#include "busst_slave_if.h"
#include "busst_master.h"
#include "ringnode.h"

#include "networkbelow_if.h"

#include "suspendedrequestqueue.h"
#include "evicteddirlinebuffer.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class DirectoryRTTOK : public sc_module, public CacheState/*,public BusST_Slave_if*/, public NetworkBelow_if, public RingNode, public BusST_Master, public SetAssociativeProp
{
public:
	sc_in<bool> port_clk;

protected:
    //////////////////////////////////////////////////////////////////////////
    // directory parameters
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
    SuspendedRequestQueue m_srqSusReqQ;

#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
    unsigned int m_nRepeatLatency;      // the latency for repeating the same address
    // still use m_nWaitCountPAS for counting
    ST_request* m_pReqResidue;             // the residue request picked up
    CACHE_LINE_STATE m_nResidueState;   // save the state a line supposed to be
    // so that the next request to be processed 
    // will continue from current state directly
#endif

#ifdef MEM_MODULE_STATISTICS
    //////////////////////////////////////////////////////////////////////////
    // statistics on different components
    ST_request* m_pStatCurReqNET;                   // backup the current request PAS for statistics
    unsigned int m_nStatReqNo;                      // total request number
    static const int s_nStatSetTraceNo = 0x40;

    map<double, stat_stru_size_t>              *m_pmmStatRequestNo;
    map<double, stat_stru_request_t>           *m_pmmStatProcessingNET;
    map<double, stat_stru_set_t>               *m_pmmStatSetTrace;
#endif

    //////////////////////////////////////////////////////////////////////////
    // directory parameters

    // delay count
    UINT m_nWaitCountNET;
    UINT m_nWaitCountBUS;

    // current request
    ST_request* m_pPrefetchDeferredReq;
    ST_request* m_pReqCurNET;
    ST_request* m_pReqCurBUS;


    list<ST_request*>   m_lstReqNET2Net;
    ST_request* m_pReqCurNET2Net;
    ST_request* m_pReqCurNET2Bus;

    ST_request* m_pReqCurBUS2Net;

    // state
    enum STATE_NET{
        STATE_NET_PROCESSING,
        STATE_NET_RETRY
    };

    enum STATE_BUS{
        STATE_BUS_PROCESSING,
        STATE_BUS_RETRY_TO_NET
    };

    STATE_NET m_nStateNET;
    STATE_BUS m_nStateBUS;

    // pipeline
    pipeline_t  *m_pPipelineNET;
    pipeline_t  *m_pPipelineBUS;

    // current map, line bits, set bits, split dir bits, then tabs
    unsigned int m_nSplitDirBits;
    unsigned int m_nSplitDirID;
//    unsigned int m_nSplitDirStartBit;


#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    // evicted dirline buffer
    EvictedDirLineBuffer m_evictedlinebuffer;
#endif

    INJECTION_POLICY m_nInjectionPolicy;

public:
	// directory should be defined large enough to hold all the information in the hierarchy below
	SC_HAS_PROCESS(DirectoryRTTOK);
	DirectoryRTTOK(sc_module_name nm, unsigned int nset, unsigned int nassoc, 
		       unsigned int nlinesize, INJECTION_POLICY nIP=IP_NONE, 
		       unsigned int nSplitMode = 0x100, 
		       REPLACE_POLICY policyR = RP_LRU, 
		       unsigned char policyW = (WP_WRITE_THROUGH|WP_WRITE_AROUND), 
		       __address_t startaddr=0, 
		       __address_t endaddr= MemoryState::MEMORY_SIZE, 
		       UINT latency = 5) 
		: /*BusST_Master(nm)*/ sc_module(nm), 
	  RingNode(), 
	  SetAssociativeProp(nlinesize), 
	  m_nSet(nset), 
	  m_nAssociativity(nassoc), 
	  m_policyReplace(policyR), 
	  m_policyWrite(policyW), 
	  m_nStartAddress(startaddr),
	  m_nEndAddress(endaddr), 
	  m_nLatency(latency), 
	  m_srqSusReqQ(0x200, 0x100), 
#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
	  m_evictedlinebuffer(0x20),
#endif
	  m_nInjectionPolicy(nIP)
	{

		SC_METHOD(BehaviorNET);
		sensitive << port_clk.pos();
		dont_initialize();

		// process for reply from memory system
		SC_METHOD(BehaviorBUS);
		sensitive << port_clk.pos();
		dont_initialize();

		//m_pfifoReqIn = new sc_fifo<ST_request*>();

        m_nSetBits = lg2(m_nSet);
		//m_nLineBits = lg2(m_nLineSize);

        m_pfifoFeedback = new sc_fifo<ST_request*>();

		// allocate space for all cache lines
		InitializeDirLines();

        // initialize parameter
        m_nWaitCountBUS = 0;
        m_nWaitCountNET = 0;

        m_nStateBUS = STATE_BUS_PROCESSING;
        m_nStateNET = STATE_NET_PROCESSING;

        // pipeline
        m_pPipelineBUS = new pipeline_t(latency);
        m_pPipelineNET = new pipeline_t(latency);

#ifdef MEM_MODULE_STATISTICS
        // statistics 
        m_pmmStatRequestNo = NULL;
        m_pmmStatProcessingNET = NULL;
        m_pmmStatSetTrace = NULL;
        m_bDirectory = true;
#endif

        assert(latency-1>0);

        // split mode:
        // lower 8 bits :   id
        // 8-15 bits:       total split dir number
        // 16-24 bits:      start bits  (not used for now)
        if (nSplitMode < 0x100)
        {
            cout << "split mode " << nSplitMode << endl;
            abort();
        }
        else
        {
            m_nSplitDirID = nSplitMode & 0xff;
            m_nSplitDirBits = log2((nSplitMode >> 8) & 0xff);
        }
	}

    ~DirectoryRTTOK(){
        for (unsigned int i=0;i<m_nSet;i++)
    	    free(m_pSet[i].lines);
        free(m_pSet);
        delete m_pfifoFeedback;
        delete m_pPipelineBUS;
        delete m_pPipelineNET;
    }

	void InitializeDirLines();

	void BehaviorNET();
     void BehaviorBUS();

     void ProcessRequestNET();
     void ProcessRequestBUS();

     bool SendRequestNETtoNET(ST_request*);
     bool SendRequestNETtoBUS(ST_request*);
     void SendRequestFromNET();

     void SendRequestBUStoNet();

    // void TerminateRequestNet();         // get off the current work and reset the cache state to idle
    //                                            // only for network interface

	// transactions

virtual     void OnNETAcquireToken(ST_request *) = 0;
virtual     void OnNETAcquireTokenData(ST_request *) = 0;
virtual     void OnNETDisseminateTokenData(ST_request *) = 0;


	// void OnBUSReadReply(ST_request*)=0;
virtual     void OnBUSSharedReadReply(ST_request*)=0;
virtual     void OnBUSExclusiveReadReply(ST_request*)=0;


#ifdef MEM_MODULE_STATISTICS
    //////////////////////////////////////////////////////////////////////////
    // statistics 
     void InitializeStatistics(unsigned int components);
     void Statistics(STAT_LEVEL lev);
     void DumpStatistics(ofstream& outfile, unsigned int component, unsigned int type);
#endif

    //////////////////////////////////////////////////////////////////////////
    // cache interfaces
	dir_line_t* LocateLine(__address_t);
	dir_line_t* GetReplacementLine(__address_t);
//    dir_line_t* GetEmptyLine(__address_t);
	unsigned int DirIndex(__address_t);
	uint64 DirTag(__address_t);

	//bool request(ST_request *req);
	 __address_t StartAddress() const {return m_nStartAddress;};
	 __address_t EndAddress() const {return m_nEndAddress;};

	ST_request* PickRequest(ST_request* req);

    // check directory lines, if there are any error, false will be returned. otherwise true
    // correct the 0-counter line state to INVALID
    bool CheckDirLines(dir_line_t* line);

    // fix the directory line states
    bool FixDirLine(dir_line_t* line);

    //////////////////////////////////////////////////////////////////////////
    // queue handling

    // CHKS potential optimization, queued request probably can bypass 

    // append request to the line
    // fail if the request buffer is full
    // bool AppendRequest2Line(ST_request* req, dir_line_t* line);

    // reversely append request to the line from the head side
    // bool ReverselyAppendRequest2Line(ST_request* req, dir_line_t* line);

    // check whether the line is being loaded or have suspended requests
    // bool HasOutstandingRequest(dir_line_t* line);

    // append lines to line queue
    // bool AppendLine2LineQueue(dir_line_t* line);

    // remove the line from the line queue
    // if the line is not fro the top of the line queue, false is returned
    // bool RemoveLineFromLineQueue(dir_line_t* line);

    // fetch request from input buffer
    // [JNEW] the fetched request from the input buffer will get into to the pipeline
     ST_request* FetchRequestNet();

    // prefetch deferred request head,
    // if the request can be passed directly it will be popped
    // otherwise, no pop action will be taken 
    // [JNEw] the fetched request from the deferred buffer should be stored in m_pPrefetchDeferredReq from caller
    // [JNEW] and the deferred request will be processed through the bus or network
     ST_request* PrefetchDeferredRequest();

    //// pop out the deferred request head
    //// * popdeferredrequest will set the AUXSTATE to NONE, which could be incorrect, since sometimes it should be loading
    //// ** the auxstate should always be modified after calling pop
    // ST_request* PopDeferredRequest(dir_line_t*&line);

    // pop out the deferred request head
    // * popdeferredrequest will set the AUXSTATE to NONE, which could be incorrect, since sometimes it should be loading
    // ** the auxstate should always be modified after calling pop
    // *** when bRem == true, the line will be removed from the line queue, otherwise, keep the line there. 
    // ST_request* PopDeferredRequest(bool bRem = false);
     ST_request* PopDeferredRequest();

    //// whether current request should be deferred and switch to prefetched deferred request directly
    // bool ShouldHotSwitch(ST_request* req);

    // cleansing pipeline requests, if any of them are seeking the same line 
    // append the requests if necessary
    // bappendreq is to notify the function to append the specified request
     void CleansingPipelineAndAppendNet(ST_request* req, dir_line_t* line, bool bappendreq = true);

     void FinishCycleNET();
     void FinishCycleBUS();

    //////////////////////////////////////////////////////////////////////////
    // 1. network request always has priority   // JXXX adjusted a bit, but almost always 
    // 2. when processing a previously non-queued request, find the line has queue associated, 
    //    it will be added to the end of queue, it will be added to the end of the queue, no matter what
    // 3. when processing a previously non-queued request, find the line has no queue, then do whatever correct
    // 4. when processing a previously queued request, find the line has queue associated,
    //    a cleansing method will be used, and all the previously queued request will be reversely pushed into the queue reversely
    //    all the previously non-queued request will be pushed into the queue
    // 5. when processing a previously queued request, find the line has no queue associated, do whatever appropriate
    //////////////////////////////////////////////////////////////////////////


    //////////////////////////////////////////////////////////////////////////
    // network interface, Network_if
     bool DirectForward(ST_request* req);
     bool MayHandle(ST_request* req);
     bool CanHandleNetRequest(ST_request*);

    // find line no. within a set
    // return id; -1 means error
    int LineNumber(dir_set_t* pset, dir_line_t* pline)
    {
        for (unsigned int i=0;i<m_nAssociativity;i++)
        {
            if (&(pset->lines[i]) == pline)
                return i;
        }

        return -1;
    }

    // debug correctness checking
    void DebugCheckCounterConsistency()
    {
        for (unsigned int i=0;i<m_nSet;i++)
        {
            unsigned int total = 0;
            for (unsigned int j=0;j<m_nAssociativity;j++)
            {
                total += m_pSet[i].lines[j].counter;
            }

            if (total > m_nAssociativity)
            {
                cout << dec << "*** set: " << i << endl;
                abort();
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // function to prefetch data to caches with Directory to support ONLY_TEST
    // basically match the state and counter
    // true:  fill in an empty line
    // false: no place available to fill the data in
    bool PreFill(__address_t lineaddr);
};

//////////////////////////////
//} memory simulator namespace
}

#endif  // header

