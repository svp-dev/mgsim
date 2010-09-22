#ifndef _DIRECTORYRT_TOK_H
#define _DIRECTORYRT_TOK_H

#include "predef.h"
#include "network_node.h"
#include "evicteddirlinebuffer.h"
#include "ddrmemorysys.h"
#include <queue>
#include <list>

namespace MemSim
{

class DirectoryRTTOK : public sc_module, public Network_Node, public CacheState
{
    struct dir_line_t
    {
        bool         valid;                 // is this line used?
        MemAddr  tag;                   // tag of the address of the line that's stored
        bool         reserved;              // the cacheline cannot be processed immediately
        unsigned int tokens;                // the number of tokens that the directory itself has
        bool         priority;              // represent the priority token
        std::queue<Message*> requests;   // Suspended requests
    };

    struct dir_set_t
    {
        dir_line_t *lines;
    };

    // Directory parameters
    unsigned int m_nLineSize;
	unsigned int m_nSet;
	unsigned int m_nAssociativity;
	dir_set_t   *m_pSet;
	
	std::queue<dir_line_t*> m_activelines;
    Message*             m_pReqCurNET2Net;
    Message*             m_pReqCurNET2Bus;
    EvictedDirLineBuffer    m_evictedlinebuffer;

    // Request queue from and to memory
    std::queue<Message*>& m_pfifoFeedback;
    std::queue<Message*>& m_pfifoMemory;

public:
	SC_HAS_PROCESS(DirectoryRTTOK);
	
	DirectoryRTTOK(sc_module_name nm, sc_clock& clock, DDRMemorySys& memory, unsigned int nset, unsigned int nassoc, unsigned int nlinesize)
      : sc_module(nm),
        m_nLineSize(nlinesize),
  	    m_nSet(nset), 
	    m_nAssociativity(nassoc), 
        m_pfifoFeedback(memory.channel_fifo_slave),
        m_pfifoMemory(memory.m_pfifoReqIn)
	{
        Message::s_nRequestAlignedSize = nlinesize;
        
		SC_METHOD(BehaviorNode);
		sensitive << clock.negedge_event();
		dont_initialize();

		SC_METHOD(BehaviorNET);
		sensitive << clock.posedge_event();
		dont_initialize();

		// process for reply from memory system
		SC_METHOD(BehaviorBUS);
		sensitive << clock.posedge_event();
		dont_initialize();

        // Allocate lines
        m_pSet = new dir_set_t[m_nSet];
        for (unsigned int i = 0; i < m_nSet; ++i)
        {
            m_pSet[i].lines = new dir_line_t[m_nAssociativity];
            for (unsigned int j = 0; j < m_nAssociativity; ++j)
            {
                m_pSet[i].lines[j].valid = false;
            }
        }
    }

    ~DirectoryRTTOK()
    {
        for (unsigned int i = 0; i < m_nSet; ++i)
    	    delete[] m_pSet[i].lines;
        delete[] m_pSet;
    }

private:    
    void BehaviorNode()
    {
        Network_Node::BehaviorNode();
    }

	void BehaviorNET();
    void BehaviorBUS();

    void ProcessRequestNET(Message* req);

    bool SendRequestNETtoBUS(Message*);

	dir_line_t* LocateLine(MemAddr);
	dir_line_t* GetEmptyLine(MemAddr);
	
	unsigned int DirIndex(MemAddr);
	uint64 DirTag(MemAddr);
};

}

#endif

