#include "cachel2tok.h"

#include "../simlink/linkmgs.h"

using namespace MemSim;

#ifdef TOKEN_COHERENCE

//#define DEBUG_TEST_XXX {cache_line_t* xtempline = LocateLineEx(0x112280);  if (xtempline!=NULL)  cout << hex << (void*)&(xtempline->tokencount)<< (void*)&(xtempline->state)  << (void*)&(xtempline->pending) << "   " << xtempline->tokencount << " " << xtempline->state << " " << xtempline->pending << xtempline->invalidated << endl;}

//#define DEBUG_TEST_XXX {}

namespace MemSim{
vector<CacheL2TOK*>   CacheL2TOK::s_vecPtrCaches;

const unsigned int CacheL2TOK::EOQ = 0xffffffff;
const unsigned int CacheL2TOK::QueueBufferSize = 0x100;

unsigned int CacheL2TOK::s_nGlobalFIFOUpperMargin = 1;    // UPPER Margin
//const unsigned int CacheL2TOK::s_nGlobalFIFOLowerMargin = 3;    // LOWER Margin
const unsigned int CacheL2TOK::s_nGlobalFIFOLowerMargin = 0x100;    // LOWER Margin
}


void CacheL2TOK::InitializeCacheLines()
{
    CacheST::InitializeCacheLines();

    unsigned int i;

    // allocate lines
    for (i=0;i<m_nSet;i++)
    {
        for (unsigned int j=0;j<m_nAssociativity;j++)
        {
            m_pSet[i].lines[j].queuehead = EOQ;
            m_pSet[i].lines[j].queuetail = EOQ;
        }
    }

    // allocate queue buffer
    m_pQueueBuffer = (queue_entry*)malloc(QueueBufferSize*sizeof(queue_entry));

    // initialize queue entries
    for (i=0;i<QueueBufferSize;i++)
    {
        m_pQueueBuffer[i].request = NULL;
        m_pQueueBuffer[i].next = i+1;
    }

    m_pQueueBuffer[QueueBufferSize-1].next = EOQ;

    // initialize empty queue header
    m_nEmptyQueueHead = 0;

    // initializing queue register
    //m_pQueueRegister = NULL;

    // initializing processing queue request flag
    //m_bProcessingQueueRequest = false;

#ifdef UNIVERSAL_ACTIVE_QUEUE_MODE
    m_nActiveQueueHead = EOQ;
    m_nActiveQueueTail = EOQ;
#endif

#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
    m_pReqResidue = NULL;
#endif

    //// initialize the invalidation matching buffer
    //m_pInvalidationMatchingBuffer = new InvalidationMatchingBufferEntry_t[s_nInvalidationMatchingBufferSize];
    //for (i=0;i<s_nInvalidationMatchingBufferSize;i++)
    //{
    //    m_pInvalidationMatchingBuffer[i].bvalid = false;
    //    for (unsigned int j=0;j<s_nInvalidationMatchingBufferSize;j++)
    //        m_pInvalidationMatchingBuffer[i].lruarray[j] = false;
    //}

    //// initialize the victim line matching buffer
    //m_pVictimLineMatchingBuffer = new VictimLineMatchingBufferEntry_t[s_nVictimLineMatchingBufferSize];
    //for (i=0;i<s_nVictimLineMatchingBufferSize;i++)
    //{
    //    m_pVictimLineMatchingBuffer[i].bvalid = false;
    //    for (unsigned int j=0;j<s_nVictimLineMatchingBufferSize;j++)
    //        m_pVictimLineMatchingBuffer[i].lruarray[j] = false;
    //}
}

//////////////////////////////////////////////////////////////////////////
// initiative requesting handling

// process an initiative request from processor side
void CacheL2TOK::ProcessInitiative()
{
LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
    clog << LOG_HEAD_OUTPUT << "process request initiatively " << FMT_ADDR(m_pReqCurINI->getlineaddress()) << endl;
    clog << "\t"; print_request(m_pReqCurINI);
LOG_VERBOSE_END

    // handle request
    // the requests arrive here should be only local read and local write
    switch(m_pReqCurINI->type)
    {
    case MemoryState::REQUEST_READ:

#ifdef OPTIMIZATION_VICTIM_BUFFER
        // $$$ optimization for victim buffer $$$
        // check the victim buffer first
        if (OnLocalReadVictimBuffer(m_pReqCurINI))
            break;
#endif

        OnLocalRead(m_pReqCurINI);
        break;
    case MemoryState::REQUEST_WRITE:
        OnLocalWrite(m_pReqCurINI);
#ifdef OPTIMIZATION_VICTIM_BUFFER
        // $$$ optimization for victim buffer $$$
        // remove the item from victim buffer on local write
        LocalWriteExtra(m_pReqCurINI->getlineaddress());
#endif
        break;

#ifdef OPTIMIZATION_READ_REPLY_FLUSHING
    case MemoryState::REQUEST_READ_REPLY:
        OnReadReply(m_pReqCurINI);
        break;
#endif

    default:
	cout << LOG_HEAD_OUTPUT ;
	cout << m_pReqCurINI->type << endl;
	cout << m_pReqCurINI->getreqaddress() << endl;
    //    abort();
        break;
    }
}

#ifdef OPTIMIZATION_VICTIM_BUFFER
// $$$ optimization for victim buffer $$$
// Load from the victim buffer
bool CacheL2TOK::OnLocalReadVictimBuffer(ST_request* req)
{
    // CHKS: double check the validness of the victim buffer in relation to the cache content

    int index;
    char *data;

    if (m_fabEvictedLine.FindBufferItem(req->getlineaddress(), index, data))
    {
        // get the data
        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "LR address " << FMT_ADDR(req->getreqaddress()) << " hit at set victim buffer" << endl;
        LOG_VERBOSE_END

        // update request 
	  assert (data != NULL);

        memcpy(req->data, data, g_nCacheLineSize);

        req->type = MemoryState::REQUEST_READ_REPLY;

#ifdef OPTIMIZATION_READ_REPLY_FLUSHING
        // $$$ read reply flushing $$$
        ReadReplyFlushing(req);
#endif

        LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "LR done address " << FMT_ADDR(req->getreqaddress()) << ", " << FMT_DTA(req->data[0]) << "returned." << endl;
        LOG_VERBOSE_END

        // save request 
        InsertSlaveReturnRequest(true, req);

        // return the reply to the processor side
        return true;
    }

    return false;
}
#endif

bool CacheL2TOK::SendAsNodeINI(ST_request* req)
{
    // reset the queued property
    req->bqueued = false;

    // send request to memory
    if (!RequestNetwork(req))
    {
        LOG_VERBOSE_BEGIN(VERBOSE_ALL)
            clog << LOG_HEAD_OUTPUT << "request transaction sent failed, network node busy" << endl;
        LOG_VERBOSE_END

        return false;
    }

    // change the state to free

#ifdef MEM_MODULE_STATISTICS
    // statistics
    if (m_pmmStatSendNodeINI != NULL)
    {
        m_pStatCurSendNodeINI = new ST_request(req);
        m_pStatCurSendNodeINI->ref = (unsigned long*)req;
    }
#endif

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "request transaction sent by network node" << endl;
        print_request(req);
    LOG_VERBOSE_END

    return true;
}


bool CacheL2TOK::SendAsSlaveINI(ST_request* req)
{
    // reset the queued property
    req->bqueued = false;

    // send reply transaction
    if (!channel_fifo_slave.nb_write(req))
    {
        LOG_VERBOSE_BEGIN(VERBOSE_ALL)
            clog << LOG_HEAD_OUTPUT << "reply transaction sent failed, bus busy" << endl;
        LOG_VERBOSE_END

        return false;
    }

    // succeed in sending to slave interface
    if (req->type == REQUEST_READ_REPLY)
    {
        m_fabInvalidation.RemoveBufferItem(req->getlineaddress());
        LOG_VERBOSE_BEGIN(VERBOSE_ALL)
            clog << LOG_HEAD_OUTPUT << "IB/WR address is removed from the invalidation matching buffer" << endl;
        LOG_VERBOSE_END
    }
    else if (req->type == REQUEST_WRITE_REPLY)
    {
#ifndef MEM_CACHE_LEVEL_ONE_SNOOP
        char* data;
        int index;
        if (!m_fabInvalidation.FindBufferItem(req->getlineaddress(), index, data))
        {
            req->bbackinv = true;
            LOG_VERBOSE_BEGIN(VERBOSE_ALL)
                clog << LOG_HEAD_OUTPUT << "WR address is going to be broadcasted and backward invalidate other caches." << endl;
            LOG_VERBOSE_END
        }
#endif
    }

    // change the state to free

    // statistics
    //if (m_pReqCurINIasSlave->type == REQUEST_READ_REPLY_X)
#ifdef MEM_MODULE_STATISTICS
        m_pStatTestINI = req;
#endif

    LOG_VERBOSE_BEGIN(VERBOSE_ALL)
        clog << LOG_HEAD_OUTPUT << "reply transaction sent to the processor/L1 cache" << endl;
    LOG_VERBOSE_END

    return true;
}


