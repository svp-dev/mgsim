#ifndef _CACHEL2_TOK_H
#define _CACHEL2_TOK_H

#include "network_node.h"
#include "processortok.h"
#include "mergestorebuffer.h"
#include <queue>
#include <set>

namespace MemSim
{

class CacheL2TOK : public sc_module, public Network_Node, public CacheState
{
    CacheID        m_id;
    unsigned int   m_lineSize;
    unsigned int   m_nSets;           // Cache set count
    unsigned int   m_assoc;           // Cache associativity
    cache_line_t  *m_lines;           // The lines

    // states
    enum STATE_INI
    {
        STATE_INI_PROCESSING,
        STATE_INI_RETRY,
    };

    enum STATE_PAS
    {
        STATE_PAS_PROCESSING,
        STATE_PAS_POSTPONE,
        STATE_PAS_RETRY,
    };

    // current request
    Message* m_pReqCurINI;
    Message* m_pReqCurPAS;

    Message*        m_pReqCurINIasNodeDB;   // the one needs double retry
    Message*        m_pReqCurINIasNode;
    Message*        m_pReqCurINIasSlaveX;
    std::queue<Message*> m_queReqINIasSlave;

    Message*        m_pReqCurPASasNodeX;
    Message*        m_pReqCurPASasSlaveX;
    std::queue<Message*> m_queReqPASasNode;
    std::queue<Message*> m_queReqPASasSlave;

    STATE_INI m_nStateINI;
    STATE_PAS m_nStatePAS;

    // pipeline
    pipeline_t m_pPipelineINI;
    pipeline_t m_pPipelinePAS;

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
    bool              m_bBufferPriority;
	unsigned int      m_nGlobalFIFOSize;
    std::list<Message*> m_pGlobalFIFOLIST;

    static unsigned int s_nGlobalFIFOUpperMargin;     // UPPER Margin (corresponding to size since it will compared with free buffer size)
    static const unsigned int s_nGlobalFIFOLowerMargin;     // LOWER Margin (corresponding to size since it will compared with free buffer size)

    // Merge Store Buffer Implementation
    // 0. pending line can lock the line itself itself by further access (any further accesses or specific access -- decided by llock TBD)
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
    // *  merge buffer slot/line are locked or not is decided by the llock variable
    // *  all request without immediate reply to the processor, will have to be queued in the merged line queue. 
    //    the merged line queue will be served before any other requests suspended on the line or even in the pipeline
    // *  change in handling AD/RS/SR return on write pending state

    // Merge Store buffer module provide the merge capability on stores on the pending lines
    MergeStoreBuffer m_msbModule;

    // Try to inject evicted lines?
    bool m_inject;
    
    bool SendAsSlave(Message* req);
public:
    std::set<ProcessorTOK*> m_processors;     // The processors connected to this cache
    std::queue<Message*> m_requests;       // Incoming from processors

    void RegisterProcessor(ProcessorTOK& processor)
    {
        m_processors.insert(&processor);
    }

    void UnregisterProcessor(ProcessorTOK& processor)
    {
        m_processors.erase(&processor);
    }

	SC_HAS_PROCESS(CacheL2TOK);
	
    CacheL2TOK(sc_module_name nm, sc_clock& clock, CacheID id,
        unsigned int nset, unsigned int nassoc, unsigned int nlinesize,
        bool inject,
        unsigned int latency = 5,
		unsigned int nGlobalFIFOSize = 0x100)
      : sc_module(nm),
        m_id(id),
        m_lineSize(nlinesize),
        m_nSets(nset), m_assoc(nassoc),
        m_nStateINI(STATE_INI_PROCESSING),
        m_nStatePAS(STATE_PAS_PROCESSING),
        m_pPipelineINI(latency-1),
        m_pPipelinePAS(latency),
        m_msbModule(3),
        m_inject(inject)
	{
	    assert(m_lineSize <= MAX_MEMORY_OPERATION_SIZE);
	    
        Message::s_nRequestAlignedSize = nlinesize;

        // Allocate lines
        m_lines = new cache_line_t[m_nSets * m_assoc];
        for (unsigned int i = 0; i < m_nSets * m_assoc; ++i)
        {
            m_lines[i].valid = false;
        }

        assert(latency > 1);

        SC_METHOD(BehaviorNode);
        sensitive << clock.negedge_event();
        dont_initialize();

        SC_METHOD(BehaviorIni);
        sensitive << clock.posedge_event();
        dont_initialize();

        SC_METHOD(BehaviorNet);
        sensitive << clock.posedge_event();
        dont_initialize();

        // rewrite upper margin
        s_nGlobalFIFOUpperMargin = latency + 2;

		// save the size of Global Queue 
		m_nGlobalFIFOSize = nGlobalFIFOSize;
        assert(nGlobalFIFOSize>=s_nGlobalFIFOLowerMargin);
        assert(nGlobalFIFOSize>s_nGlobalFIFOUpperMargin);
        assert(s_nGlobalFIFOUpperMargin<=s_nGlobalFIFOLowerMargin);

        // initialize the global queue
        m_bBufferPriority = false;
	}

