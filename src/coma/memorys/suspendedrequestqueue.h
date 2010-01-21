#ifndef SUSPENDED_REQUEST_QUEUE_H
#define SUSPENDED_REQUEST_QUEUE_H

#include "predef.h"
#include "simcontrol.h"

namespace MemSim
{

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

    // the line in the active list
    dir_line_t* GetActiveLine();
    ST_request* GetTopActiveRequest();
    ST_request* PopTopActiveRequest();

    // append the line to the line queue
    bool AppendLine2LineQueue(dir_line_t* line);

    // remove the line from the line queue
    // fail if the line doesn't exist on the top
    bool RemoveLineFromLineQueue(dir_line_t* line);

    // append request to the line
    bool AppendRequest2Line(ST_request* req, dir_line_t* line);

    bool ReverselyAppendRequest2Line(ST_request* req, dir_line_t* line);

    // reactivate line can be 
    // 1. the line just got a reply, so simply changing it to normal state
    // 2. the line got a reply, so put it in active line queue
    bool ReactivateLine(dir_line_t* line);

    // Normalize line Aux
    bool NormalizeLineAux(dir_line_t* line);
    bool HasOutstandingRequest(dir_line_t* line);
    bool StartLoading(dir_line_t* line);
    bool IsRequestQueueEmpty(dir_line_t* line);
};

}
#endif
