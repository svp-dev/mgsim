#ifndef SUSPENDED_REQUEST_QUEUE_H
#define SUSPENDED_REQUEST_QUEUE_H

#include "predef.h"
#include "simcontrol.h"

namespace MemSim{
//{ memory simulator namespace
////////////////////////////////


// the suspended request queue is to serve directories with capability to handle continous request to the same address
// managing suspending, activating and queueing the requests belongs to the same line.
// * it requires "aux", "breserved", "queuehead", and "queuetail" field in the directory line
class SuspendedRequestQueue
{
public:
    // queue constant numbers
    static const unsigned int EOQ;

protected:
    // queue structures
    // request queue
    typedef struct _request_queue_entry
    {
        ST_request* request;
        unsigned int next;          // index of the next entry in a queue
    } request_queue_entry;

    request_queue_entry* m_pReqQueueBuffer;        // The whole request queue/buffer
    unsigned int m_nEmptyReqQueueHead;     // 0xffffffff

    // no needs for a queue implementation in current situation for the lines
    // queue here is just to give a possibility for shuffle later.

    // line queue -- queue of lines that are waiting suspended (mostly suspended on memory request)
    typedef struct _line_queue_entry
    {
        dir_line_t* line;
        //char data[CONST_REQUEST_DATA_SIZE];     // this will store the returned data from the memory
        unsigned int next;
    } line_queue_entry;

    line_queue_entry* m_pServingLineQueueBuffer;    // this queue has the lines to be sent to the network
                                                    // the lines are suspended because outstanding request to the main memory

    unsigned int m_nEmptyLineQueueHead;     // 0xffffffff
    unsigned int m_nActiveLineQueueHead;    // 0xffffffff
    unsigned int m_nActiveLineQueueTail;    // 0xffffffff

    enum AUXSTATE{
    AUXSTATE_NONE,                      // nothing special
    AUXSTATE_LOADING,                   // loading flag, 
                                        // data is being read from the memory
                                        // * loading line does not have a place in the line queue
    AUXSTATE_DEFER                      // auxiliary defer flag
                                        // requests are suspended on the line
                                        // * deferred line must have a position in the line queue
    };

    unsigned int m_nLineQueueSize;
    unsigned int m_nRequestQueueSize;

public:
    SuspendedRequestQueue(unsigned int requestqueuesize = 0x200, unsigned int linequeuesize=0x100) : m_nLineQueueSize(linequeuesize), m_nRequestQueueSize(requestqueuesize)
    {
        // allocate request queue buffer
        m_pReqQueueBuffer = (request_queue_entry*)malloc(requestqueuesize*sizeof(request_queue_entry));

        // initialize request queue entries
        for (unsigned int i=0;i<requestqueuesize;i++)
        {
            m_pReqQueueBuffer[i].request = NULL;
            m_pReqQueueBuffer[i].next = i+1;
        }

        m_pReqQueueBuffer[requestqueuesize-1].next = EOQ;

        // initialize request queue empty queue header
        m_nEmptyReqQueueHead = 0;

        // allocate line queue buffer
        m_pServingLineQueueBuffer = (line_queue_entry*)malloc(linequeuesize*sizeof(line_queue_entry));

        // initialize request queue entries
        for (unsigned int i=0;i<linequeuesize;i++)
        {
            m_pServingLineQueueBuffer[i].line = NULL;
            m_pServingLineQueueBuffer[i].next = i+1;
        }

        m_pServingLineQueueBuffer[linequeuesize-1].next = EOQ;


        // initialize line queue empty queue head and active queuehead
        m_nEmptyLineQueueHead = 0;
        m_nActiveLineQueueHead = EOQ;
        m_nActiveLineQueueTail = EOQ;
    }

    ~SuspendedRequestQueue()
    {
        free(m_pServingLineQueueBuffer);
        free(m_pReqQueueBuffer);
    }

    char GetAuxInitialState()
    {
        return AUXSTATE_NONE;
    }

    const char* AuxStateName(int auxstate, bool shortname=false)
    {
        const char* ret;

        switch (auxstate)
        {
        case AUXSTATE_NONE:
            ret = shortname?"N":"AUXSTATE_NONE";
            break;

        case AUXSTATE_LOADING:
            ret = shortname?"L":"AUXSTATE_LOADING";
            break;

        case AUXSTATE_DEFER:
            ret = shortname?"D":"AUXSTATE_DEFER";
            break;

        default:
            cerr << "***error***: wrong auxillary state for directory" << endl;
            ret = NULL;
            break;
        }

        return ret;
    }

