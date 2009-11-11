#ifndef _SIM_CONTROL_H
#define _SIM_CONTROL_H

#include <ios>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdlib>

#include "stat_def.h"

#include "dcswitch.h"

using namespace std;

namespace MemSim{
//{ memory simulator namespace


#ifdef MEM_ENABLE_SIM_LOG
    #define LOG_VERBOSE_BEGIN_OBJ(psimobj, verbose) if (psimobj->m_nVerbose >= verbose){\
                                                    streambuf* buf = clog.rdbuf();\
                                                    if (psimobj->m_ll == psimobj->LOG_GLOBAL)\
                                                    {\
                                                        clog.rdbuf(psimobj->logbufGlobal);\
                                                    }\
                                                    else if (psimobj->m_ll == psimobj->LOG_LOCAL)\
                                                    {\
                                                        clog.rdbuf(psimobj->m_logbufLocal);\
                                                    }
#else
    #define LOG_VERBOSE_BEGIN_OBJ(psimobj, verbose) if (false){
#endif


#ifdef MEM_ENABLE_SIM_LOG
    #define LOG_VERBOSE_BEGIN(verbose)              if (m_nVerbose >= verbose){\
                                                    streambuf* buf = clog.rdbuf();\
                                                    if (m_ll == LOG_GLOBAL)\
                                                    {\
                                                        clog.rdbuf(logbufGlobal);\
                                                    }\
                                                    else if (m_ll == LOG_LOCAL)\
                                                    {\
                                                        clog.rdbuf(m_logbufLocal);\
                                                    }
#else
    #define LOG_VERBOSE_BEGIN(verbose)              if (false){
#endif
                                    
#ifdef MEM_ENABLE_SIM_LOG
    #define LOG_VERBOSE_END                             clog.flush();\
                                                        clog.rdbuf(buf);\
                                                    }
#else
    #define LOG_VERBOSE_END                         }
#endif


#define VERBOSE_DEFAULT                 VERBOSE_DETAIL

#ifdef WIN32
extern class BusST_Master;
extern class RingNode;
#else
class BusST_Master;
class RingNode;
#endif

class SimObj
{
public:
	static streambuf* logbufGlobal;
protected:
	static ofstream* ofsGlobal;
	static streambuf* clogbufDefault;

    bool m_bAssoModule;

    static const unsigned int DEFAULT_TOPOLOGY_ID = 0xffff;
public:
	enum LOG_LOCATION{
		LOG_CONSOLE,	// output to console
		LOG_LOCAL,		// output to local logfile
		LOG_GLOBAL		// output to global logfile
	};

	enum VERBOSE_LEVEL{
		VERBOSE_ALL = 5,       // MOST + DELAY SYMBOL + DATA VERIFY
		VERBOSE_MOST = 4,      // DETAIL + ALL BUS/NODE INFO AND DETAILED DEBUG INFO + ALL TRANSACTIONS ARRIVES AT EVERYWHERE
		VERBOSE_DETAIL = 3,    // STATE + ALL TRANSACTION ARRIVES AT CACHEES AND DIRECTORIES + DATA VERIFY FAILED
		VERBOSE_STATE = 2,     // BASICS + STATE TRANSITION AND BUS WRITE-BACK + QUEUE AND ACTIVATE
		VERBOSE_BASICS = 1,    // BASIC INFORMATION: HIT/MISS + MAJOR TRANSACTIONS BEING PROCESSED
		VERBOSE_NONE = 0       // NO DEBUG INFO
	};

    enum REVIEW_LEVEL{
        SO_REVIEW_ALL = 4,       // Review everything
        SO_REVIEW_DETAIL = 3,    // Review details with data with detail information in queue
        SO_REVIEW_NORMAL = 2,    // Review details with data with general info in queue
        SO_REVIEW_BASICS = 1,    // Review only the basic info
        SO_REVIEW_NONE = 0       // Nothing will be reviewed
    };

    enum STAT_LEVEL{            // Statistics level
        SO_STAT_NONE = 0,
        SO_STAT_ALL
    };

    //typedef struct __stat_entry_t{
    //    double          time;       // time stamp of the event
    //    unsigned int    component;  // component
    //    void *          data;       // detailed data of the event
    //} stat_event_t;

	VERBOSE_LEVEL m_nVerbose;
	streambuf* m_logbufLocal;
	ofstream* m_ofsLocal;
	LOG_LOCATION m_ll;

    static vector<SimObj*> s_vecObj;

protected:
    // object name
    char* m_pName;

