#ifndef _MEMORYBANKST_H
#define _MEMORYBANKST_H

#include "predef.h"
#include "membankst_if.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class MemoryBankST : public sc_module, public MemoryState, public MemBank_if, virtual public SimObj
{
public:
	sc_in<bool> port_clk;
protected:
	unsigned int	m_nBankID;

	// maybe include size
	__address_t	m_nSize;

	// delay
	sc_time m_tLatency;

	//sc_fifo<ST_request*> *m_pfifoReqIn;
	sc_fifo<ST_request*> *m_pfifoReply;

#ifdef SIMULATE_DATA_TRANSACTION
    MemoryDataContainer *m_pMemoryDataContainer;
#endif


public:
	SC_HAS_PROCESS(MemoryBankST);

    MemoryBankST(sc_module_name nm, sc_fifo<ST_request*> *pfifoReply
#ifdef SIMULATE_DATA_TRANSACTION
        , MemoryDataContainer *pMemoryDataContainer
#endif
        , sc_time delay = sc_time(50, SC_NS))
        : sc_module(nm), m_pfifoReply(pfifoReply), m_tLatency(delay)
#ifdef SIMULATE_DATA_TRANSACTION
        , m_pMemoryDataContainer(pMemoryDataContainer)
#endif
    {
        SC_THREAD(Behavior);
        sensitive << port_clk.pos();
        dont_initialize();

        // initialization
        //m_pfifoReqIn = new sc_fifo<ST_request*>();
    }

	void Behavior()
	{
		while(true)
		{
			if (m_pfifoReqIn->num_available_fast() <= 0)
			{
				wait(port_clk.posedge_event());
				continue;
			}

			//wait(SC_ZERO_TIME);
			//wait(m_pfifoReqIn->data_written_event());
			ST_request *req = m_pfifoReqIn->read();

LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
            clog << LOG_HEAD_OUTPUT << "request in " << FMT_ADDR(req->address) << endl;
            clog << "\t"; print_request(req);
LOG_VERBOSE_END

			// delay 
			// ** delay should be decided upon the size and request type
			wait(m_tLatency);

			// handle request
			switch (req->type)
			{
			case MemoryState::REQUEST_READ:
			case MemoryState::REQUEST_READ_REDIRECT:
			case MemoryState::REQUEST_REMOTE_READ_SH:	// newly added
            case MemoryState::REQUEST_REMOTE_READ_EX:	// newly added
				// cout << "|||||||||||||||||||||||||||||||||||||||||||||||||||||" << endl;
				FunRead(req);
				break;
			case MemoryState::REQUEST_WRITE:
			case MemoryState::REQUEST_WRITE_REDIRECT:
            case MemoryState::REQUEST_WRITE_BACK:
				FunWrite(req);
				break;
			default:
				break;
			}

            // think twice alert
            if (req->type != MemoryState::REQUEST_WRITE_BACK)
            {
			    // send reply transaction
			    m_pfifoReply->write(req);
LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
			clog << LOG_HEAD_OUTPUT << "feedback to controller" << endl;
LOG_VERBOSE_END
            }

			
			wait(port_clk.posedge_event());
		}
	}


    void FunRead(ST_request* req)
    {
        // for (int...)
        // set all the data
        //req->data[0] = req->address;
        //generate_random_data(req->data, req->size);
#ifdef SIMULATE_DATA_TRANSACTION
        m_pMemoryDataContainer->FetchMemData(req->address, req->size*sizeof(UINT32), (char*)req->data);     // 32 bit alert
#endif

        req->type = MemoryState::REQUEST_READ_REPLY;

LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "read done address " << FMT_ADDR(req->address) << ", " << FMT_DTA(req->data[0]) << "returned." << endl;
LOG_VERBOSE_END
    }


    void FunWrite(ST_request* req)
    {
        req->type = MemoryState::REQUEST_WRITE_REPLY;

#ifdef SIMULATE_DATA_TRANSACTION
        m_pMemoryDataContainer->Verify(req->address, req->size*sizeof(UINT32), (char*)req->data);     // 32 bit alert
#endif

LOG_VERBOSE_BEGIN(VERBOSE_MOST)
        clog << LOG_HEAD_OUTPUT << "write done address " << FMT_ADDR(req->address) << " with " << FMT_DTA(req->data[0]) << endl;
LOG_VERBOSE_END
    }

//	bool request(ST_request *req)
//	{
//		m_pfifoReqIn->write(req);
//LOG_VERBOSE_BEGIN(VERBOSE_DETAIL)
//		clog << LOG_HEAD_OUTPUT << "request received"  << endl;
//LOG_VERBOSE_END
//		return true;
//	}

};

//////////////////////////////
//} memory simulator namespace
}

#endif
