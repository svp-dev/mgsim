#include "cachest.h"
using namespace MemSim;

CacheST::CacheST(sc_module_name nm, unsigned int nset, unsigned int nassoc, unsigned int nlinesize)
  : sc_module(nm),
    m_nLineSize(nlinesize),
    m_nSet(nset), m_nAssociativity(nassoc)
{
    ST_request::s_nRequestAlignedSize = nlinesize;

    // allocate all the data buffer
    unsigned int nByte = m_nSet * m_nAssociativity * m_nLineSize;
    m_pBufData = (char*)calloc(nByte, sizeof(char));

    // allocate sets
    m_pSet = (cache_set_t*)malloc(m_nSet * sizeof(cache_set_t));

    // allocate lines
    for (unsigned int i = 0; i < m_nSet; ++i)
    {
        m_pSet[i].lines = (cache_line_t*)malloc(m_nAssociativity * sizeof(cache_line_t));
        for (unsigned int j = 0; j < m_nAssociativity; ++j)
        {
            m_pSet[i].lines[j].state = CLS_INVALID;
            m_pSet[i].lines[j].tokencount = 0;
            m_pSet[i].lines[j].invalidated = false;
            m_pSet[i].lines[j].priority = false;
            m_pSet[i].lines[j].pending = false;
            m_pSet[i].lines[j].tlock = false;
            m_pSet[i].lines[j].llock = false;
            m_pSet[i].lines[j].breserved = false;
            m_pSet[i].lines[j].data = &m_pBufData[(i * m_nAssociativity + j) * m_nLineSize];

            for (unsigned int k = 0;k<CACHE_BIT_MASK_WIDTH/8;k++)
                m_pSet[i].lines[j].bitmask[k] = 0;
        }
    }
}

CacheST::~CacheST()
{
    free(m_pBufData);
    for (unsigned int i=0;i<m_nSet;i++)
    {
        free(m_pSet[i].lines);
    }
    free(m_pSet);
}
