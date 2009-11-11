#include "cachest.h"
using namespace MemSim;

// procedures and functions

void CacheST::InitializeCacheLines()
{
#ifdef SIMULATE_DATA_TRANSACTION
    // allocate all the data buffer
    unsigned int nByte = m_nSet * m_nAssociativity * s_nLineSize;
    m_pBufData = (char*)calloc(nByte, sizeof(char));
#endif

    // allocate sets
    m_pSet = (cache_set_t*)malloc(m_nSet * sizeof(cache_set_t));

    // allocate lines
    for (unsigned int i=0;i<m_nSet;i++)
    {
        m_pSet[i].lines = (cache_line_t*)malloc(m_nAssociativity * sizeof(cache_line_t));
        for (unsigned int j=0;j<m_nAssociativity;j++)
        {
#ifndef TOKEN_COHERENCE
            m_pSet[i].lines[j].state = LNINVALID;
#else
            m_pSet[i].lines[j].state = CLS_INVALID;
            m_pSet[i].lines[j].tokencount = 0;
            m_pSet[i].lines[j].invalidated = false;
            m_pSet[i].lines[j].priority = false;
            m_pSet[i].lines[j].pending = false;
            m_pSet[i].lines[j].tlock = false;
            m_pSet[i].lines[j].SetLineLock(false);

            m_pSet[i].lines[j].breserved = false;
#endif

#ifdef SIMULATE_DATA_TRANSACTION
            m_pSet[i].lines[j].data = &m_pBufData[i * m_nAssociativity * s_nLineSize+ j * s_nLineSize];	// ???!!!***%%%
#endif

            for (unsigned int k = 0;k<CACHE_BIT_MASK_WIDTH/8;k++)
            m_pSet[i].lines[j].bitmask[k] = 0;
        }
    }

LOG_VERBOSE_BEGIN(VERBOSE_BASICS)
    clog << LOG_HEAD_OUTPUT << dec << m_nSet << "-set "  << m_nAssociativity << "-way associative " << s_nLineSize << "-byte line cache initialized with delay of " << m_nLatency << endl;
LOG_VERBOSE_END
}

CacheST::~CacheST()
{
#ifdef SIMULATE_DATA_TRANSACTION
    // free the data buffer
    free(m_pBufData);
#endif
    
    // free the lines
    for (unsigned int i=0;i<m_nSet;i++)
    {
        free((char*)m_pSet[i].lines);
    }

    // free the set
    free((char*)m_pSet);
}

bool CacheST::CheckDataConsistency(cache_line_t* line, ST_request* req, bool bfull)
{

#ifdef SIMULATE_DATA_TRANSACTION
    bool blinecomplete = line->IsLineAtCompleteState();

	// check the mask bits, and only update the unmasked ones
	for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH;i++)
	{
		unsigned int maskhigh = i/8;
		unsigned int masklow = i%8;

		char testchr = 1 << masklow;

        if ( ((bfull) && (blinecomplete))    // full with line full
            || ((line->bitmask[maskhigh]&testchr) != 0)	) // if it's not full or it's not complete, then consult the masked
		{
			for (unsigned int j=0;j<CACHE_REQUEST_ALIGNMENT;j++)
			{
				if (line->data[i*CACHE_REQUEST_ALIGNMENT+j] != req->data[i*CACHE_REQUEST_ALIGNMENT+j])
					return false;
			}
		}
	}

	return true;
#else

	return true;
#endif
}