    ~CacheL2TOK()
    {
        delete[] m_lines;
    }

private:    
    void BehaviorNode()
    {
        Network_Node::BehaviorNode();

        // At the end of every cycle, check the MGSim/SystemC interface
        for (std::set<ProcessorTOK*>::iterator iter = m_processors.begin(); iter != m_processors.end(); ++iter)
        {
            (*iter)->OnCycleEnd();
        }
    }
	
	void BehaviorIni();
	void BehaviorNet();

    // transactions handler
    // initiative
    void ProcessInitiative();

    void SendFromINI();

    Message* FetchRequestINIFromQueue();

    // cleansing pipeline and insert request into queue
    void CleansingAndInsert(Message* req);

    // passive
    void ProcessPassive();
    void SendFromPAS();

    void OnLocalRead(Message*);
    void OnLocalWrite(Message*);

    void EvictLine(cache_line_t*);

    // passive request handlers
    void OnAcquireTokenRem(Message*);
    void OnAcquireTokenRet(Message*);
    void OnAcquireTokenDataRem(Message*);
    void OnAcquireTokenDataRet(Message*);
    void OnDisseminateTokenData(Message*);

    cache_line_t* LocateLine(MemAddr);
    cache_line_t* GetEmptyLine(MemAddr);

    void OnPostAcquirePriorityToken(cache_line_t*, Message*);

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
    cache_line_t* GetReplacementLine(MemAddr);

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

    // USED TO INSERT BOTH PREVIOUSLY QUEUED AND NON-QUEUED REQUESTS
	// operation insert about global FIFO
	// if the FIFO is full return false;
	// otherwise return true;
	bool InsertRequest2GlobalFIFO(Message* req)
	{
        assert(m_pGlobalFIFOLIST.size() < m_nGlobalFIFOSize);

        if (m_nGlobalFIFOSize == m_pGlobalFIFOLIST.size())
            return false;

        if (req->bqueued)
        {
            // ONLY USED TO PUSH PREVIOUSLY QUEUED REQUESTS
            m_pGlobalFIFOLIST.push_front(req);
        }
        else
        {
            // the request will be a queued request
            req->bqueued = true;
            m_pGlobalFIFOLIST.push_back(req);
		}
        return true;
	}

    // insert Slave return request for either INIasSlave or PASasSlave request
    void InsertSlaveReturnRequest(bool ini, Message *req);

    // insert network request for PAS interface
    void InsertPASNodeRequest(Message* req);

    // get a slave request if none on the spot
    Message* GetSlaveReturnRequest(bool ini)
    {
        Message* req = NULL;
        if (ini) 
        {
            if (!m_queReqINIasSlave.empty())
            {
                req = m_queReqINIasSlave.front();
                m_queReqINIasSlave.pop();
            }
        }
        else
        {
            if (!m_queReqPASasSlave.empty())
            {
                req = m_queReqPASasSlave.front();
                m_queReqPASasSlave.pop();
            }
        }
        return req;
    }

    // get a pas node request if none on the spot
    Message* GetPASNodeRequest()
    {
        Message* req = NULL;
        if (!m_queReqPASasNode.empty())
        {
            req = m_queReqPASasNode.front();
            m_queReqPASasNode.pop();
        }
        return req;
    }
};

}

#endif