    // the line in the active list
    dir_line_t* GetActiveLine();
//    {
//        if (m_nActiveLineQueueHead == EOQ)
//            return NULL;
//
//        return m_pServingLineQueueBuffer[m_nActiveLineQueueHead].line;
//    }

    ST_request* GetTopActiveRequest();
//    {
//        dir_line_t* activeline = GetActiveLine();
//
//        if (activeline == NULL)
//            return NULL;
//
//        assert(activeline != NULL);
//        assert(activeline->queuehead != EOQ);
//        assert(activeline->aux == AUXSTATE_DEFER);
//
//        return m_pReqQueueBuffer[activeline->queuehead].request;
//    }

    ST_request* PopTopActiveRequest();
//    {
//        if (m_nActiveLineQueueHead == EOQ)
//        {
//            return NULL;
//        }
//
//        // get the first line and request
//        unsigned int activeslot = m_nActiveLineQueueHead;
//        dir_line_t* activeline = m_pServingLineQueueBuffer[activeslot].line;
//
//        assert(activeline != NULL);
//        assert(activeline->queuehead != EOQ);
//
//        ST_request* activereq = m_pReqQueueBuffer[activeline->queuehead].request;
//
//        // the queuehead is already the tail
//        if (activeline->queuehead == activeline->queuetail)
//        {
//            assert(m_pReqQueueBuffer[activeline->queuehead].next == EOQ);
//
//            // remove the whole line from the defer-list
//
//            // first add the slot to the empty queue
//            m_pReqQueueBuffer[activeline->queuehead].next = m_nEmptyReqQueueHead;
//            m_nEmptyReqQueueHead = activeline->queuehead;
//
//            // remove queue head and tail
//            activeline->queuehead = EOQ;
//            activeline->queuetail = EOQ;
//
//            // set the aux state
//            activeline->aux = AUXSTATE_NONE;
//            activeline->breserved = false;
//
//            // change the active line queue head
//            m_nActiveLineQueueHead = m_pServingLineQueueBuffer[activeslot].next;
//
//            // put the slot on the empty list
//            m_pServingLineQueueBuffer[activeslot].next = m_nEmptyLineQueueHead;
//            m_nEmptyLineQueueHead = activeslot;
//        }
//        else    // just remove the current request from the queue
//        {
//            // save the queuehead
//            unsigned int currentreqslot = activeline->queuehead;
//
//            // change the queue head
//            activeline->queuehead = m_pReqQueueBuffer[activeline->queuehead].next;
//
//            // put the removed slot into empty queue
//            m_pReqQueueBuffer[currentreqslot].next = m_nEmptyReqQueueHead;
//            m_nEmptyReqQueueHead = currentreqslot;
//
////            if (bRem)
////            {
////                if (!RemoveLineFromLineQueue(activeline))
////                    assert(false);
////
//////                LOG_VERBOSE_BEGIN(VERBOSE_MOST)
//////                    clog << LOG_HEAD_OUTPUT << "line removed from active line queue" << endl;
//////                LOG_VERBOSE_END
////            }
//        }
//
////        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
////            clog << LOG_HEAD_OUTPUT << "request popped " << FMT_ADDR(activereq->getreqaddress()) << endl;
////            clog << "\t"; print_request(m_pReqCurNET);
////        LOG_VERBOSE_END
//
//        return activereq;
//        
//    }

    // append the line to the line queue
    bool AppendLine2LineQueue(dir_line_t* line);
//    {
//        if (m_nEmptyLineQueueHead == EOQ)
//        {
//            assert(false);
//
//            return false;
//        }
//
//        // acquire the an empty slot
//        unsigned int newslot = m_nEmptyLineQueueHead;
//
//        // remove the line from the empty queue
//        m_nEmptyLineQueueHead = m_pServingLineQueueBuffer[newslot].next;
//
//        // update the current line queue tail
//        if (m_nActiveLineQueueTail != EOQ)
//            m_pServingLineQueueBuffer[m_nActiveLineQueueTail].next = newslot;
//
//        m_nActiveLineQueueTail = newslot;
//
//        // update the new queue tail
//        m_pServingLineQueueBuffer[newslot].line = line;
//        m_pServingLineQueueBuffer[newslot].next = EOQ;
//
//        if (m_nActiveLineQueueHead == EOQ)
//        {
//            m_nActiveLineQueueHead = newslot;
//        }
//
//        return true;
//    }


