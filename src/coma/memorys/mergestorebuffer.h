#ifndef _MERGESTOREBUFFER_H
#define _MERGESTOREBUFFER_H

#include "fabuffer.h"
#include "predef.h"

// Memory Store Buffer is constructed with FABuffer,
// buffer lines are used to be use for memory store buffer

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////

class MergeStoreBuffer : virtual public SimObj
{
private:
    // the data container 
    FABuffer<__address_t, cache_line_t> m_fabLines;

    // FABuffer uses fixed index, the queued requests are corresponding to the index in the fablines
    vector<ST_request*>**    m_pvecRequestQueue;

    // cache line data
    char**      m_ppData;
    ST_request**    m_ppMergedRequest;

    unsigned int m_nSize;

public:
    MergeStoreBuffer(unsigned int size) : SimObj("MergeStoreBuffer"), m_fabLines(size), m_nSize(size)
    {
        m_pvecRequestQueue = (vector<ST_request*>**)malloc(sizeof(vector<ST_request*>*)*size);
        m_ppData = (char**)malloc(sizeof(char*)*size);
        m_ppMergedRequest = (ST_request**)malloc(size*sizeof(ST_request*));
        for(unsigned int i=0;i<size;i++)
        {
            m_pvecRequestQueue[i] = new vector<ST_request*>();
            m_ppData[i] = (char*)malloc(g_nCacheLineSize);
            m_ppMergedRequest[i] = new ST_request();
            m_ppMergedRequest[i]->type = MemoryState::REQUEST_NONE;
        }
    }

    virtual ~MergeStoreBuffer()
    {
        // data inside will be cleared by the FABuffer destructor
        for (unsigned int i=0;i<m_nSize;i++)
        {
            delete m_pvecRequestQueue[i];
            free(m_ppData[i]);

            delete m_ppMergedRequest[i];
        }

        free(m_pvecRequestQueue);
        free(m_ppData);
        free(m_ppMergedRequest);
    }

    /////////////////////////////////////////////////////////////////
    // load and write function
    // load will always load a whole line, 
    // sometime might need the current line to serve part of the line since the msb line might be imcomplete
    //virtual bool LoadBufferWithLine(cache_line_t* line, __address_t address, char* data, ST_request* req)
    //{
    
    //}

    // write to the buffer
    virtual bool WriteBuffer(ST_request* req);

    // load from the buffer
    //virtual bool LoadBuffer(__address_t address, char* data, ST_request* req)
    virtual bool LoadBuffer(ST_request* req, cache_line_t *linecache);

    // return -1 : failed, nothing is free
    // return  n : succeed, n == index
    virtual int IsFreeSlotAvailable();

    virtual bool IsAddressPresent(__address_t address);

    virtual int AddressSlotPosition(__address_t address);

    virtual bool IsSlotLocked(__address_t address, ST_request* req);

    virtual void UpdateMergedRequest(__address_t address, cache_line_t* linecache, ST_request* reqpas);

    virtual void DuplicateRequestQueue(__address_t address, ST_request* reqrev);

    virtual ST_request* GetMergedRequest(__address_t address);

    virtual vector<ST_request*>* GetQueuedRequestVector(__address_t address);

    virtual bool CleanSlot(__address_t address);

protected:

    // return false : no allocation
    // return  treu : allocated
    // index always return the current/allocated index, -1 represent total failure
    virtual bool AllocateSlot(__address_t address, int& index);

    // req should be only write request
    virtual bool UpdateSlot(ST_request* req);
};

//////////////////////////////
//} memory simulator namespace
}

#endif