void CacheL2TOK::SendFromINI()
{
    AutoFillSlaveReturnRequest(true);
    bool sentnodedb = (m_pReqCurINIasNodeDB == NULL);
    bool sentnode = (m_pReqCurINIasNode == NULL);
    bool sentslave = (m_pReqCurINIasSlaveX == NULL);

    if ( (m_pReqCurINIasNodeDB==NULL)&&(m_pReqCurINIasNode==NULL)&&(m_pReqCurINIasSlaveX==NULL) )
    {
        assert(m_nStateINI == STATE_INI_PROCESSING);
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "Cache INI skip to next cycle, pipeline done for the cycle" << endl;
        LOG_VERBOSE_END

        return;
    }

    if (m_pReqCurINIasNodeDB != NULL)
    {
        if (!SendAsNodeINI(m_pReqCurINIasNodeDB))
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "(DB) sent failed, try next cycle." << endl;
            LOG_VERBOSE_END

            sentnodedb = false;
        }
        else
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "(DB) sent succeeded by nextwork node." << endl;
            LOG_VERBOSE_END

            sentnodedb = true;
            m_pReqCurINIasNodeDB = NULL;
        }
    }
    else if (m_pReqCurINIasNode != NULL)
    {
        if (!SendAsNodeINI(m_pReqCurINIasNode))
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "node request sent failed, try next cycle." << endl;
            LOG_VERBOSE_END

            sentnode = false;
        }
        else
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "sent succeeded by nextwork node." << endl;
            LOG_VERBOSE_END

            sentnode = true;
            m_pReqCurINIasNode = NULL;
        }
    }

    if (m_pReqCurINIasSlaveX != NULL)
    {
        if (!SendAsSlaveINI(m_pReqCurINIasSlaveX))
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "slave request sent failed, try next cycle." << endl;
            LOG_VERBOSE_END

            sentslave = false;
        }
        else
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "slave request sent succeeded." << endl;
            LOG_VERBOSE_END

            sentslave = true;
            m_pReqCurINIasSlaveX = NULL;
        }
    }

    if (sentnodedb&&sentnode&&sentslave)
    {
        m_nStateINI = STATE_INI_PROCESSING;
    }
    else
    {
        m_nStateINI = STATE_INI_RETRY;
    }
}


ST_request* CacheL2TOK::FetchRequestINI()
{
    ST_request* ret = NULL;

    CheckThreshold4GlobalFIFO();

    // check whether the buffer has priority
    if (m_bBufferPriority)
    {

        // the queue shouldn't be empty when buffer has priority
      assert (!GlobalFIFOEmpty());

        // pop the request from the queue 
        ret = PoPQueuedRequest();
        if (ret == NULL)
        {
            cerr << ERR_HEAD_OUTPUT << "should not fail in getting the request (1)" << endl;
            abort();
            return NULL;
        }

        return ret;
    }

    // from here the buffer doesnt' have priority anymore

    // check input buffer
    // check whether there are any available requests
    if (m_pfifoReqIn->num_available_fast() <= 0)
    {
      if (GlobalFIFOEmpty())
            return NULL;

        // if no request then check the 
        ret = PoPQueuedRequest();
        if (ret == NULL)
        {
            cerr << ERR_HEAD_OUTPUT << "should not fail in getting the request (2)" << endl;
            abort();
            return NULL;
        }

        return ret;
    }
    else if (!m_pfifoReqIn->nb_read(ret))      // save the request to m_pReqCurINI
    {
        cerr << ERR_HEAD_OUTPUT << "should not get failed in this nb_read request" << endl;
        abort();
        return NULL;
    }

#ifdef MEM_MODULE_STATISTICS
    // stat
    m_nStatReqNo++;
#endif

    if (DoesFIFOSeekSameLine(ret))
    {
   	if (ret->type == 4) cout << hex << "f0 " << ret->getreqaddress() << endl;
        InsertRequest2GlobalFIFO(ret);
        ret = NULL;
    }

    return ret;
}