    // remove the line from the line queue
    // fail if the line doesn't exist on the top
    bool RemoveLineFromLineQueue(dir_line_t* line);
//    {
//        // check whether the line is from the head of the line queue
//        if ((m_nActiveLineQueueHead == EOQ)||(m_pServingLineQueueBuffer[m_nActiveLineQueueHead].line != line))
//            return false;
//
//        // remove the line from the top of the queue
//        // the removed line will not be added to the tail, 
//        // the line will only reenter the line queue when bus-request completes
//        unsigned int topline = m_nActiveLineQueueHead;
//
//        // remove the line from the top slot of the line queue
//        m_nActiveLineQueueHead = m_pServingLineQueueBuffer[topline].next;
//
//        // put the line on the empty queue
//        m_pServingLineQueueBuffer[topline].line = NULL;
//        m_pServingLineQueueBuffer[topline].next = m_nEmptyLineQueueHead;
//        m_nEmptyLineQueueHead = topline;
//
//        return true;
//    }


    // append request to the line
    bool AppendRequest2Line(ST_request* req, dir_line_t* line);
//    {
//        // function returns true if succeed
//        // return false if the buffer is full
//        // otherwise exception
//
//        // this will try to append request to the line associated queue
//        // if the line is not loading yet, this should not happen
//        // since the loading request will update this flag
//        // if the line is already loading, then append the request directly to the queue
//
//        assert(!req->bqueued);
//
//        if (line->aux == AUXSTATE_NONE)
//        {
//            assert(false);
//            return false;
//        }
//        else if ((line->aux == AUXSTATE_LOADING) || (line->aux == AUXSTATE_DEFER))
//        {
//            unsigned int emptyhead = m_nEmptyReqQueueHead;
//
//            // check the line queuehead and queuetail
//            if ( (line->queuehead == EOQ)&&(line->queuetail == EOQ) )
//            {
//                // initialize the queuehead
//                line->queuehead = emptyhead;
//            }
//            else if ( (line->queuehead != EOQ)&&(line->queuetail != EOQ) )
//            {
//
//            }
//            else
//            {
//                assert(false);
//                return false;
//            }
//
//            // acquire the empty queue head
//            // fail, if the buffer is full
//            if (emptyhead == EOQ)
//                return false;
//
//            // get the empty head and alter the empty queue
//            unsigned int secondempty = m_pReqQueueBuffer[emptyhead].next;
//            m_nEmptyReqQueueHead = secondempty;
//
//            // use the head for the request
//            m_pReqQueueBuffer[emptyhead].request = req;
//
//            // set queue property
//            req->bqueued = true;
//
//            // append the request to the queue
//            if (line->queuetail != EOQ)
//                m_pReqQueueBuffer[line->queuetail].next = emptyhead;
//            line->queuetail = emptyhead;
//
//            // finish the tail of the current queue
//            m_pReqQueueBuffer[emptyhead].next = EOQ;
//
//            return true;
//        }
//        else
//        {
//            assert(false);
//            return false;
//        }
//
//    }

    bool ReverselyAppendRequest2Line(ST_request* req, dir_line_t* line);
//    {
//        // function returns true if succeed
//        // return false if the buffer is full
//        // otherwise exception
//
//        // this will try to reversely append request to the line associated queue from the head side
//        // if the line is not loading yet, this should not happen
//        // since the loading request will update this flag
//        // if the line is already loading, then append the request directly to the queue
//
//        assert(req->bqueued);
//
//        if (line->aux == AUXSTATE_NONE)
//        {
//            assert(false);
//            return false;
//        }
//        else if ((line->aux == AUXSTATE_LOADING) || (line->aux == AUXSTATE_DEFER))
//        {
//            unsigned int emptyhead = m_nEmptyReqQueueHead;
//
//            if (emptyhead == EOQ)
//            {
//                // it's full
//                return false;
//            }
//
//            // update empty queue
//            m_nEmptyReqQueueHead = m_pReqQueueBuffer[emptyhead].next;
//
//            // insert the request here 
//            m_pReqQueueBuffer[emptyhead].request = req;
//            m_pReqQueueBuffer[emptyhead].next = line->queuehead;    // no matter it's EOQ or not
//
//            // update line->queue head
//            line->queuehead = emptyhead;
//
//            // update line->queue tail
//            if (line->queuetail == EOQ)
//            {
//                line->queuetail = emptyhead;
//            }
//
//            return true;
//        }
//        else
//        {
//            assert(false);
//            return false;
//        }
//
//    }

