#include "suspendedrequestqueue.h"

namespace MemSim
{

const unsigned int SuspendedRequestQueue::EOQ = 0xffffffff;

// the line in the active list
dir_line_t* SuspendedRequestQueue::GetActiveLine()
{
    return (m_nActiveLineQueueHead != EOQ)
        ? m_pServingLineQueueBuffer[m_nActiveLineQueueHead].line
        : NULL;
}

ST_request* SuspendedRequestQueue::GetTopActiveRequest()
{
    dir_line_t* activeline = GetActiveLine();

    if (activeline == NULL)
        return NULL;

    assert(activeline != NULL);
    assert(activeline->queuehead != EOQ);
    assert(activeline->aux == AUXSTATE_DEFER);

    return m_pReqQueueBuffer[activeline->queuehead].request;
}

ST_request* SuspendedRequestQueue::PopTopActiveRequest()
{
    if (m_nActiveLineQueueHead == EOQ)
    {
        return NULL;
    }

    // get the first line and request
    unsigned int activeslot = m_nActiveLineQueueHead;
    dir_line_t* activeline = m_pServingLineQueueBuffer[activeslot].line;

    assert(activeline != NULL);
    assert(activeline->queuehead != EOQ);

    ST_request* activereq = m_pReqQueueBuffer[activeline->queuehead].request;

    // the queuehead is already the tail
    if (activeline->queuehead == activeline->queuetail)
    {
        assert(m_pReqQueueBuffer[activeline->queuehead].next == EOQ);

        // remove the whole line from the defer-list

        // first add the slot to the empty queue
        m_pReqQueueBuffer[activeline->queuehead].next = m_nEmptyReqQueueHead;
        m_nEmptyReqQueueHead = activeline->queuehead;

        // remove queue head and tail
        activeline->queuehead = EOQ;
        activeline->queuetail = EOQ;

        // set the aux state
        activeline->aux = AUXSTATE_NONE;
        activeline->breserved = false;

        // change the active line queue head
        m_nActiveLineQueueHead = m_pServingLineQueueBuffer[activeslot].next;

        // put the slot on the empty list
        m_pServingLineQueueBuffer[activeslot].next = m_nEmptyLineQueueHead;
        m_nEmptyLineQueueHead = activeslot;
    }
    else    // just remove the current request from the queue
    {
        // save the queuehead
        unsigned int currentreqslot = activeline->queuehead;

        // change the queue head
        activeline->queuehead = m_pReqQueueBuffer[activeline->queuehead].next;

        // put the removed slot into empty queue
        m_pReqQueueBuffer[currentreqslot].next = m_nEmptyReqQueueHead;
        m_nEmptyReqQueueHead = currentreqslot;
    }
    return activereq;
    
}

// append the line to the line queue
bool SuspendedRequestQueue::AppendLine2LineQueue(dir_line_t* line)
{
    assert (m_nEmptyLineQueueHead != EOQ);

    // acquire the an empty slot
    unsigned int newslot = m_nEmptyLineQueueHead;

    // remove the line from the empty queue
    m_nEmptyLineQueueHead = m_pServingLineQueueBuffer[newslot].next;

    // update the current line queue tail
    if (m_nActiveLineQueueTail != EOQ)
        m_pServingLineQueueBuffer[m_nActiveLineQueueTail].next = newslot;

    m_nActiveLineQueueTail = newslot;

    // update the new queue tail
    m_pServingLineQueueBuffer[newslot].line = line;
    m_pServingLineQueueBuffer[newslot].next = EOQ;

    if (m_nActiveLineQueueHead == EOQ)
    {
        m_nActiveLineQueueHead = newslot;
    }

    return true;
}


// remove the line from the line queue
// fail if the line doesn't exist on the top
bool SuspendedRequestQueue::RemoveLineFromLineQueue(dir_line_t* line)
{
    // check whether the line is from the head of the line queue
    if ((m_nActiveLineQueueHead == EOQ)||(m_pServingLineQueueBuffer[m_nActiveLineQueueHead].line != line))
        return false;

    // remove the line from the top of the queue
    // the removed line will not be added to the tail, 
    // the line will only reenter the line queue when bus-request completes
    unsigned int topline = m_nActiveLineQueueHead;

    // remove the line from the top slot of the line queue
    m_nActiveLineQueueHead = m_pServingLineQueueBuffer[topline].next;

    // put the line on the empty queue
    m_pServingLineQueueBuffer[topline].line = NULL;
    m_pServingLineQueueBuffer[topline].next = m_nEmptyLineQueueHead;
    m_nEmptyLineQueueHead = topline;

    return true;
}


// append request to the line
bool SuspendedRequestQueue::AppendRequest2Line(ST_request* req, dir_line_t* line)
{
    // function returns true if succeed
    // return false if the buffer is full
    // otherwise exception

    // this will try to append request to the line associated queue
    // if the line is not loading yet, this should not happen
    // since the loading request will update this flag
    // if the line is already loading, then append the request directly to the queue
    assert(!req->bqueued);
    assert(line->aux != AUXSTATE_NONE);
    assert(line->aux == AUXSTATE_LOADING || line->aux == AUXSTATE_DEFER);

    unsigned int emptyhead = m_nEmptyReqQueueHead;

    // check the line queuehead and queuetail
    if (line->queuehead == EOQ && line->queuetail == EOQ)
    {
        // initialize the queuehead
        line->queuehead = emptyhead;
    }

    // acquire the empty queue head
    // fail, if the buffer is full
    if (emptyhead == EOQ)
        return false;

    // get the empty head and alter the empty queue
    unsigned int secondempty = m_pReqQueueBuffer[emptyhead].next;
    m_nEmptyReqQueueHead = secondempty;

    // use the head for the request
    m_pReqQueueBuffer[emptyhead].request = req;

    // set queue property
    req->bqueued = true;

    // append the request to the queue
    if (line->queuetail != EOQ)
        m_pReqQueueBuffer[line->queuetail].next = emptyhead;
    line->queuetail = emptyhead;

    // finish the tail of the current queue
    m_pReqQueueBuffer[emptyhead].next = EOQ;
    return true;
}

bool SuspendedRequestQueue::ReverselyAppendRequest2Line(ST_request* req, dir_line_t* line)
{
    // function returns true if succeed
    // return false if the buffer is full
    // otherwise exception

    // this will try to reversely append request to the line associated queue from the head side
    // if the line is not loading yet, this should not happen
    // since the loading request will update this flag
    // if the line is already loading, then append the request directly to the queue
    assert(req->bqueued);
    assert (line->aux != AUXSTATE_NONE);

    assert ((line->aux == AUXSTATE_LOADING) || (line->aux == AUXSTATE_DEFER));
    {
        unsigned int emptyhead = m_nEmptyReqQueueHead;

        if (emptyhead == EOQ)
        {
            // it's full
            return false;
        }

        // update empty queue
        m_nEmptyReqQueueHead = m_pReqQueueBuffer[emptyhead].next;

        // insert the request here 
        m_pReqQueueBuffer[emptyhead].request = req;
        m_pReqQueueBuffer[emptyhead].next = line->queuehead;    // no matter it's EOQ or not

        // update line->queue head
        line->queuehead = emptyhead;

        // update line->queue tail
        if (line->queuetail == EOQ)
        {
            line->queuetail = emptyhead;
        }
        return true;
    }
}

// reactivate line can be 
// 1. the line just got a reply, so simply changing it to normal state
// 2. the line got a reply, so put it in active line queue
bool SuspendedRequestQueue::ReactivateLine(dir_line_t* line)
{
    if (line == NULL || !line->breserved)
        return false;

    if (line->queuehead != EOQ)
    {
        // append the line onto the line queue
        line->aux = AUXSTATE_DEFER;

        // append the line to the line queue
        AppendLine2LineQueue(line);
    }
    else
    {
        line->aux = AUXSTATE_NONE;
        line->breserved = false;
    }

    return true;
}

// Normalize line Aux
bool SuspendedRequestQueue::NormalizeLineAux(dir_line_t* line)
{
    assert(line->tokencount <= CacheState::GetTotalTokenNum());

    if (line->tokencount == CacheState::GetTotalTokenNum())
    {
        // All the tokens are collected by the directory now
        if (line->aux == AUXSTATE_DEFER || line->aux == AUXSTATE_LOADING)
        {
            line->breserved = true;         // JNEW
        }
        else    //AUXSTATE_NONE
        {
            assert(line->queuehead==EOQ);
            assert(line->queuetail==EOQ);
            assert(line->breserved == false);   // JNEW
        }
    }

    if (line->aux != AUXSTATE_DEFER && line->aux != AUXSTATE_LOADING)
    {
        // time to reset the reserved flag 
        line->breserved = false;
    }
    return true;
}

bool SuspendedRequestQueue::HasOutstandingRequest(dir_line_t* line)
{
    if (line->breserved)
    {
        assert(line->aux == AUXSTATE_DEFER || line->aux == AUXSTATE_LOADING);
        return true;
    }
    assert(line->queuehead == EOQ);
    assert(line->queuetail == EOQ);
    return false;
}


bool SuspendedRequestQueue::StartLoading(dir_line_t* line)
{
    assert(!line->breserved);
    assert(line->aux == AUXSTATE_NONE);
    assert(line->queuehead == EOQ);

    line->breserved = true;
    line->aux = AUXSTATE_LOADING;

    return true;
}

bool SuspendedRequestQueue::IsRequestQueueEmpty(dir_line_t* line)
{
    return line->queuehead == EOQ;
}

}
