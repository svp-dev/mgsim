#ifndef _MEM_SCHEDULE_PIPELINE_H
#define _MEM_SCHEDULE_PIPELINE_H

#include "predef.h"

namespace MemSim
{

#define MSP_LRP_NONE        -255    // LAST REQUEST POSITION: regarded as no previous requests
#define MSP_LRP_TH_CLOSE    -20     // threashold to close row

// the requests are scheduled at their return time. 
// the requests moved out of the pipeline can be returned immediately
class MemorySchedulePipeline
{
    std::vector<ST_request*> m_pipeline;
    unsigned int             m_nHead;            // head of the buffer
    unsigned int             m_nPointer;         // pointing to the next empty slot to the last scheduled request
    int                      m_nLastReqPosition; // last request position, 
                                                 // it is a relative position to the head of the buffer
                                                 // it can be negtive 
public:
    MemorySchedulePipeline(unsigned int size)
        : m_pipeline(size, NULL),
        m_nHead(0),
        m_nPointer(0),
        m_nLastReqPosition(0)
    {
    }

    bool ScheduleNext(ST_request * req, unsigned int gap, unsigned int delay)
    {
        assert(delay + 1 < m_pipeline.size());
       
        if (m_nLastReqPosition == MSP_LRP_NONE)
        {
            m_nLastReqPosition = gap;
        }
        else
        {
            // Check pipeline size
            if (m_nLastReqPosition + (int)(gap + delay) >= (int)m_pipeline.size() - 1)
            {
                return false;
            }
            m_nLastReqPosition = std::max(m_nLastReqPosition + (int)gap, 0);
        }
        
        unsigned int newpos = m_nHead + m_nLastReqPosition + delay;
        m_pipeline[newpos % m_pipeline.size()] = req;
        m_nPointer = (newpos + 1) % m_pipeline.size();
        return true;
    }

    // eventid : 0 - nothing
    //           1 - row close
    ST_request* AdvancePipeline(unsigned int& eventid)
    {
        ST_request* req = NULL;
        if (m_nPointer != m_nHead)
        {
            req = m_pipeline[m_nHead];
            m_pipeline[m_nHead] = NULL;
            m_nHead = (m_nHead + 1) % m_pipeline.size();
        }
        
        eventid = 0;
        if (m_nLastReqPosition != MSP_LRP_NONE)
        {
            m_nLastReqPosition--;
            if (m_nLastReqPosition < MSP_LRP_TH_CLOSE)
            {
                m_nLastReqPosition = MSP_LRP_NONE;
                eventid = 1;
            }
        }
   
        return req; 
    }
};


}

#endif