void CacheL2TOK::CleansingPipelineINI(ST_request* req)
{
    //////////////////////////////////////////////////////////////////////////
    // two passes:
    // first pass, will process the pipeline registers reversely considering only the previously queued requests
    // they will be removed from the pipeline, and pushed reversely from the front into the queue
    // second pass, will process the pipeline registers check all the previously non-queued requests
    // they will be removed from the pipeline, and pushed directly from the back as the order they are in the pipeline

#ifdef MEM_MODULE_STATISTICS
    // statistics
    m_bStatCleansingINI = true;
#endif

    __address_t lineaddr = req->getlineaddress();

    // temp request vector
    vector<ST_request*> vecreq;

    // check from the back size if anyone has the same line addr
    pipeline_t::lst_t::const_iterator iter;

    //////////////////////////////////////////////////////////////////////////
    // dump all the stuff to the vector
    // the checking is carried out from the back to front (tail to head)
    m_pPipelineINI->copy(vecreq);

    //////////////////////////////////////////////////////////////////////////
    // first pass

    // queued requests: reversely check every register
    for (int i=vecreq.size()-1;i>=0;i--)
    {
        ST_request* reqtemp = vecreq[i];

        if ((reqtemp != NULL)&&(reqtemp->bqueued)&&(reqtemp->getlineaddress() == lineaddr))
        {
            // reversely push back from the front side to the global queue 
            InsertRequest2GlobalFIFO(reqtemp);

            // fill the place for the regiser with NULL
            vecreq[i] = NULL;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // second pass
    // non-queued requests
    for (unsigned int i=0;i<vecreq.size();i++)
    {
        ST_request* reqtemp = vecreq[i];

        if ((reqtemp != NULL)&&(!reqtemp->bqueued)&&(reqtemp->getlineaddress() == lineaddr))
        {
            // this is unlikely to happen for now
            // CHKS: might need some other check since there are already requests put in to queue in the first pass ...
            //assert(!DoesFIFOSeekSameLine(reqtemp));

            // push back from the bottom to the global queue
            InsertRequest2GlobalFIFO(reqtemp);

            // fill the place for the regiser with NULL
            vecreq[i] = NULL;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // dump back to the pipeline 
    m_pPipelineINI->reset(vecreq);

#ifdef OPTIMIZATION_SORT_GLOBAL_FIFO_ON_RPUSH
    // $$$ optimization to sort $$$
    SortGlobalFIFOAfterReversePush(req);
#endif
}

// cleansing pipeline and insert request into queue
void CacheL2TOK::CleansingAndInsert(ST_request* req)
{
   if (req->type == 4) cout << hex << "c0 " << req->getreqaddress() << endl;
    // cleansing the pipeline
    CleansingPipelineINI(req);

    // put the current request in the global queue
    if (!InsertRequest2GlobalFIFO(req))
    {
        // never fails
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "request failed to inserted into Global buffer, cache STALL!" << endl;
        LOG_VERBOSE_END

        // this should not be reached
	  abort();
    }
    else
    {
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "request is inserted into Global FIFO" << endl;
        LOG_VERBOSE_END
    }
}


// react to the initiators, processors or level 1 cache. 
void CacheL2TOK::BehaviorIni()
{
#ifdef MEM_MODULE_STATISTICS
    // statistics : initialization
    InitCycleStatINI();
#endif

    //DEBUG_TEST_XXX

#ifdef  OPTIMIZATION_NON_EXIST_LINE_BYPASS_BUFFER
    // $ optimization $
    m_pReqNonExistBypass = NULL;
#endif

    switch (m_nStateINI)
    {
    case STATE_INI_PROCESSING:
        // initialize requests to be sent
        m_pReqCurINIasNodeDB = NULL;
        m_pReqCurINIasNode = NULL;
        m_pReqCurINIasSlaveX = NULL;

        AdvancePipelineINI(true);

#ifdef  OPTIMIZATION_NON_EXIST_LINE_BYPASS_BUFFER
        // $ optimization $
        m_pReqNonExistBypass = NULL;
#endif

#ifdef MEM_MODULE_STATISTICS
        // statistics
        m_pStatCurReqINI = m_pReqCurINI;
#endif

        // if nothing to process, just skip this round
        if (m_pReqCurINI == NULL)
        {
            AutoFillSlaveReturnRequest(true);
            if (m_pReqCurINIasSlaveX != NULL)
            {
                SendFromINI();
            }

            break;
        }

        // process request
        ProcessInitiative();

        SendFromINI();

        break;

    // try to send the request again to the network
    case STATE_INI_RETRY:
        if (m_pPipelineINI->top() == NULL)
        {
            AdvancePipelineINI(false);
        }

        SendFromINI();

        break;

    default:
      abort();
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// passive handling
void CacheL2TOK::ProcessPassive()
{
#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
    if (m_nStatePAS == STATE_PAS_RESIDUE)
    {
LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
    clog << LOG_HEAD_OUTPUT << "start processing in residue mode" << endl;
LOG_VERBOSE_END
    }
#endif

    // JXXX maybe do the same with INI?
    if ( (m_pReqCurPAS != NULL)&&(m_pReqCurINI != NULL)&&(m_pReqCurPAS->getlineaddress()==m_pReqCurINI->getlineaddress()) )
    {
        Postpone();
        return;
    }
    
    m_nStatePAS = STATE_PAS_PROCESSING;

LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
    clog << LOG_HEAD_OUTPUT << "process network request " << FMT_ADDR(m_pReqCurPAS->getlineaddress()) << endl;
//    clog << m_pReqCurPAS << endl;
    clog << "\t"; print_request(m_pReqCurPAS);
LOG_VERBOSE_END

    ST_request* req = m_pReqCurPAS;
    switch(req->type)
    {
    case MemoryState::REQUEST_ACQUIRE_TOKEN:
		if (IS_NODE_INITIATOR(req, this))
			OnAcquireTokenRet(req);
		else
			OnAcquireTokenRem(req);
        break;

    case MemoryState::REQUEST_ACQUIRE_TOKEN_DATA:
		if (IS_NODE_INITIATOR(req, this))
			OnAcquireTokenDataRet(req);
		else
			OnAcquireTokenDataRem(req);
        break;

    case MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA:
        OnDisseminateTokenData(req);
        break;

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    case Request_LOCALDIR_NOTIFICATION:
      assert (LinkMGS::s_oLinkConfig.m_nDirectory != 0);
      OnDirNotification(req);
      break;
#endif

    default:
        break;
    }
}

bool CacheL2TOK::SendAsNodePAS(ST_request* req)
{
    // check colliding request
    if (  ((m_nStateINI == STATE_INI_RETRY)&&(m_pReqCurINIasNode!=NULL)&&(req->getlineaddress()==m_pReqCurINIasNode->getlineaddress()))  ||  ((m_nStateINI == STATE_INI_RETRY)&&(m_pReqCurINIasNodeDB!=NULL)&&(req->getlineaddress()==m_pReqCurINIasNodeDB->getlineaddress()))  )
    {
        // it's already retrying for the INI request on the same address, suspend the network request for this cycle, and send INI request instead.
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "network request address collide with INI retry request, sending request postponed." << endl;
        LOG_VERBOSE_END

        //m_nStatePAS = STATE_PAS_RETRY_AS_NODE;
        return false;

    }

    // send request to memory
    if (!RequestNetwork(req))
    {
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
                clog << LOG_HEAD_OUTPUT << "request transaction sent failed, network node busy" << endl;
        LOG_VERBOSE_END

        //m_nStatePAS = STATE_PAS_RETRY_AS_NODE;
        return false;
    }

#ifdef MEM_MODULE_STATISTICS
    // statistics
    if (m_pmmStatSendNodeNET != NULL)
    {
        m_pStatCurSendNodeNET = new ST_request(req);
        m_pStatCurSendNodeNET->ref = (unsigned long*)req;
    }
#endif

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "request transaction sent by network node" << endl;
        print_request(req);
    LOG_VERBOSE_END

#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
    if (m_pReqResidue != NULL)
    {
        SetStateToResiduePAS();
    }
    else
#endif
        // interface is free to process next req
        //m_nStatePAS = STATE_PAS_PROCESSING;

    return true;
}


bool CacheL2TOK::SendAsSlavePAS(ST_request* req)
{
    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "SEND AS SLAVE ALERT" << endl;
    LOG_VERBOSE_END

    // send reply transaction
    if (!channel_fifo_slave.nb_write(req))
    {
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "reply transaction sent failed, bus busy" << endl;
        LOG_VERBOSE_END

        return false;
    }

    if (req->type == REQUEST_READ_REPLY)
    {
        //RemoveBRfromInvalidationBuffer(m_pReqCurPASasSlave);
        m_fabInvalidation.RemoveBufferItem(req->getlineaddress());
        LOG_VERBOSE_BEGIN(VERBOSE_ALL)
            clog << LOG_HEAD_OUTPUT << "IB/WR address is removed from the invalidation matching buffer" << endl;
        LOG_VERBOSE_END
    }
    else if (req->type == REQUEST_WRITE_REPLY)
    {
#ifndef MEM_CACHE_LEVEL_ONE_SNOOP
        char *data;
        int index;
        if (!m_fabInvalidation.FindBufferItem(req->getlineaddress(), index, data))
        {
            req->bbackinv = true;
            LOG_VERBOSE_BEGIN(VERBOSE_ALL)
                clog << LOG_HEAD_OUTPUT << "PAS: WR address is going to be broadcasted and backward invalidate other caches." << endl;
            LOG_VERBOSE_END
        }
#endif
    }

    LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "reply transaction sent to the processor/L1 cache" << endl;
    LOG_VERBOSE_END

#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
    if (m_pReqResidue != NULL)
    {
        SetStateToResiduePAS();
    }
    else
#endif
    // interface is free to process next req

    // statistics
    //if (m_pReqCurPASasSlave->type == REQUEST_READ_REPLY_X)
#ifdef MEM_MODULE_STATISTICS
        m_pStatTestNET = req;
#endif

    return true;
}


void CacheL2TOK::SendFromPAS()
{
    AutoFillSlaveReturnRequest(false);
    AutoFillPASNodeRequest();
    bool sentnode = (m_pReqCurPASasNodeX==NULL);
    bool sentslave = (m_pReqCurPASasSlaveX==NULL);

    if ((m_pReqCurPASasSlaveX==NULL)&&(m_pReqCurPASasNodeX==NULL))
    {
        // assert the state
        // actually the state can only be processing
        assert((m_nStatePAS == STATE_PAS_PROCESSING)||(m_nStatePAS == STATE_PAS_POSTPONE));
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << LOG_HEAD_OUTPUT << "Cache PAS skip to next cycle" << endl;
        LOG_VERBOSE_END

        // maybe this is not necessary
        m_nStatePAS = STATE_PAS_PROCESSING;
        return;
    }

    if (m_pReqCurPASasSlaveX!=NULL)
    {
        if (!SendAsSlavePAS(m_pReqCurPASasSlaveX))
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "PAS slave sent failed" << endl;
            LOG_VERBOSE_END

            sentslave = false;
        }
        else
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "PAS slave sent succeeded" << endl;
            LOG_VERBOSE_END

            sentslave = true;
            m_pReqCurPASasSlaveX = NULL;
        }
    }

    if (m_pReqCurPASasNodeX!=NULL)
    {
        if (!SendAsNodePAS(m_pReqCurPASasNodeX))
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "PAS node sent failed" << endl;
            LOG_VERBOSE_END

            sentnode = false;
        }
        else
        {
            LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
                clog << LOG_HEAD_OUTPUT << "PAS node sent succeeded" << endl;
            LOG_VERBOSE_END

            sentnode = true;
            m_pReqCurPASasNodeX = NULL;
        }
    }

    if ((sentnode&&sentslave)&&(m_queReqPASasNode.empty()))
    {
        m_nStatePAS = STATE_PAS_PROCESSING;
    }
    else
    {
        m_nStatePAS = STATE_PAS_RETRY;
    }
}

void CacheL2TOK::FinishCyclePAS()
{
    // reset state to processing 
    m_nStatePAS = STATE_PAS_PROCESSING;

    LOG_VERBOSE_BEGIN(VERBOSE_ALL)
        clog << LOG_HEAD_OUTPUT << "cache passive cycle done, pipeline done" << endl;
    LOG_VERBOSE_END
}

void CacheL2TOK::Postpone()
{
    // reset state to processing 
    assert((m_nStatePAS==STATE_PAS_PROCESSING)||(m_nStatePAS==STATE_PAS_POSTPONE));
    m_nStatePAS = STATE_PAS_POSTPONE;

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "pipeline stop, processing postponed" << endl;
    LOG_VERBOSE_END
}

ST_request* CacheL2TOK::FetchRequestPAS()
{
    ST_request* req_incoming = NULL;
    if (m_fifoinNetwork.num_available_fast() <= 0)
    {
        req_incoming = NULL;
    }
    else if (!m_fifoinNetwork.nb_read(req_incoming))
    {
        cerr << ERR_HEAD_OUTPUT << "should not get failed in this nb_read request" << endl;
        abort();
        return NULL;
    }

    LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "net request in " << FMT_ADDR(req_incoming->getlineaddress()) << endl;
        clog << "\t"; print_request(req_incoming);
    LOG_VERBOSE_END

    return req_incoming;
}


// react to the network request/feedback
void CacheL2TOK::BehaviorNet()
{
#ifdef MEM_MODULE_STATISTICS
    // statistics : initialization
    InitCycleStatNET();
#endif

    //DEBUG_TEST_XXX

    ST_request* req_incoming;

    switch (m_nStatePAS)
    {
    case STATE_PAS_PROCESSING:
        req_incoming  = FetchRequestPAS();

        // shift pipeline and get request from pipeline
        m_pReqCurPAS = m_pPipelinePAS->shift(req_incoming);

        // reset combined pointer
        m_pReqCurPASasSlaveX = NULL;
        m_pReqCurPASasNodeX = NULL;

#ifdef MEM_MODULE_STATISTICS
        // statistics
        m_pStatCurReqNET = m_pReqCurPAS;
#endif

#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
        m_pReqResidue = NULL;      // reset residue request pointer
#endif

        if (m_pReqCurPAS == NULL)
        {
#ifdef OPTIMIZATION_NON_EXIST_LINE_BYPASS_BUFFER
            // $$$ optimization - non-existing line bypass $$$
            TryMissingLineBypass();
#endif

            AutoFillSlaveReturnRequest(false);
            AutoFillPASNodeRequest();
            if (m_pReqCurPASasSlaveX != NULL)
                SendFromPAS();

            break;
        }

        // processing the reply request
        ProcessPassive();

        if (m_nStatePAS == STATE_PAS_POSTPONE)
            break;

        SendFromPAS();

        break;

    case STATE_PAS_POSTPONE:
#ifdef MEM_MODULE_STATISTICS
        // statistics
        m_pStatCurReqNET = m_pReqCurPAS;
#endif

        ProcessPassive();

        if (m_nStatePAS == STATE_PAS_POSTPONE)
            break;

        SendFromPAS();

        break;

    case STATE_PAS_RETRY:
        if (m_pPipelinePAS->top() == NULL)
        {
            req_incoming = FetchRequestPAS();

            // shift pipeline and get request from pipeline
            m_pPipelinePAS->shift(req_incoming);
        }

        SendFromPAS();
        break;

#ifdef IMMEDIATE_ACTIVATE_QUEUE_MODE
    case STATE_PAS_RESIDUE:
        m_nWaitCountPAS++;
        if (m_nWaitCountPAS >= m_nRepeatLatency)
        {
            ProcessPassive();
        }
        break;
#endif

    default:
      abort();
        break;
    }
}

void CacheL2TOK::BindProcTopology(ProcessorTOK &proc)
{
    // save the id
    int pid = (int)proc.GetProcessorID();

    // update pidmax
    m_nPidMax = (m_nPidMax < pid)?pid:m_nPidMax;

    if (m_nPidMin == -1)
    {
        m_nPidMin = pid;
    }
    else
    {
        m_nPidMin = (m_nPidMin > pid)?pid:m_nPidMin;
    }
}


// the hash function for deciding whether to invalidate
bool CacheL2TOK::RacingHashInvalidation(int pid)
{
    // check processor topology
  assert(!( (m_nPidMin > m_nPidMax) || (m_nPidMax < 0) || (m_nPidMin < 0) ));

    // make simple decision
    if (pid < m_nPidMin)
    {
        return false;
    }

    if (pid > m_nPidMax)
    {
        return true;
    }

    // hash invalidation shouldn't have the request from the same cache
    cerr << ERR_HEAD_OUTPUT << "hash invalidation shouldn't happen to itself!" << endl;
    abort();

    return false;
}


//////////////////////////////////////////////////////////////////////////
// common handler

//bool CacheL2TOK::DirectForward(ST_request* req)
//{
//    // other situation false
//    return false;
//}

bool CacheL2TOK::MayHandle(ST_request* req)
{

    return true;
}

bool CacheL2TOK::CanHandleNetRequest(ST_request* request)
{
    return true;

}


cache_line_t* CacheL2TOK::LocateLine(__address_t address)
{
    cache_line_t* line;
    unsigned int index = CacheIndex(address);
    uint64 tag = CacheTag(address);

    line = &(m_pSet[index].lines[0]);

    for (unsigned int i=0; i<m_nAssociativity;i++, line++)
    {
        if ((line->state != CLS_INVALID) && (line->tag == tag))
            return line;
    }

    // miss
    return NULL;
}

cache_line_t* CacheL2TOK::LocateLineEx(__address_t address)
{
    cache_line_t* line;
    unsigned int index = CacheIndex(address);
    uint64 tag = CacheTag(address);

    line = &(m_pSet[index].lines[0]);

    for (unsigned int i=0; i<m_nAssociativity;i++, line++)
    {
        if ((line->state != CLS_INVALID) && (line->tag == tag))
            return line;
    }

    // miss
    return NULL;
}

cache_line_t* CacheL2TOK::GetReplacementLine(__address_t address)
{
    cache_line_t *line, *lruline;
    unsigned int index = CacheIndex(address);
    // uint64 tag = CacheTag(address);

    lruline = line = &(m_pSet[index].lines[0]);
    for (unsigned int i=0; i<m_nAssociativity; i++, line++)
    {
        // return the first found empty one
        if (line->state == CLS_INVALID)
            return line;

        // keep lruline as least recent used line
        if (line->time < lruline->time)
            lruline = line;
    }

    // for RP_RND
    if (m_policyReplace == RP_RND)
    {
        unsigned i = rand() % m_nAssociativity;
        line = &(m_pSet[index].lines[i]);
    }

    return lruline;
}

cache_line_t* CacheL2TOK::GetReplacementLineEx(__address_t address)
{
  abort();
    cache_line_t *line, *lruline;
    unsigned int index = CacheIndex(address);
    // uint64 tag = CacheTag(address);

    lruline = line = &(m_pSet[index].lines[0]);
    for (unsigned int i=0; i<m_nAssociativity; i++, line++)
    {
        // return the first found empty one
        if (line->state == CLS_INVALID)
            return line;

        // keep lruline as least recent used line
        if (line->time < lruline->time)
            lruline = line;
    }

    // for RP_RND
    if (m_policyReplace == RP_RND)
    {
        unsigned i = rand() % m_nAssociativity;
        lruline = &(m_pSet[index].lines[i]);
    }

    return lruline;
}

cache_line_t* CacheL2TOK::GetEmptyLine(__address_t address)
{
    cache_line_t* line = &(m_pSet[CacheIndex(address)].lines[0]);
 
    for (unsigned int i=0; i<m_nAssociativity; i++, line++)
    {
        // return the first found empty one
        if (line->state == CLS_INVALID)
            return line;
    }

    return NULL;
}

ST_request* CacheL2TOK::PickRequest(ST_request* req)
{
    vector<ST_request*>::iterator iter;
    for (iter=m_vRedirectedRequest.begin();iter != m_vRedirectedRequest.end();iter++)
    {
        ST_request* ireq = (ST_request*)*iter;
        // **** check type as well
        if (req->getreqaddress() == ireq->getreqaddress())	//// !!!!????****
        {
LOG_VERBOSE_BEGIN(VERBOSE_ALL)
            clog << LOG_HEAD_OUTPUT << "request matched"  << endl;
LOG_VERBOSE_END

            if ((req->type == REQUEST_READ_REPLY)||(req->type == REQUEST_WRITE_REPLY))
                m_vRedirectedRequest.erase(iter);
            return ireq;
        }
    }
    cerr << ERR_HEAD_OUTPUT << "request not found!" << endl;
    return NULL;
}


void CacheL2TOK::UpdateCacheLine(cache_line_t* line, ST_request* req, CACHE_LINE_STATE state, unsigned int token, bool pending, bool invalidated, bool priority, bool tlock, LINE_UPDATE_METHOD lum, bool bupdatetime)
{
    // make sure the reuqest conform to the bit vector format before entering this procedure.

    // always assume the cache line is picked up correctly

    // always assume line is already found thus
    assert(line != NULL);

    // request has to abey the alignment

	assert(req->getreqaddress()%CACHE_REQUEST_ALIGNMENT == 0);

    if ( (req->type == REQUEST_READ) || (req->type == REQUEST_WRITE) || (req->type == REQUEST_READ_REPLY) || (req->type == REQUEST_WRITE_REPLY) )
    	assert(req->nsize%CACHE_REQUEST_ALIGNMENT == 0);     // maybe treat this differently

    if ( (lum != LUM_NO_UPDATE)&&(req->nsize > s_nLineSize) )	//// ****
    {
        cerr << ERR_HEAD_OUTPUT << "wrong request size, doesn't support large request size" << endl;
        return;
    }

    if (lum == LUM_STORE_UPDATE)
    {
        if ((!line->pending)&&(state == CLS_OWNER))
        {
            // clear the bitmask 
            for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
                line->bitmask[i] = 0;

        }
        lum = LUM_PRIMARY_UPDATE;
    }

    if (state != CLS_INVALID)
    {
#ifdef OPTIMIZATION_NON_EXIST_LINE_BYPASS_BUFFER
        // $$$ optimization non-existing line $$$
        m_fabNonExistLine.RemoveBufferItem(req->getlineaddress());
#endif
    }

    // update TAG
    line->tag = CacheTag(req->getlineaddress());

    // update time if necessary
    if (bupdatetime)
        line->time = sc_time_stamp();

    // update line state
    line->state = state;

    // update other info
    line->tokencount = token;
    line->pending = pending;
    line->invalidated = invalidated;
    line->priority = priority;
    line->tlock = tlock;

	// reset the mask bits for CLS_INVALID
	if (state == CLS_INVALID)
	{
		for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
			line->bitmask[i] = 0;

        assert(priority == false);
	}

    if (line->tokencount == 0)
    {
        assert(priority == false);
    }
    else if (line->tokencount == GetTotalTokenNum())
    {
        assert(priority == true);
    }

    // if no need to update data
    if (lum == LUM_NO_UPDATE)
        return; 

    // check whether it's clear [***] update, if yes, clear the mask 
    if (lum == LUM_CLR_PRIMARY_UPDATE)
    {
        //assert(state == LNWRITEPENDINGM);
        assert(state == CLS_OWNER);
        assert(pending);
        assert(token > 0);
        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
            line->bitmask[i] = 0;
        lum = LUM_PRIMARY_UPDATE;
    }

    // primary update will overwrite all the updated information from the request to the cacheline
	if (lum == LUM_PRIMARY_UPDATE)	// incremental update
	{
        // reset the mask bits for CLS_INVALID
        if (state == CLS_INVALID)
        {
            // should this be reached?
	  abort();
            for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
                line->bitmask[i] = 0;

            return;
        }

        bool allupdate = true;
        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
        {
            if ((unsigned char)req->bitmask[i] == 0xff)
                continue;
            else
            {
                allupdate = false;
                break;
            }
        }
        // maybe also check from the state point of view to verify

        // dont need to check the offset and size of the request 
        // assume the request is always at the cache line size to be handled in this level

        if (req->IsRequestWithCompleteData() && allupdate)
        {
            memcpy(line->data, req->data, g_nCacheLineSize);

            for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
                line->bitmask[i] = 0xff;
        }
        else
        {
            for (unsigned int i=0;i<g_nCacheLineSize;i+=CACHE_REQUEST_ALIGNMENT)
            {
                unsigned int maskhigh = i / (8*CACHE_REQUEST_ALIGNMENT);
                unsigned int masklow = i % (8*CACHE_REQUEST_ALIGNMENT);


                char maskbit = 1 << masklow;

                if ((req->IsRequestWithCompleteData())||((req->bitmask[maskhigh]&maskbit) != 0))
                {
                    line->bitmask[maskhigh] |= maskbit;

                    memcpy(&line->data[i], &req->data[i], CACHE_REQUEST_ALIGNMENT);
                }
            }

        }
	}

    // feedback update will update only the empty slots in the cacheline
    // and finally make the line complete
	else if ( (lum == LUM_FEEDBACK_UPDATE)||(lum == LUM_NOM_FEEDBACK_UPDATE) )	// complete the cacheline
	{
        // make sure the request is complete
#ifndef NDEBUG
        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
        {

	  assert ((unsigned char)req->bitmask[i] == 0xff);
        }
#endif

        // reset the mask bits for CLS_INVALID
        if (state == CLS_INVALID)
        {
            for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
                line->bitmask[i] = 0;

            return;
        }

#ifdef SIMULATE_DATA_TRANSACTION
		// __address_t alignedaddr = AlignAddress4Cacheline(req->getlineaddress());

		// check the mask bits, and only update the unmasked ones
		for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH;i++)
		{
			unsigned int maskhigh = i/8;
			unsigned int masklow = i%8;

			char testchr = 1 << masklow;

			if ((line->bitmask[maskhigh]&testchr) == 0)	// if it's unmasked
			{
				// update line
				memcpy(&line->data[i*CACHE_REQUEST_ALIGNMENT], &req->data[i*CACHE_REQUEST_ALIGNMENT], CACHE_REQUEST_ALIGNMENT);
				// CHKS:  assumed replied data always contains the whole line
				// which means replied data are always cacheline aligned.
			}
		}
#endif

		// update the mask bits
		
		if (lum == LUM_FEEDBACK_UPDATE)
			for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
				line->bitmask[i] = 0xff;
	}
	else
	{
		cerr << ERR_HEAD_OUTPUT << "wrong update method specified : " << lum << endl;
	}
}

void CacheL2TOK::IVUpdateCacheLine(cache_line_t* line, ST_request* req, CACHE_LINE_STATE state, unsigned int token, bool pending, bool invalidated, bool priority, bool tlock, LINE_UPDATE_METHOD lum, bool bupdatetime)
{
    // always assume the cache line is picked up correctly

    // always assume line is already found thus
    assert(line != NULL);

    assert(lum == LUM_RAC_FEEDBACK_UPDATE);

    // assert tag
    assert(line->tag == CacheTag(req->getlineaddress()));

    // assert quest size // double check, this might be no longer needed

    // update time
    if (bupdatetime)
        line->time = sc_time_stamp();

	bool bupdatealldata = ( (line->state == CLS_OWNER)&&(state == CLS_OWNER)&&(line->pending)&&(pending)&&(line->tokencount ==0)&&(token > 0) );

    // update line state (maybe not needed)
    line->state = state;

    // update other info
    line->tokencount = token;
    line->pending = pending;
    line->invalidated = invalidated;
    line->priority = priority;
    line->tlock = tlock;

    if (line->tokencount == 0)
    {
        assert(priority == false);
    }
    else if (line->tokencount == GetTotalTokenNum())
    {
        assert(priority == true);
    }

    // update the cacheline with the dirty data in the request
    // start from the offset and update the size from the offset
    if (lum == LUM_RAC_FEEDBACK_UPDATE) // of course it is
    {

        //for (unsigned int i=0;i<req->nsize;i++)
		for (unsigned int i=0;i<g_nCacheLineSize;i++)
        {
            // get the mask high and low
            unsigned int maskhigh = i/8;
            unsigned int masklow = i%8;

            // make the bit mask for this certain data character
            char testchar = 1 << masklow;

			// if not all the data needs to be updated. then skip the unnecessary parts, only update if required
			if ((req->bitmask[maskhigh]&testchar) == 0)
			{
                if ( (bupdatealldata) && ((line->bitmask[maskhigh]&testchar) == 0) )    // then update
					line->data[i] = req->data[i];

				continue;
			}
            else
            {
                // if the mask is already on there in the cache line then dont update 
                // otherwise update
                if ( ((line->bitmask[maskhigh]&testchar) == 0)||(bupdatealldata) )    // then update
                {
                    line->data[i] = req->data[i];
                    
                    // update the cacheline mask
                    line->bitmask[maskhigh] |= testchar;
                }
            }

            //// we dont care about the cacheline bit mask, we just write the updated data.

        }
    }
}


void CacheL2TOK::UpdateRequest(ST_request* req, cache_line_t* line, MemoryState::REQUEST requesttype, __address_t address, bool bdataavailable, bool bpriority, bool btransient, unsigned int ntokenacquired, REQUEST_UPDATE_METHOD rum)
{
    req->type = requesttype;
    req->tokenacquired = ntokenacquired;
    req->dataavailable = bdataavailable;
    req->bpriority = bpriority;
    req->btransient = btransient;

    if (req->bpriority)
    {
        assert(req->btransient == false);
    }

    if (ntokenacquired == 0)
        assert(bpriority == false);
    else if (ntokenacquired == GetTotalTokenNum())
        assert(bpriority == true);

    if ((requesttype == REQUEST_READ)||(requesttype == REQUEST_WRITE)||(requesttype == REQUEST_READ_REPLY)||(requesttype == REQUEST_WRITE_REPLY)||(requesttype == REQUEST_INVALIDATE_BR))
    {
        assert(bpriority == false);
        assert(ntokenacquired == 0xffff);
    }

    assert(line != NULL);
    assert(line->state != CLS_INVALID);

    // if nothing to be updated
    if (rum == RUM_NONE)
        return;


    if (rum == RUM_ALL)
        req->dataavailable = true;

    bool blinecomplete = line->IsLineAtCompleteState();

#ifdef SIMULATE_DATA_TRANSACTION

    // check the whole line with mask bits and update the request according to the cacheline
    for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH;i++)
    {
        unsigned int maskhigh = i / 8;
        unsigned int masklow = i % 8;

        char testmask = 1 << masklow;

        if ( ((testmask & line->bitmask[maskhigh]) != 0) || (blinecomplete) )
        {
            // copy that part
            for (int j=0;j<CACHE_REQUEST_ALIGNMENT;j++)
            {
				// calculate the current position
//				unsigned int curpos = i*8+j;

				//if ( (rum == RUM_MASK) && ((curpos<reqoffset)||(curpos>=(reqoffset+reqnsize))) )
				if ( (rum == RUM_MASK) && ((req->bitmask[maskhigh]&testmask) == 0) )
					continue;

				//if ( (rum == RUM_NON_MASK) && ((i>=reqoffset)&&(i<(reqoffset+reqnsize))) )
				if ( (rum == RUM_NON_MASK) && ((req->bitmask[maskhigh]&testmask) != 0) )
					continue;

                unsigned int index = i*CACHE_REQUEST_ALIGNMENT+j;
                req->data[index] = line->data[index];

                // update read request bitmaks, write request bitmask will not be updated. 
                if ((requesttype == REQUEST_ACQUIRE_TOKEN_DATA) && (req->tokenrequested != GetTotalTokenNum()))
                {
                    req->bitmask[maskhigh] |= testmask;
                }
            }
        }

    }
    
   
#else
    req->data[0] = address;
#endif
}

// check the line validness according to the bitmask associated with the line
bool CacheL2TOK::CheckLineValidness(cache_line_t* line, bool bwholeline, char* pmask)
{
    if (!bwholeline)
    {
      assert (pmask != NULL);

        // this part is not implemented yet
      abort();
    }

    // a patch to writepending states
    if ((line->pending)&&(line->state == CLS_OWNER)&&(line->tokencount > 0))
    {
        return true;
    }

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    if ((line->pending)&&(line->state == CLS_OWNER)&&(line->tokencount==0)&&(!line->invalidated))
    {
        return true;
    }
#endif


    // check the bitmasks
    for (int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
    {
        if (bwholeline)
        {
            if ((line->bitmask[i]) != (char)0xff)   // check only for the whole line
            {
                return false;
            }
        }
        else
        {
            if (line->bitmask[i] != pmask[i])   // check only the partial line according to the mask
                return false;
        }
    }

    return true;
}

#ifdef OPTIMIZATION_NON_EXIST_LINE_BYPASS_BUFFER
// optimization for missing line bypass
void CacheL2TOK::TryMissingLineBypass()
{
    // check pipeline for request
    list<ST_request*>::iterator iter;
    vector<ST_request*> vecdump;

    // dump request to a vector
    for (iter = m_pPipelinePAS->m_lstRegisters.begin();iter != m_pPipelinePAS->m_lstRegisters.end();iter++)
    {
        ST_request* req = *iter;
        vecdump.push_back(req);
    }

    ST_request* request_bypass = NULL;
    unsigned int bypass_index;

    // from the top check each request, see whether the request has other requests in front acquire the same line
    for (unsigned int i=0;i<vecdump.size();i++)
    {
        if (vecdump[i] == NULL)
            continue;

        bool bcollide = false;
        // check each one whether they are 
        for (unsigned int j=0;j<i;j++)
        {
            if (vecdump[j] == NULL)
                continue;

            if (vecdump[i]->getlineaddress() == vecdump[j]->getlineaddress())
            {
                bcollide = true;
                break;
            }

            if ( (get_initiator_node(vecdump[i]) == get_initiator_node(vecdump[j])) && (CacheIndex(vecdump[i]->getlineaddress()) == CacheIndex(vecdump[j]->getlineaddress())) )
            {
                bcollide = true;
                break;
            }

        }

        // if found some one trying the same line, then skip this request
        if (bcollide)
            continue;   // try next one 

        // CHKS: maybe SET
        if ( (m_pReqNonExistBypass != NULL)&&(m_pReqNonExistBypass->getlineaddress() == vecdump[i]->getlineaddress()) )
        {
            // abort();
            continue;
        }

        // check whether the line is directly invalid
        int index;
        char* data;
        if (m_fabNonExistLine.FindBufferItem(vecdump[i]->getlineaddress(), index, data))
        {
            request_bypass = vecdump[i];
            bypass_index = i;
            break;
        }
    }

    // if nothing to bypass
    if (request_bypass == NULL)
        return;

    // remove the request
    vecdump[bypass_index] = NULL;

    // clean the list
    m_pPipelinePAS->m_lstRegisters.clear();

    // refill all the list
    for (unsigned int i=0;i<vecdump.size();i++)
    {
        m_pPipelinePAS->m_lstRegisters.push_back(vecdump[i]);
    }

    // double check
    cache_line_t* pline = LocateLineEx(request_bypass->getlineaddress());
    assert (pline == NULL);


    // perform bypassing
    // Pass the transaction down
    LOG_VERBOSE_BEGIN(VERBOSE_ALL)
        clog << LOG_HEAD_OUTPUT << "line not present" << endl;
    LOG_VERBOSE_END

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "following request bypassed address " << FMT_ADDR(request_bypass->getreqaddress()) << endl;
        print_request(request_bypass);
    LOG_VERBOSE_END

    // save the current request
    m_pReqCurPASasNodeX = request_bypass;

    // try to send the request to network
    SendAsNodePAS();
}
#endif

// create new eviction or write back request: 
ST_request* CacheL2TOK::NewEVorWBRequest(ST_request* req, cache_line_t* pline, bool beviction, bool bINI)
{
    // create and initialize the new eviction / write back request
    ST_request* reqnew = new ST_request();

    reqnew->type = MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA;

    if (beviction)
        reqnew->tokenrequested = 0;     // request
    else
        reqnew->tokenrequested = GetTotalTokenNum();

    reqnew->addresspre = pline->getlineaddress(CacheIndex(req->getlineaddress()), m_nSetBits) >> g_nCacheLineWidth;
    reqnew->offset = 0;
    reqnew->nsize = g_nCacheLineSize;
    reqnew->Conform2BitVecFormat();
    reqnew->dataavailable = true;
    memcpy(reqnew->data, pline->data, g_nCacheLineSize);			// copy data ??? maybe not necessary for EVs
    ADD_INITIATOR_BUS(reqnew, this);

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "New DD request " << FMT_ADDR(reqnew->getlineaddress()) << endl;
        print_request(reqnew);
    LOG_VERBOSE_END


#ifdef OPTIMIZATION_VICTIM_BUFFER
    // $$$ optimization for victim line buffer $$$
    m_fabEvictedLine.InsertItem2Buffer(reqnew->getlineaddress(), reqnew->data, g_nCacheLineSize);
#endif

    // if it's from passive interface, skip new IB request
    if (!bINI)
        return reqnew;

    if (m_nBBIPolicy == BBI_EAGER)
    {
        assert(m_pReqCurINIasSlaveX == NULL);
        ST_request *reqib = new ST_request(reqnew);
        ADD_INITIATOR_NODE(reqib, (void*)NULL);
        reqib->type = REQUEST_INVALIDATE_BR;
        m_pReqCurINIasSlaveX = reqib;
    }
    else
    {}

    return reqnew;
}




bool CacheL2TOK::InsertOutstandingRequest(ST_request* req)
{
	__address_t alignedaddr = AlignAddress4Cacheline(req->getlineaddress());

	pair<map<__address_t, ST_request*>::iterator,bool> ret;

	ret = m_mapPendingRequests.insert(pair<__address_t, ST_request*>(alignedaddr, req));

	if (ret.second==false)
	{
		cerr << ERR_HEAD_OUTPUT << "request to the same location already exist" << endl;
		return false;
	}

	return true;
}


ST_request* CacheL2TOK::RemoveOutstandingRequest(__address_t address)
{
	__address_t alignedaddr = AlignAddress4Cacheline(address);

	map<__address_t, ST_request*>::iterator iter;

	iter = m_mapPendingRequests.find(alignedaddr);

	if (iter == m_mapPendingRequests.end())
		return NULL;

	ST_request* ret = iter->second;
	m_mapPendingRequests.erase(iter);

	return ret;
}

bool CacheL2TOK::PreFill(__address_t lineaddr, char *data)
{
/*
    cache_line_t* line = LocateLineEx(lineaddr);

    // if line is already there, no more loading
    if (line != NULL)
    {
        assert(line->state == LNSHARED);
        return false;
    }

    line = GetReplacementLineEx(lineaddr);

    // something available already
    if (line->state != LNINVALID)
        return false;

    line->state = LNSHARED;
    line->tag = CacheTag(lineaddr);
    for (int i=0;i<(CACHE_BIT_MASK_WIDTH/8);i++)
       line->bitmask[i] = (char)0xff;

    memcpy(line->data, data, g_nCacheLineSize);
*/
    return true;
}

// ntoken is the token required
void CacheL2TOK::Modify2AcquireTokenRequest(ST_request* req, unsigned int ntoken)
{
    ADD_INITIATOR_NODE(req, this);

    req->tokenrequested = ntoken;
    req->dataavailable = false;

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "New token acquisition("<< ntoken << ") request " << FMT_ADDR(req->getlineaddress()) << " is prepared." << endl;
        print_request(req);
    LOG_VERBOSE_END
}

void CacheL2TOK::Modify2AcquireTokenRequestRead(ST_request* req)
{
    assert((req->type != MemoryState::REQUEST_ACQUIRE_TOKEN)&&(req->type != MemoryState::REQUEST_ACQUIRE_TOKEN_DATA));

    req->type = MemoryState::REQUEST_ACQUIRE_TOKEN_DATA;
    // clear request bitmask
    for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
        req->bitmask[i] = 0;

    Modify2AcquireTokenRequest(req, 1);
}

void CacheL2TOK::Modify2AcquireTokenRequestWrite(ST_request* req, bool reqdata)
{
    assert((req->type != MemoryState::REQUEST_ACQUIRE_TOKEN)&&(req->type != MemoryState::REQUEST_ACQUIRE_TOKEN_DATA));

    if (reqdata)
        req->type = MemoryState::REQUEST_ACQUIRE_TOKEN_DATA;
    else
        req->type = MemoryState::REQUEST_ACQUIRE_TOKEN;

    Modify2AcquireTokenRequest(req, GetTotalTokenNum());
}

void CacheL2TOK::Modify2AcquireTokenRequestWrite(ST_request* req, cache_line_t* line, bool reqdata)
{
    assert((req->type != MemoryState::REQUEST_ACQUIRE_TOKEN)&&(req->type != MemoryState::REQUEST_ACQUIRE_TOKEN_DATA));

    if (reqdata)
        req->type = MemoryState::REQUEST_ACQUIRE_TOKEN_DATA;
    else
        req->type = MemoryState::REQUEST_ACQUIRE_TOKEN;

    Modify2AcquireTokenRequest(req, GetTotalTokenNum());
}

// disseminate a number of tokens
ST_request* CacheL2TOK::NewDisseminateTokenRequest(ST_request* req, cache_line_t* line, unsigned int ntoken, bool bpriority)
{
#ifndef MEMSIM_DIRECTORY_REQUEST_COUNTING
    assert(line->tokencount > 0);
    assert(!line->pending);
    assert(!line->invalidated);
#endif
    assert(line->tokencount >= ntoken);
    assert(line->state != CLS_INVALID);
    ST_request* reqdt = new ST_request();

    if (line->priority == false)
        assert(bpriority == false);

    reqdt->type = MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA;
    reqdt->addresspre = line->getlineaddress(CacheIndex(req->getlineaddress()), m_nSetBits) >> g_nCacheLineWidth;
    reqdt->offset = 0;
    reqdt->nsize = g_nCacheLineSize;
    reqdt->Conform2BitVecFormat();
    reqdt->dataavailable = true;
    memcpy(reqdt->data, line->data, g_nCacheLineSize);
    ADD_INITIATOR_BUS(reqdt, this);

    if (bpriority)
    {
        reqdt->bpriority = true;
        line->priority = false;
    }
    else
        reqdt->bpriority = false;

    reqdt->tokenacquired = ntoken;

    if (line->state == CLS_SHARER)
    {
        reqdt->tokenrequested = 0;
    }
    else
    {
        reqdt->tokenrequested = GetTotalTokenNum();
    }
    assert(reqdt->btransient == false);

    line->tokencount = line->tokencount - ntoken;

    PostDisseminateTokenRequest(line, reqdt);

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "New token dissemination(" << ntoken << ") request " << FMT_ADDR(reqdt->getlineaddress()) << " is created." << endl;
        print_request(reqdt);
    LOG_VERBOSE_END


    return reqdt;
}

ST_request* CacheL2TOK::NewDisseminateTokenRequest(ST_request* req, cache_line_t* line)
{
    return NewDisseminateTokenRequest(req, line, line->tokencount, line->priority);
}

void CacheL2TOK::PostDisseminateTokenRequest(cache_line_t* line, ST_request* pdisseminatetokenreq)
{
    if (line->tokencount == 0)
    {
        line->state = CLS_INVALID;
        line->pending = false;
        line->invalidated = false;
        line->priority = false;

#ifdef OPTIMIZATION_VICTIM_BUFFER
        // $$$ optimization for victim line buffer $$$
        m_fabEvictedLine.InsertItem2Buffer(pdisseminatetokenreq->getlineaddress(), pdisseminatetokenreq->data, g_nCacheLineSize);
#endif
    }
}

void CacheL2TOK::AcquireTokenFromLine(ST_request* req, cache_line_t* line, unsigned int ntoken)
{
    // make sure they have the same address
    assert(req->getlineaddress() == line->getlineaddress(CacheIndex(req->getlineaddress()), m_nSetBits));

    // make desision what to do 

    // first plan, give as much as required, if possible
}

void CacheL2TOK::InsertSlaveReturnRequest(bool ini, ST_request *req)
{
    assert(req != NULL);
    if (ini)
    {
        m_queReqINIasSlave.push(req);
        if (m_pReqCurINIasSlaveX == NULL)
        {
            m_pReqCurINIasSlaveX = m_queReqINIasSlave.front();
            m_queReqINIasSlave.pop();
        }
    }
    else
    {
        m_queReqPASasSlave.push(req);
        if (m_pReqCurPASasSlaveX == NULL)
        {
            m_pReqCurPASasSlaveX = m_queReqPASasSlave.front();
            m_queReqPASasSlave.pop();
        }
    }

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "Req done address " << FMT_ADDR(req->getreqaddress()) << "returned." << endl;
        if (req->type != REQUEST_INVALIDATE_BR)
            print_request(req);
    LOG_VERBOSE_END
}

void CacheL2TOK::InsertPASNodeRequest(ST_request* req)
{
    m_queReqPASasNode.push(req);
    if (m_pReqCurPASasNodeX == NULL)
    {
        m_pReqCurPASasNodeX = m_queReqPASasNode.front();
        m_queReqPASasNode.pop();
    }

    LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
        clog << LOG_HEAD_OUTPUT << "Request address " << FMT_ADDR(req->getreqaddress()) << " is going to be send to network." << endl;
        assert(req->type != REQUEST_INVALIDATE_BR);
        print_request(req);
    LOG_VERBOSE_END
}

void CacheL2TOK::AutoFillPASNodeRequest()
{
    if (m_pReqCurPASasNodeX == NULL)
    {
        if (!m_queReqPASasNode.empty())
        {
            m_pReqCurPASasNodeX = m_queReqPASasNode.front();
            m_queReqPASasNode.pop();
        }
        else
            m_pReqCurPASasNodeX = NULL;
    }
}


#ifdef MEM_MODULE_STATISTICS
void CacheL2TOK::InitCycleStatINI()
{
    m_bStatCleansingINI = false;
    m_pStatCurReqINI = NULL;
    m_pStatCurSendNodeINI = NULL;
    m_pStatTestINI = NULL;
}

void CacheL2TOK::InitCycleStatNET()
{
    m_pStatCurReqNET = NULL;
    m_pStatTestNET = NULL;
    m_pStatCurSendNodeNET = NULL;
}

void CacheL2TOK::InitializeStatistics(unsigned int components)
{
    switch(components)
    {
    case STAT_CACHE_COMP_REQUEST_NO:
        if (m_pmmStatRequestNo == NULL)
            m_pmmStatRequestNo = new map<double, stat_stru_size_t>();
        break;

    case STAT_CACHE_COMP_PROCESSING_INI:
        if (m_pmmStatProcessingINI == NULL)
            m_pmmStatProcessingINI  = new map<double, stat_stru_request_t>();
        break;

    case STAT_CACHE_COMP_PROCESSING_NET:
        if (m_pmmStatProcessingNET == NULL)
            m_pmmStatProcessingNET  = new map<double, stat_stru_request_t>();
        break;

    case STAT_CACHE_COMP_PIPELINE_INI:
        if (m_pmmStatPipelineINI == NULL)
            m_pmmStatPipelineINI = new map<double, stat_stru_request_list_t>();
        break;

    case STAT_CACHE_COMP_PIPELINE_NET:
        if (m_pmmStatPipelineNET == NULL)
            m_pmmStatPipelineNET = new map<double, stat_stru_request_list_t>();
        break;

    case STAT_CACHE_COMP_CLEANSING_INI:
        if (m_pmmStatCleansingINI == NULL)
            m_pmmStatCleansingINI = new map<double, stat_stru_request_t>();
        break;

    case STAT_CACHE_COMP_SEND_NODE_INI:
        if (m_pmmStatSendNodeINI == NULL)
            m_pmmStatSendNodeINI  = new map<double, stat_stru_request_t>();
        break;

    case STAT_CACHE_COMP_SEND_NODE_NET:
        if (m_pmmStatSendNodeNET == NULL)
            m_pmmStatSendNodeNET = new map<double, stat_stru_request_t>();
        break;

    case STAT_CACHE_COMP_INCOMING_FIFO_INI:
        if (m_pmmStatIncomingFIFOINI == NULL)
            m_pmmStatIncomingFIFOINI = new map<double, stat_stru_size_t>();
        break;

    case STAT_CACHE_COMP_INCOMING_FIFO_NET:
        if (m_pmmStatIncomingFIFONET == NULL)
            m_pmmStatIncomingFIFONET = new map<double, stat_stru_size_t>();
        break;

    case STAT_CACHE_COMP_TEST:
        if (m_pmmStatTest == NULL)
            m_pmmStatTest = new map<double, stat_stru_size_t>();
            //m_pmmStatTest = new map<double, stat_stru_request_t>();
            //m_pmmStatTest = new map<double, stat_stru_request_list_t>();
        break;

    case STAT_CACHE_COMP_ALL:
        if (m_pmmStatProcessingINI == NULL)
            m_pmmStatProcessingINI  = new map<double, stat_stru_request_t>();
        if (m_pmmStatProcessingNET == NULL)
            m_pmmStatProcessingNET  = new map<double, stat_stru_request_t>();
        if (m_pmmStatPipelineINI == NULL)
            m_pmmStatPipelineINI= new map<double, stat_stru_request_list_t>();
        if (m_pmmStatPipelineNET == NULL)
            m_pmmStatPipelineNET= new map<double, stat_stru_request_list_t>();
        if (m_pmmStatSendNodeINI == NULL)
            m_pmmStatSendNodeINI  = new map<double, stat_stru_request_t>();
        if (m_pmmStatSendNodeNET == NULL)
            m_pmmStatSendNodeNET = new map<double, stat_stru_request_t>();
        if (m_pmmStatIncomingFIFOINI == NULL)
            m_pmmStatIncomingFIFOINI = new map<double, stat_stru_size_t>();
        if (m_pmmStatIncomingFIFONET == NULL)
            m_pmmStatIncomingFIFONET = new map<double, stat_stru_size_t>();
        break;

    default:
        cout << "warning: specified statistics not matching with the cache" << endl;
        break;
    }
}

void CacheL2TOK::Statistics(STAT_LEVEL lev)
{
    if (m_pmmStatRequestNo != NULL)
    {
        m_pmmStatRequestNo->insert(pair<double,stat_stru_size_t>(sc_time_stamp().to_seconds(),m_nStatReqNo));
    }

    if (m_pmmStatProcessingINI != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        stat_stru_request_t req;
        if (m_pStatCurReqINI == NULL)
        {
            req.valid = false;
        }
        else
        {
            req.valid = true;
            req.address = m_pStatCurReqINI->getreqaddress();
            req.offset = m_pStatCurReqINI->offset;
            req.size = m_pStatCurReqINI->nsize;
            req.type = m_pStatCurReqINI->type;
            req.ptr = m_pStatCurReqINI;

            m_pmmStatProcessingINI->insert(pair<double,stat_stru_request_t>(sc_time_stamp().to_seconds(),req));
        }
    }

    if (m_pmmStatProcessingNET != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        stat_stru_request_t req;
        if (m_pStatCurReqNET == NULL)
        {
            req.valid = false;
        }
        else
        {
            req.valid = true;
            req.address = m_pStatCurReqNET->getlineaddress();
            req.offset = m_pStatCurReqNET->offset;
            req.size = m_pStatCurReqNET->nsize;
            req.type = m_pStatCurReqNET->type;
            req.ptr = m_pStatCurReqNET;

            m_pmmStatProcessingNET->insert(pair<double,stat_stru_request_t>(sc_time_stamp().to_seconds(),req));
        }
    }

    if (m_pmmStatPipelineINI != NULL)
    {
        list<ST_request*>::iterator iter;
        stat_stru_request_list_t lstreq;

        stat_stru_request_t reqcur;
        if (m_pStatCurReqINI == NULL)
            reqcur.valid = false;
        else
        {
            reqcur.valid = true;
            reqcur.address = m_pStatCurReqINI->getlineaddress();
            reqcur.offset = m_pStatCurReqINI->offset;
            reqcur.size = m_pStatCurReqINI->nsize;
            reqcur.type = m_pStatCurReqINI->type;
            reqcur.ptr = m_pStatCurReqINI;
        }
        lstreq.push_back(reqcur);

        for (iter=m_pPipelineINI->m_lstRegisters.begin();iter!=m_pPipelineINI->m_lstRegisters.end();iter++)
        {
            ST_request* reqori = (*iter);

            stat_stru_request_t req;

            if (reqori == NULL)
            {
                req.valid = false;
            }
            else
            {
                req.valid = true;
                req.address = reqori->getlineaddress();
                req.offset = reqori->offset;
                req.size = reqori->nsize;
                req.type = reqori->type;
                req.ptr = reqori;
            }

            lstreq.push_back(req);
        }

        m_pmmStatPipelineINI->insert(pair<double, stat_stru_request_list_t>(sc_time_stamp().to_seconds(),lstreq));
    }

    if (m_pmmStatPipelineNET != NULL)
    {

    }

    if (m_pmmStatCleansingINI != NULL)
    {
        stat_stru_request_t req;
        if (m_bStatCleansingINI)
        {
            req.valid = true;
            req.address = m_pStatCurReqINI->getlineaddress();
            req.offset = m_pStatCurReqINI->offset;
            req.size = m_pStatCurReqINI->nsize;
            req.type = m_pStatCurReqINI->type;
            req.ptr = m_pStatCurReqINI;

            m_pmmStatCleansingINI->insert(pair<double,stat_stru_request_t>(sc_time_stamp().to_seconds(),req));
        }
    }

    if (m_pmmStatSendNodeINI != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        stat_stru_request_t req;
        if (m_pStatCurSendNodeINI == NULL)
        {
            req.valid = false;
        }
        else
        {
            req.valid = true;
            req.address = m_pStatCurSendNodeINI->getlineaddress();
            req.offset = m_pStatCurSendNodeINI->offset;
            req.size = m_pStatCurSendNodeINI->nsize;
            req.type = m_pStatCurSendNodeINI->type;
            req.ptr = m_pStatCurSendNodeINI->ref;

            delete m_pStatCurSendNodeINI;
            m_pStatCurSendNodeINI = NULL;

            m_pmmStatSendNodeINI->insert(pair<double,stat_stru_request_t>(sc_time_stamp().to_seconds(),req));
        }
    }

    if (m_pmmStatSendNodeNET != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        stat_stru_request_t req;
        if (m_pStatCurSendNodeNET == NULL)
        {
            req.valid = false;
        }
        else
        {
            req.valid = true;
            req.address = m_pStatCurSendNodeNET->getlineaddress();
            req.offset = m_pStatCurSendNodeNET->offset;
            req.size = m_pStatCurSendNodeNET->nsize;
            req.type = m_pStatCurSendNodeNET->type;
            req.ptr = m_pStatCurSendNodeNET->ref;

            delete m_pStatCurSendNodeNET;
            m_pStatCurSendNodeNET = NULL;

            m_pmmStatSendNodeNET->insert(pair<double, stat_stru_request_t>(sc_time_stamp().to_seconds(), req));
        }
    }

    if (m_pmmStatIncomingFIFOINI != NULL)
    {

    }

    if (m_pmmStatIncomingFIFONET != NULL)
    {

    }

    if (m_pmmStatTest != NULL)
    {

    }

}

void CacheL2TOK::DumpStatistics(ofstream& outfile, unsigned int component, unsigned int type)
{
    if (m_pmmStatRequestNo != NULL)
    {
        map<double, stat_stru_size_t>::iterator iter;

        for (iter=m_pmmStatRequestNo->begin();iter!=m_pmmStatRequestNo->end();iter++)
        {
            outfile << setw(10) << (*iter).first << "\t" << name() << "\t" << dec << (*iter).second << endl;
        }
    }

    if (m_pmmStatProcessingINI != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        for (iter=m_pmmStatProcessingINI->begin();iter!=m_pmmStatProcessingINI->end();iter++)
        {
            stat_stru_request_t req = (*iter).second;
            if (req.valid)
                outfile << "PrI\t" << setw(10) << (*iter).first << "\t" << req.ptr << "\t" << hex << req.address << dec << "\t" << req.type << "\t" << req.size << endl;
        }
    }

    if (m_pmmStatProcessingNET != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        for (iter=m_pmmStatProcessingNET->begin();iter!=m_pmmStatProcessingNET->end();iter++)
        {
            stat_stru_request_t req = (*iter).second;
            if (req.valid)
                outfile << "PrN\t" << setw(10) << (*iter).first << "\t" << req.ptr << "\t" << dec << req.address << "\t" << dec << req.type << "\t" << req.size << endl;
        }
    }

    if (m_pmmStatPipelineINI != NULL)
    {
        map<double, stat_stru_request_list_t>::iterator iter;

        for (iter=m_pmmStatPipelineINI->begin();iter!=m_pmmStatPipelineINI->end();iter++)
        {
            stat_stru_request_list_t& lst = (*iter).second;

            // time
            outfile << "PiI\t" << setw(10) << (*iter).first << "\t";
            
            // number of used registers
            unsigned int used = 0;
            for (unsigned int i=0;i<lst.size();i++)
            {
                if (lst[i].valid)
                    used ++;
            }
            outfile << dec << used << "\t";

            // output request address and type
            for (unsigned int i=0;i<lst.size();i++)
            {
                if (lst[i].valid)
                    outfile << dec << lst[i].ptr << "\t" << lst[i].address << "\t" << lst[i].type << "\t";
                else
                    outfile << dec << "X\tX\tX\t";
            }

            outfile << endl;
        }
    }

    if (m_pmmStatPipelineNET != NULL)
    {

    }

    if (m_pmmStatCleansingINI != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        for (iter=m_pmmStatCleansingINI->begin();iter!=m_pmmStatCleansingINI->end();iter++)
        {
            stat_stru_request_t req = (*iter).second;
            if (req.valid)
                outfile << "Cle\t" << setw(10) << (*iter).first << "\t" << req.ptr << "\t" << dec << req.address << "\t" << dec << req.type << "\t" << req.size << endl;
        }
    }

    if (m_pmmStatSendNodeINI != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        for (iter=m_pmmStatSendNodeINI->begin();iter!=m_pmmStatSendNodeINI->end();iter++)
        {
            stat_stru_request_t req = (*iter).second;
            if (req.valid)
                outfile << "SnI-" << name() << "\t" << setw(10) << (*iter).first << "\t" << hex << req.address << "\t" << dec << req.type << "\t" << req.size << "\t" << req.ptr << endl;
        }
    }

    if (m_pmmStatSendNodeNET != NULL)
    {
        map<double, stat_stru_request_t>::iterator iter;

        for (iter=m_pmmStatSendNodeNET->begin();iter!=m_pmmStatSendNodeNET->end();iter++)
        {
            stat_stru_request_t req = (*iter).second;
            if (req.valid)
                outfile << "SnN-" << name() << "\t" << setw(10) << (*iter).first << "\t" << hex << req.address << "\t" << dec << req.type << "\t" << req.size << "\t" << req.ptr << endl;
        }
    }

    if (m_pmmStatIncomingFIFOINI != NULL)
    {

    }

    if (m_pmmStatIncomingFIFONET != NULL)
    {

    }

    if (m_pmmStatTest != NULL)
    {
        //map<double, stat_stru_request_t>::iterator iter;
        map<double, stat_stru_size_t>::iterator iter;

        for (iter=m_pmmStatTest->begin();iter!=m_pmmStatTest->end();iter++)
        {
            //if (req.valid)
                outfile << "Tes\t" << setw(10) << (*iter).first << "\t" << dec << (*iter).second << endl;
        }
    }

}
#endif

#endif  // token coherence

