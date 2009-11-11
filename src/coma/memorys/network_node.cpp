#include "network_node.h"
using namespace MemSim;

// forward transaction to the next node
void Network_Node::SendRequestINI()
{
    // forward transaction to the next node
    if (!m_fifoNetOut.nb_write(m_pReqCurINI))
    {
LOG_VERBOSE_BEGIN(VERBOSE_MOST)
	    clog << LOG_HEAD_OUTPUT << "transaction sent failed. network congestion, try again next cycle" << endl;
LOG_VERBOSE_END

        m_nStateINI = STATE_INI_CONGEST;
        return;
    }

LOG_VERBOSE_BEGIN(VERBOSE_MOST)
	clog << LOG_HEAD_OUTPUT << "transaction forwarded to the next node" << endl;
LOG_VERBOSE_END

    // set the state back
    m_nStateINI = STATE_INI_AVAILABLE;
}

void Network_Node::BehaviorIni()
{
    switch (m_nStateINI)
    {
    case STATE_INI_AVAILABLE:
        // check whether there are available request
        if (m_fifoIn.num_available_fast() <= 0)
            return;

        if (!m_fifoIn.nb_read(m_pReqCurINI))
        {
            cerr << ERR_HEAD_OUTPUT << "should not get failed in this nb_read request" << endl;
            return;
        }

LOG_VERBOSE_BEGIN(VERBOSE_MOST)
    	clog << LOG_HEAD_OUTPUT << "INI REQ in " << FMT_ADDR(m_pReqCurINI->getreqaddress()) << endl;
	    clog << "\t"; print_request(m_pReqCurINI);
LOG_VERBOSE_END

        // clog << "----------------------------------------------------" << endl;
        // delay alt

        // handle request
        // network node doesn't handle requests

        // try to send the request 
        SendRequestINI();
        break;

    case STATE_INI_CONGEST:
        // try to send the request 
        SendRequestINI();
        break;
    default:
        break;
    }
}

// forward directly to the next node
void Network_Node::SendRequestPASNET()
{
    // forward directly
    if (!m_fifoNetOut.nb_write(m_pReqCurPAS))
    {
LOG_VERBOSE_BEGIN(VERBOSE_MOST)
	    clog << LOG_HEAD_OUTPUT << "transaction sent failed. network congestion, try again next cycle" << endl;
LOG_VERBOSE_END

        m_nStatePAS |= STATE_PAS_CONGEST_NET;
        return;
    }

LOG_VERBOSE_BEGIN(VERBOSE_MOST)
    clog << LOG_HEAD_OUTPUT << "transaction sent to the next node." << endl;
LOG_VERBOSE_END

    if ((m_nStatePAS & STATE_PAS_CONGEST_NET) != 0)
        m_nStatePAS ^= STATE_PAS_CONGEST_NET;

    if ( (m_nStatePAS != STATE_PAS_AVAILABLE) && (m_nStatePAS != STATE_PAS_CONGEST_NODE) )
    {
        cerr << ERR_HEAD_OUTPUT << "wrong state detected" << endl;
    }
}

void Network_Node::SendRequestPASNODE()
{
/*
    // send the request to cache or director
    if (!m_fifoOut.nb_write(m_pReqCurPAS))
    {
LOG_VERBOSE_BEGIN(VERBOSE_MOST)
	    clog << LOG_HEAD_OUTPUT << "transaction sent failed. cache/directory buffer is full, try again next cycle" << endl;
LOG_VERBOSE_END

        m_nStatePAS |= STATE_PAS_CONGEST_NODE;
        return;
    }
*/
LOG_VERBOSE_BEGIN(VERBOSE_MOST)
    clog << LOG_HEAD_OUTPUT << "transaction passed to cachel2/memory" << endl;
LOG_VERBOSE_END

    if ((m_nStatePAS & STATE_PAS_CONGEST_NODE) != 0)
        m_nStatePAS ^= STATE_PAS_CONGEST_NODE;

    if ( (m_nStatePAS != STATE_PAS_AVAILABLE)/* || (m_nStatePAS != STATE_PAS_CONGEST_NODE) */)
    {
        cerr << ERR_HEAD_OUTPUT << "wrong state detected " << m_nStatePAS << endl;
    }
}

void Network_Node::BehaviorNet()
{
    switch (m_nStatePAS)
    {
    case STATE_PAS_AVAILABLE:
        // check whether there are any request available
        if (m_fifoNetIn.num_available_fast() <= 0)
        {
            return;
        }

        // try to get the next request
        if (!m_fifoNetIn.nb_read(m_pReqCurPAS))
        {
            cerr << ERR_HEAD_OUTPUT << "should not get failed in this nb_read request" << endl;
            return;    
        }

LOG_VERBOSE_BEGIN(VERBOSE_MOST)
		clog << LOG_HEAD_OUTPUT << "NET REQ in " << FMT_ADDR(m_pReqCurPAS->getreqaddress()) << endl;
		clog << "\t"; print_request(m_pReqCurPAS);
LOG_VERBOSE_END

        // delay alt
        // ** delay might vary according to different requests

        // handle request
        // issues to handle:
        // 0. if the request doesn't belong to the directory which hash function allows, it will pass the request and return;
        // 1. Should I pass the request to the next node
        // 2. Should I send this request to the L2 Cache

        // see whether directory hash function allows
        //if (m_pCache->SkipDirecotry())
        //{
        //	m_bBusyNet = false;
        //	wait(port_clk.posedge_event());
        //	continue
        //}

        // pass the request to cachel2
        if (m_pCache->DirectForward(m_pReqCurPAS))
        {
            // DIRECT_FORWARD alert alert
            //// forward transaction
            //SendRequestPASNET();
        }

        if (m_pCache->MayHandle(m_pReqCurPAS))
        {
            // pass the transaction to cache/directory
            SendRequestPASNODE();
        }
        break;

    // try to send the request again to next node
    case STATE_PAS_CONGEST_NET:
        SendRequestPASNET();
        if (m_nStatePAS == STATE_PAS_CONGEST_NODE)
            SendRequestPASNODE();
        break;

    // try to send the request again to cache/directory
    case STATE_PAS_CONGEST_NODE:
        SendRequestPASNODE();
        break;

    default:
        break;
    }
}