    // reactivate line can be 
    // 1. the line just got a reply, so simply changing it to normal state
    // 2. the line got a reply, so put it in active line queue
    bool ReactivateLine(dir_line_t* line);
//    {
//        if ((line == NULL)||(!line->breserved))
//            return false;
//
//        if (line->queuehead != EOQ)
//        {
//            // append the line onto the line queue
//            line->aux = AUXSTATE_DEFER;
//
//            // append the line to the line queue
//            AppendLine2LineQueue(line);
//        }
//        else
//        {
//            line->aux = AUXSTATE_NONE;
//            line->breserved = false;
//        }
//
//        return true;
//    }



    // Normalize line Aux
    bool NormalizeLineAux(dir_line_t* line);
//    {
//        assert(!((line->tokencount < 0) || (line->tokencount > CacheState::GetTotalTokenNum())));
//
//        if (line->tokencount == CacheState::GetTotalTokenNum()) // all the tokens are collected by the directory now
//        {
//            if ((line->aux == AUXSTATE_DEFER)||(line->aux == AUXSTATE_LOADING))
//            {
//                //assert(line->queuehead!=NULL);
//                //assert(line->queuetail!=NULL);
//
//                //line->state = DRRESERVED;    //JNEW
//                line->breserved = true;         // JNEW
//
////                LOG_VERBOSE_BEGIN(VERBOSE_STATE)
////                    clog << LOG_HEAD_OUTPUT << "line loading or deferred, reserved flag is set." << endl;  // JNEW
////                LOG_VERBOSE_END
//            }
//            else    //AUXSTATE_NONE
//            {
//                assert(line->queuehead==EOQ);
//                assert(line->queuetail==EOQ);
//                assert(line->breserved == false);   // JNEW
//            }
//        }
//
//        if ( (line->aux != AUXSTATE_DEFER) && (line->aux != AUXSTATE_LOADING) )
//        {
//            // time to reset the reserved flag 
//            line->breserved = false;
////            LOG_VERBOSE_BEGIN(VERBOSE_STATE)
////                clog << LOG_HEAD_OUTPUT << "line reset to non-reserved state, incoming queue will not be automatically queued." << endl;  // JNEW
////            LOG_VERBOSE_END
//        }
//
//        return true;
//    }

    bool HasOutstandingRequest(dir_line_t* line);
//    {
//        if (line->breserved)
//        {
//            assert((line->aux == AUXSTATE_DEFER)||(line->aux == AUXSTATE_LOADING));
//            return true;
//        }
//        else
//        {
//            assert(line->queuehead == EOQ);
//            assert(line->queuetail == EOQ);
//
//            return false;
//        }
//    }


    bool StartLoading(dir_line_t* line);
//    {
//        assert(!line->breserved);
//        assert(line->aux == AUXSTATE_NONE);
//        assert(line->queuehead == EOQ);
//
//        line->breserved = true;
//        line->aux = AUXSTATE_LOADING;
//
//        return true;
//    }

    bool IsRequestQueueEmpty(dir_line_t* line);
//    {
//        if (line->queuehead == EOQ)
//            return true;
//
//        return false;
//    }

    bool IsActiveLineQueueEmpty();
//    {
//        if (m_nActiveLineQueueHead == EOQ)
//            return true;
//
//        return false;
//    }

    // ** for a certain line, the function should be used continuously.
    // return true, if there's a next request on the queue.
    // return false, if end of queue is reached.
    bool GetNextReq(dir_line_t& line, ST_request* &req, bool restart = false);
//    {
//        static unsigned int qpos = EOQ;
//
//        if (restart)
//        {
//            qpos = line.queuehead;
//        }
//
//        if (qpos == EOQ)
//        {
//            req = NULL;
//            return false;
//        }
//
//        req = m_pReqQueueBuffer[qpos].request;
//        qpos = m_pReqQueueBuffer[qpos].next;
//
//        return true;
//    }

    // ** the function should be called continously for a directory
    // return true, if there's a next line in the active line queue
    // return false, if end of the queue is reached.
    bool GetNextActiveLine(dir_line_t* &line, bool restart = false);
//    {
//        static unsigned int qpos = EOQ;
//
//        if (m_nActiveLineQueueHead == EOQ)
//        {
//            line = NULL;
//            return false;
//        }
//
//        if (restart)
//            qpos = m_nActiveLineQueueHead;
//
//        if (qpos == EOQ)
//        {
//            line = NULL;
//            return false;
//        }
//
//        line = m_pServingLineQueueBuffer[qpos].line;
//        qpos = m_pServingLineQueueBuffer[qpos].next;
//
//        return true;
//    }
};

//////////////////////////////
////} memory simulator namespace
}
#endif