    // potential bus master or ring node
//    void* m_pBusOrNode;
    void* m_pBus;
    void* m_pNode;

    // Topology ID, used to determine the topology relation with other components
    unsigned int m_nTopologyID;

public:
    SimObj(){
        m_pName = NULL;
        m_nVerbose = VERBOSE_ALL;
        m_logbufLocal = NULL;
        m_ofsLocal = NULL;
        m_ll = LOG_CONSOLE;
        m_bAssoModule = false;

        // save the object into vector containnig all the objects
        s_vecObj.push_back(this);

        m_nTopologyID = DEFAULT_TOPOLOGY_ID;
    }

	SimObj(const char* pname){
		m_nVerbose = VERBOSE_ALL;
		m_logbufLocal = NULL;
		m_ofsLocal = NULL;
		m_ll = LOG_CONSOLE;
        m_bAssoModule = false;

        // save the object name
		if (pname!=NULL)
		{
			m_pName = (char*)malloc(strlen(pname)+1);
			strcpy(m_pName, pname);
		}
		else
			assert(false);

        // save the object into all-object vector
        s_vecObj.push_back(this);

        m_nTopologyID = DEFAULT_TOPOLOGY_ID;
	}

    virtual ~SimObj()
    {
        // can be done in a global shot ***
        //if (clogbufDefault != NULL)
        //{
        //	clog.rdbuf(clogbufDefault);
        //}

        if (m_pName != NULL)
            free(m_pName);
        delete m_ofsLocal;
    }

	virtual void InitializeLog(const char* logName, LOG_LOCATION ll, VERBOSE_LEVEL verbose = VERBOSE_DEFAULT)
	{
		m_ll = ll;
        m_nVerbose = verbose;
		switch (ll)
		{
		case LOG_CONSOLE:
			if (clogbufDefault != NULL)
				clog.rdbuf(clogbufDefault);
			break;
		case LOG_LOCAL:
			if (logName == NULL)
			{
				m_ll = LOG_CONSOLE;
			}
			else
			{
				m_ofsLocal = new ofstream(logName);
				m_logbufLocal = m_ofsLocal->rdbuf();
			}
			break;
		case LOG_GLOBAL:
			// globals should be initialized outside
            if ((ofsGlobal == NULL) || (logbufGlobal == NULL))
            {
                m_ll = LOG_CONSOLE;
            }
			break;
		default:
			m_ll = LOG_CONSOLE;
			break;
		}

	}

    void SetTopologyID(unsigned int topid)
    {
        m_nTopologyID = topid;
    }

    unsigned int GetTopologyID()
    {
        return m_nTopologyID;
    }

    void SetVerbose(int nlevel)
    {
        if (nlevel > VERBOSE_ALL)
            m_nVerbose = VERBOSE_ALL;
        else if (nlevel < VERBOSE_NONE)
            m_nVerbose = VERBOSE_NONE;
        else
            m_nVerbose = (VERBOSE_LEVEL)nlevel;
    }

    void SetVerbose(VERBOSE_LEVEL verbose)
	{
		m_nVerbose = verbose;
	}

    static void SetGlobalLog(const char * logName)
    {
        SimObj::ofsGlobal = new ofstream(logName);
        SimObj::logbufGlobal  = SimObj::ofsGlobal->rdbuf();
    }

    const char* GetObjectName()
    {
        return m_pName;
    }

    void SetBusMaster(BusST_Master* pbus)
    {
        //m_pBusOrNode = (void*)pbus;
        m_pBus = (void*)pbus;
    }

    void SetRingNode(RingNode* pnode)
    {
        //m_pBusOrNode = (void*)pnode;
        m_pNode = (void*)pnode;
    }

    BusST_Master* GetBusMaster()
    {
        //return (BusST_Master*)m_pBusOrNode;
        return (BusST_Master*)m_pBus;
    }

    RingNode* GetRingNode()
    {
        //return (RingNode*)m_pBusOrNode;
        return (RingNode*)m_pNode;
    }

    virtual void ReviewState(REVIEW_LEVEL rev){};

#ifdef MEM_MODULE_STATISTICS
    virtual void InitializeStatistics(unsigned int components){};
    virtual void Statistics(STAT_LEVEL lev){};

    virtual void DumpStatistics(ofstream& outfile, unsigned int component, unsigned int type){};
#endif

    bool IsAssoModule(){return m_bAssoModule;};
};

//} memory simulator namespace
}

#endif
