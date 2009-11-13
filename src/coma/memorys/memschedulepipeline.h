#ifndef _MEM_SCHEDULE_PIPELINE_H
#define _MEM_SCHEDULE_PIPELINE_H

#include "predef.h"


namespace MemSim{
// memomry sim namespace


#define MSP_LRP_NONE        -255    // LAST REQUEST POSITION: regarded as no previous requests
#define MSP_LRP_TH_CLOSE    -20     // threashold to close row

// the requests are scheduled at their return time. 
// the requests moved out of the pipeline can be returned immediately

class MemorySchedulePipeline : public SimObj
{
    unsigned int    m_nSize;

    ST_request**    m_pRequestPipeline;

    int             m_nHead;    // head of the buffer
    int             m_nPointer; // pointing to the next empty slot to the last scheduled request

    int             m_nLastReqPosition; // last request position, 
                                        // it is a relative position to the head of the buffer
                                        // it can be negtive 

public:
    MemorySchedulePipeline(unsigned int maxsize)
    {
        m_nSize = maxsize;
        Initialize();
        m_nLastReqPosition = 0;
    }

    ~MemorySchedulePipeline()
    {
        free(m_pRequestPipeline);
    }

    bool ScheduleNext(ST_request * req, unsigned int gap, unsigned int delay)
    {
      assert (delay < (m_nSize-1));
       
//LOG_VERBOSE_BEGIN(VERBOSE_STATE)
//clog << LOGN_HEAD_OUTPUT << "SCHEDULE REQUEST : " << endl;
//print_request(req);
//LOG_VERBOSE_END

        int newpos;
        int newlast;
        if (m_nLastReqPosition == MSP_LRP_NONE)
        {
            assert(m_nPointer == m_nHead);
            newpos = (m_nHead + gap + delay)%m_nSize;
            assert(gap + delay < (m_nSize - 1));


            newlast = gap;

            // more assertions
        }
        else
        {
            // check pipeline size
//            if ( (newpos-m_nHead)%m_nSize < (newpos-m_nPointer)%m_nSize 
	  if ((m_nLastReqPosition + (int)gap + (int)delay) >= ((int)m_nSize - 1))
            {
//                cerr << "warning: insufficient pipeline buffer in MSP." << endl;
                return false;
            }

            if ((m_nLastReqPosition + (int)gap) < 0)
            {
                newpos = (m_nHead + delay)%m_nSize;
                newlast = 0;

//                assert( (newpos-m_nHead)%m_nSize > (newpos-m_nPointer)%m_nSize );
            }
            else
            {
                newpos = (m_nHead + m_nLastReqPosition + gap + delay)%m_nSize;
                newlast = m_nLastReqPosition + gap;

                assert((gap+delay) < (m_nSize -1));

                // more assertion here

            }
        }

        m_pRequestPipeline[newpos] = req;

        m_nPointer = (newpos + 1)%m_nSize;

//LOG_VERBOSE_BEGIN(VERBOSE_STATE)
//clog << LOGN_HEAD_OUTPUT << "SCHEDULE REQUEST : " << endl;
//clog << sc_time_stamp() <<  "  " << gap << " " << delay << " " << m_nLastReqPosition << " " << m_nHead << " " << newpos << " " << m_nPointer << endl;
//LOG_VERBOSE_END
//
//        cout << sc_time_stamp()<<  "  " << gap << " " << delay << " " << m_nLastReqPosition << " " << m_nHead << " " << newpos << " " << m_nPointer << endl;
        assert(m_nPointer != m_nHead);

        m_nLastReqPosition = newlast;

        return true;
    }

    // eventid :    0 - nothing
    //              1 - row close
    ST_request* AdvancePipeline(unsigned int& eventid)
    {
        eventid = 0;

        if (m_nPointer == m_nHead)
        {
            if (m_nLastReqPosition != MSP_LRP_NONE)
            {
                m_nLastReqPosition--;

                if (m_nLastReqPosition < MSP_LRP_TH_CLOSE)
                {
                    m_nLastReqPosition = MSP_LRP_NONE;
                    eventid = 1;
                }
            }
 
            return NULL;
        }

        ST_request* req = m_pRequestPipeline[m_nHead];
        m_pRequestPipeline[m_nHead] = NULL;
        m_nHead = (m_nHead+1)%m_nSize;

        if (m_nLastReqPosition != MSP_LRP_NONE)
        {
            m_nLastReqPosition--;

            if (m_nLastReqPosition < MSP_LRP_TH_CLOSE)
            {
                m_nLastReqPosition = MSP_LRP_NONE;
                eventid = 1;
            }
        }
   
//        cout << " == " << " " << m_nHead << " " << m_nPointer << endl;
        return req; 
    }

private:
    void Initialize()
    {
        m_nPointer = 0;
        m_nHead = 0;

        m_pRequestPipeline = (ST_request**)malloc(m_nSize * sizeof(ST_request*));
        for (unsigned int i=0;i<m_nSize;i++)
            m_pRequestPipeline[i] = NULL;
    }
};


}

#endif
