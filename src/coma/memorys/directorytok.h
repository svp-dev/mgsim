#ifndef _DIRECTORYTOK_H
#define _DIRECTORYTOK_H

#include "predef.h"
#include "network_node.h"
#include "evicteddirlinebuffer.h"
#include <list>

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

class NetworkBelow_Node : public Network_Node
{
};

class NetworkAbove_Node : public Network_Node
{
};

class DirectoryTOK : public sc_module, public CacheState,  public NetworkBelow_Node, public NetworkAbove_Node
{
    struct dir_line_t
    {
        bool valid;
        MemAddr tag;

        unsigned int tokencount;    // the number of tokens that the directory itself has
    
        bool priority;              // represent the priority token

        unsigned int nrequestin;    // remote request in local level
                                    // with remote request inside the local level,
                                    // the directory line can still be evicted.
                                    // * DD will not be counted in any way
                                    // * since it might never get out the directory.
                                    // * to avoid inform the directory when consumed, it will not be counted
                                    
        unsigned int nrequestout;   // local requests in global level

        int ntokenline;     // tokens inside on the lines
                            // *** in naive directory scheme
                            // *** the tokencount represent the token directory hold,
                            // *** token group holds the tokens that local caches has
                            // *** the two have no intersections in local directory.

        int ntokenrem;     // tokens inside from remote request
    };

    struct dir_set_t
    {
        dir_line_t *lines;
    };

    CacheID      m_firstCache;
    CacheID      m_lastCache;
    unsigned int m_nLineSize;
	unsigned int m_nSet;
	unsigned int m_nAssociativity;
	dir_set_t   *m_pSet;

    // evicted dirline buffer
    EvictedDirLineBuffer m_evictedlinebuffer;

    // current request
    Message* m_pReqCurABO;
    Message* m_pReqCurBEL;

    std::list<Message*>   m_lstReqB2a;
    std::list<Message*>   m_lstReqB2b;
    std::list<Message*>   m_lstReqA2a;
    std::list<Message*>   m_lstReqA2b;

    Message* m_pReqCurABO2a;
    Message* m_pReqCurABO2b;
    Message* m_pReqCurBEL2a;
    Message* m_pReqCurBEL2b;

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

	void BehaviorBelowNet();
	void BehaviorAboveNet();

    void ProcessRequestBEL();
    void ProcessRequestABO();

    void SendRequestFromBEL();
    void SendRequestFromABO();

    void OnABOAcquireToken(Message *);
    void OnABOAcquireTokenData(Message *);
    void OnABODisseminateTokenData(Message *);

    void OnBELAcquireToken(Message *);
    void OnBELAcquireTokenData(Message *);
    void OnBELDisseminateTokenData(Message *);
    void OnBELDirNotification(Message *);

    bool IsBelow(CacheID id) const;
    
    dir_line_t* LocateLine(MemAddr);
    dir_line_t* GetEmptyLine(MemAddr);

    void BehaviorNodeAbove()
    {
        NetworkAbove_Node::BehaviorNode();
    }

    void BehaviorNodeBelow()
    {
        NetworkBelow_Node::BehaviorNode();
    }
public:
	// directory should be defined large enough to hold all the information in the hierarchy below
	SC_HAS_PROCESS(DirectoryTOK);
	
	DirectoryTOK(sc_module_name nm, sc_clock& clock, CacheID firstCache, CacheID lastCache, unsigned int nset, unsigned int nassoc, unsigned int nlinesize, unsigned int latency = 5) 
      : sc_module(nm),
        m_firstCache(firstCache),
        m_lastCache(lastCache),
        m_nLineSize(nlinesize),
	    m_nSet(nset),
	    m_nAssociativity(nassoc),
        m_nStateABO(STATE_ABOVE_PROCESSING),
        m_nStateBEL(STATE_BELOW_PROCESSING),
        m_pPipelineABO(latency),
        m_pPipelineBEL(latency)
	{
	    Message::s_nRequestAlignedSize = nlinesize;
	    
        // forward below interface
        SC_METHOD(BehaviorNodeBelow);
        sensitive << clock.negedge_event();
        dont_initialize();

        // forward above interface
        SC_METHOD(BehaviorNodeAbove);
        sensitive << clock.negedge_event();
        dont_initialize();
	    
        // handle below interface
		SC_METHOD(BehaviorBelowNet);
		sensitive << clock.posedge_event();
		dont_initialize();

        // handle above interface
		SC_METHOD(BehaviorAboveNet);
		sensitive << clock.posedge_event();
		dont_initialize();

        // allocate sets
        m_pSet = new dir_set_t[m_nSet];

        // Allocate lines
        for (unsigned int i = 0; i < m_nSet; ++i)
        {
            m_pSet[i].lines = new dir_line_t[m_nAssociativity];
            for (unsigned int j = 0; j < m_nAssociativity; ++j)
            {
                m_pSet[i].lines[j].valid = false;
                m_pSet[i].lines[j].tokencount = 0;
                m_pSet[i].lines[j].priority = false;
                m_pSet[i].lines[j].nrequestin = 0;
                m_pSet[i].lines[j].nrequestout = 0;
                m_pSet[i].lines[j].ntokenline = 0;
                m_pSet[i].lines[j].ntokenrem = 0;
            }
        }
	}

    ~DirectoryTOK()
    {
        for (unsigned int i = 0; i < m_nSet; ++i)
            delete[] m_pSet[i].lines;
        delete[] m_pSet;
    }
};

}
#endif
