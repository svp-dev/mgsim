#include "linkmgs.h"
#include "../memorys/simcontrol.h"
#include "../memorys/predef.h"
#include "../simlink/memorydatacontainer.h"
#include "../memorys/cachel2tok.h"
#include <stdio.h>

#define CHECKCM_ERROR_NONE  0
#define CHECKCM_ERROR_LOCKINGSTATEINCACHE  1
#define CHECKCM_ERROR_INCONSISTENCY_SS  2
#define CHECKCM_ERROR_INCONSISTENCY_SO  3                       // inconsistency with owner
#define CHECKCM_ERROR_INCONSISTENCY_DOUBLEOWNER  4              // DOUBLE OWNED OR MODIFIED
#define CHECKCM_ERROR_UNKNOWN  0XFF

namespace MemSim{
    unsigned int g_uMemoryAccessesL = 0;
    unsigned int g_uMemoryAccessesS = 0;
}


void setverboselevel(int nlevel)
{
    vector<SimObj*>::iterator iter;
    for (iter=g_vecAllSimObjs.begin();iter!=g_vecAllSimObjs.end();iter++)
    {
        SimObj* pobj = *iter;
        pobj->SetVerbose(nlevel);
    }
}




//////////////////////////////////////////////////////////////////////////
// memory related procedures
// for instance memory dumping and memory check procedures
// both will use a memory map, which contains a table with two fields
// the table is addressed by cachelines, 
// the whole address space allowed within the range is partitioned into many fields in table
// each column is corresponding to a cacheline. 
// each column contains two fields, one is data, another is states.
// it's like a directly mapped cache in the scale of the whole memory range.
// the table is implemented with two arrays, one with cache data, another with only line states.

// * currently the implementation only allows (MEMORYSIZE%CACHELINESIZE == 0)


// allocate a memory block and allocate state array. 
// function fail if either of the two pointers returned are NULL
void allocatememorytable(char* &data, char* &state)
{
    unsigned __int64 memorysize = LinkMGS::s_oLinkConfig.m_nMemorySize;
    unsigned int linesize = LinkMGS::s_oLinkConfig.m_nLineSize;

    if (memorysize%linesize != 0) 
    {
        data = NULL;
        state = NULL;
        return;
    }

    // allocate data
    data = (char*)malloc(memorysize);
        
    // allocate state array
    state = (char*)malloc(memorysize/linesize);
}

#ifdef TOKEN_COHERENCE
// return error code for inconsistency or other stuff.
// 0xff      : failed to get memory data
// 0x01 bit  : hitonmsb
// 0x02 bit  : lockonmsb
unsigned int gatherlinevalue(unsigned __int64 addr, char* linedata)
{
    unsigned int linesize = LinkMGS::s_oLinkConfig.m_nLineSize;
    unsigned int nlinebit = lg2(linesize);

    unsigned __int64 addrline = addr >> nlinebit << nlinebit;

    if (!MemoryDataContainer::s_pDataContainer->Fetch(addrline, linesize, linedata))
    {
        cout << "failed to acquire data from the data container." << endl;
        return 0xff;
    }

    unsigned int ret = 0;

    // update the value from the cache
    vector<CacheL2TOK*>::iterator iter;
    for (iter=CacheL2TOK::s_vecPtrCaches.begin();iter!=CacheL2TOK::s_vecPtrCaches.end();iter++)
    {
        CacheL2TOK* pcache = *iter;

        cache_line_t* line = pcache->LocateLineEx(addr);

        if (line == NULL)
            continue;

        // check whether it's a hit on the MSB
        //bool hitonmsb = pcache->m_msbModule.IsAddressPresent(addr);
        //ret |= hitonmsb;

        // check whether the line is already locked
        //bool lockonmsb = hitonmsb?pcache->m_msbModule.IsSlotLocked(addr, NULL):false;
        //ret |= (lockonmsb < 1);

        if (line->gettokenglobalvisible() > 0)
        {
            // copy data
            memcpy(linedata, line->data, linesize);
        }
    }
    
    return ret;
}

// check only L2 caches
bool checkcacheandmemory(int& errorcode, char* &pmemdata, char* &pstate, bool bIgnoreLockingStates = false)
{
    // first allocate a memory block 
    allocatememorytable(pmemdata, pstate);

    unsigned __int64 memorysize = LinkMGS::s_oLinkConfig.m_nMemorySize;
    unsigned int linesize = LinkMGS::s_oLinkConfig.m_nLineSize;
    unsigned int nlinebit = lg2(linesize);
    unsigned __int64 startaddress = LinkMGS::s_oLinkConfig.m_nStartingAddress;

    enum CM_STATE{
        CM_STATE_BC,        // data in backing store
        CM_STATE_MO,        // data in modified or owned
        CM_STATE_SH,        // data in shared
    };

    if ((pmemdata == NULL) || (pstate == NULL))
    {
        free(pmemdata);
        free(pstate);
        return false;
    }

    // copy the data of the memory all together to the allocated space. 
    if (!MemoryDataContainer::s_pDataContainer->Fetch(LinkMGS::s_oLinkConfig.m_nStartingAddress, memorysize, pmemdata))
    {
        cout << "failed to copy data from memory data container." << endl;
        free(pmemdata);
        free(pstate);
        return false;
    }

    // initialize the state as BC
    for (unsigned int i=0;i<memorysize/linesize;i++)
        pstate[i] = (char)CM_STATE_BC;

    // get associativity and set number
    unsigned int associativity = LinkMGS::s_oLinkConfig.m_nCacheAssociativity;
    unsigned int nset = LinkMGS::s_oLinkConfig.m_nCacheSet;
    unsigned int nsetbit = lg2(nset);

    // start checking the caches
    // any cachelines in locking state will be regarded as an error 
    vector<CacheL2TOK*>::iterator iter;
    for (iter=CacheL2TOK::s_vecPtrCaches.begin();iter!=CacheL2TOK::s_vecPtrCaches.end();iter++)
    {
        CacheL2TOK* pcache = *iter;

        for (unsigned int i=0;i<nset;i++)
        {
            // get the lines in a set then checking each cachelines inside
            cache_line_t* plines = pcache->GetSetLines(i);

            // check the cachelines
            for (unsigned int j=0;j<associativity;j++)
            {
                // get the line
                cache_line_t* pline = &plines[j];

                // get the address of the line
                __address_t lineaddr = pline->getlineaddress(i, nsetbit);

                // get the offset of this address in the memory table
                unsigned __int64 taboffset = (lineaddr - startaddress) >> nlinebit;

                // compare the state of the line in cache and in memory table
                bool berror = false;

                // get the line data and state associated with certain address
                char* curstate = &pstate[taboffset];
                char* curdata = &pmemdata[(unsigned __int64)taboffset*linesize];

                switch (pline->state)
                {
                case CacheState::CLS_INVALID:
                    // invalid line will not update memory table
                    break;

                case CacheState::CLS_SHARER:
                    if (!pline->pending)    // SHARED
                    {
                        // shared might have different data with the main memory
                        // compare and update the memory data and state
                        assert(!pline->invalidated);

                        if (*curstate == (char)CM_STATE_BC)
                        {
                            // update
                            *curstate = CM_STATE_SH;
                            memcpy(curdata, pline->data, linesize);
                        }
                        else if (pstate[taboffset] == (char)CM_STATE_SH)
                        {
                            // compare data
                            if (memcmp(curdata, pline->data, linesize) == 0)
                            {
                                // doing nothing
                            }
                            else
                            {
                                berror = 1;
                                errorcode = CHECKCM_ERROR_INCONSISTENCY_SS;
                            }
                        }
                        else if (pstate[taboffset] == (char)CM_STATE_MO)
                        {
                            if (memcmp(curdata, pline->data, linesize) == 0)
                            {
                                // doing nothing
                            }
                            else
                            {
                                berror = 1;
                                errorcode = CHECKCM_ERROR_INCONSISTENCY_SO;
                            }
                        }
                        else
                        {
                            berror = 1;
                            errorcode = CHECKCM_ERROR_UNKNOWN;
                        }
                    }
                    // pending lines
                    else if (!pline->invalidated)    // READPENDING
                    {
                        if (bIgnoreLockingStates)
                        {
                            // do something
                        }
                        else
                        {
                            berror = true;
                            errorcode = CHECKCM_ERROR_LOCKINGSTATEINCACHE;
                        }
                    }
                    else    // invalidated pending lines    // READPENDINGI
                    {
                        if (bIgnoreLockingStates)
                        {
                            // do something
                        }
                        else
                        {
                            berror = true;
                            errorcode = CHECKCM_ERROR_LOCKINGSTATEINCACHE;
                        }
                    }

                    break;

                case CacheState::CLS_OWNER:
                    if (!pline->pending)    // MODIFIED or OWNED
                    {
                        if (pline->tokencount == CacheState::GetTotalTokenNum())    // MODIFIED
                        {
                            // modified might have different data with the main memory
                            // compare and update data and state

                            if (*curstate == (char)CM_STATE_BC)
                            {
                                // update
                                *curstate = CM_STATE_MO;
                                memcpy(curdata, pline->data, linesize);
                            }
                            else if (pstate[taboffset] == (char)CM_STATE_SH)
                            {
                                // compare data
                                if (memcmp(curdata, pline->data, linesize) == 0)
                                {
                                    // update
                                    *curstate = CM_STATE_MO;
                                }
                                else
                                {
                                    berror = 1;
                                    errorcode = CHECKCM_ERROR_INCONSISTENCY_SO;
                                }
                            }
                            else if (pstate[taboffset] == (char)CM_STATE_MO)
                            {
                                berror = 1;
                                errorcode = CHECKCM_ERROR_INCONSISTENCY_DOUBLEOWNER;
                            }
                            else
                            {
                                berror = 1;
                                errorcode = CHECKCM_ERROR_UNKNOWN;
                            }
                        }
                        else    // OWNED
                        {
                            // owned might have different data
                            // compare and update

                            if (*curstate == (char)CM_STATE_BC)
                            {
                                // update
                                *curstate = CM_STATE_MO;
                                memcpy(curdata, pline->data, linesize);
                            }
                            else if (pstate[taboffset] == (char)CM_STATE_SH)
                            {
                                // compare data
                                if (memcmp(curdata, pline->data, linesize) == 0)
                                {
                                    // update
                                    *curstate = CM_STATE_MO;
                                }
                                else
                                {
                                    berror = 1;
                                    errorcode = CHECKCM_ERROR_INCONSISTENCY_SO;
                                }
                            }
                            else if (pstate[taboffset] == (char)CM_STATE_MO)
                            {
                                berror = 1;
                                errorcode = CHECKCM_ERROR_INCONSISTENCY_DOUBLEOWNER;
                            }
                            else
                            {
                                berror = 1;
                                errorcode = CHECKCM_ERROR_UNKNOWN;
                            }
                        }
                    }
                    else if (!pline->invalidated)    // WRITEPENDINGM, WRITEPENDINGI
                    {
                        if (pline->tokencount == CacheState::GetTotalTokenNum())    // WPM
                        {
                            if (bIgnoreLockingStates)
                            {
                                // do something
                            }
                            else
                            {
                                berror = true;
                                errorcode = CHECKCM_ERROR_LOCKINGSTATEINCACHE;
                            }
                        }
                        else    // WPI
                        {
                            if (bIgnoreLockingStates)
                            {
                                // do something
                            }
                            else
                            {
                                berror = true;
                                errorcode = CHECKCM_ERROR_LOCKINGSTATEINCACHE;
                            }
                        }
                    }
                    else    // WRITEPENDINGE
                    {
                        if (bIgnoreLockingStates)
                        {
                            // do something
                        }
                        else
                        {
                            berror = true;
                            errorcode = CHECKCM_ERROR_LOCKINGSTATEINCACHE;
                        }
                    }
                    break;

                default:
                    break;
                }

                // error report
                if (berror)
                {
                    switch (errorcode)
                    {
                    case CHECKCM_ERROR_LOCKINGSTATEINCACHE:
                        cout << "line@address " << hex << lineaddr << "-" << lineaddr+linesize << dec << " at locking state" << endl;
                        break;

                    case CHECKCM_ERROR_INCONSISTENCY_SS:
                        cout << "line@address " << hex << lineaddr << "-" << lineaddr+linesize << dec << " S-S inconsistency" << endl;
                        break;

                    case CHECKCM_ERROR_INCONSISTENCY_SO:
                        cout << "line@address " << hex << lineaddr << "-" << lineaddr+linesize << dec << " S-O inconsistency" << endl;
                        break;

                    case CHECKCM_ERROR_INCONSISTENCY_DOUBLEOWNER:
                        cout << "line@address " << hex << lineaddr << "-" << lineaddr+linesize << dec << " Double Owner" << endl;
                        break;

                    case CHECKCM_ERROR_UNKNOWN:
                        cout << "line@address " << hex << lineaddr << "-" << lineaddr+linesize << dec << " unknow error" << endl;
                        break;

                    default:
                        free(pmemdata);
                        free(pstate);
                        return false;
                    }
                }

            }
        }

    }

    return true;
}

#else

// check only L2 caches
bool checkcacheandmemory(int& errorcode, char* &pmemdata, char* &pstate, bool bIgnoreLockingStates = false)
{
    // first allocate a memory block 
    allocatememorytable(pmemdata, pstate);

    unsigned __int64 memorysize = LinkMGS::s_oLinkConfig.m_nMemorySize;
    unsigned int linesize = LinkMGS::s_oLinkConfig.m_nLineSize;
    unsigned int nlinebit = lg2(linesize);
    unsigned __int64 startaddress = LinkMGS::s_oLinkConfig.m_nStartingAddress;

    enum CM_STATE{
        CM_STATE_BC,        // data in backing store
        CM_STATE_MO,        // data in modified or owned
        CM_STATE_SH,        // data in shared
    };

    if ((pmemdata == NULL) || (pstate == NULL))
    {
        free(pmemdata);
        free(pstate);
        return false;
    }

    // copy the data of the memory all together to the allocated space. 
    if (!MemoryDataContainer::s_pDataContainer->Fetch(LinkMGS::s_oLinkConfig.m_nStartingAddress, memorysize, pmemdata))
    {
        cout << "failed to copy data from memory data container." << endl;
        free(pmemdata);
        free(pstate);
        return false;
    }

    // initialize the state as BC
    for (unsigned int i=0;i<memorysize/linesize;i++)
        pstate[i] = (char)CM_STATE_BC;

    // get associativity and set number
    unsigned int associativity = LinkMGS::s_oLinkConfig.m_nCacheAssociativity;
    unsigned int nset = LinkMGS::s_oLinkConfig.m_nCacheSet;
    unsigned int nsetbit = lg2(nset);

    // start checking the caches
    // any cachelines in locking state will be regarded as an error 
    vector<CacheL2ST*>::iterator iter;
    for (iter=CacheL2ST::s_vecPtrCaches.begin();iter!=CacheL2ST::s_vecPtrCaches.end();iter++)
    {
        CacheL2ST* pcache = *iter;

        for (unsigned int i=0;i<nset;i++)
        {
            // get the lines in a set then checking each cachelines inside
            cache_line_t* plines = pcache->GetSetLines(i);

            // check the cachelines
            for (unsigned int j=0;j<associativity;j++)
            {
                // get the line
                cache_line_t* pline = &plines[j];

                // get the address of the line
                __address_t lineaddr = pline->getlineaddress(i, nsetbit);

                // get the offset of this address in the memory table
                unsigned __int64 taboffset = (lineaddr - startaddress) >> nlinebit;

                // compare the state of the line in cache and in memory table
                bool berror = false;

                // get the line data and state associated with certain address
                char* curstate = &pstate[taboffset];
                char* curdata = &pmemdata[(unsigned __int64)taboffset*linesize];

                switch (pline->state)
                {
                case CacheState::LNINVALID:
                    // invalid line will not update memory table
                    break;

                case CacheState::LNSHARED:
                    // shared might have different data with the main memory
                    // compare and update the memory data and state

                    if (*curstate == (char)CM_STATE_BC)
                    {
                        // update
                        *curstate = CM_STATE_SH;
                        memcpy(curdata, pline->data, linesize);
                    }
                    else if (pstate[taboffset] == (char)CM_STATE_SH)
                    {
                        // compare data
                        if (memcmp(curdata, pline->data, linesize) == 0)
                        {
                            // doing nothing
                        }
                        else
                        {
                            berror = 1;
                            errorcode = CHECKCM_ERROR_INCONSISTENCY_SS;
                        }
                    }
                    else if (pstate[taboffset] == (char)CM_STATE_MO)
                    {
                        if (memcmp(curdata, pline->data, linesize) == 0)
                        {
                            // doing nothing
                        }
                        else
                        {
                            berror = 1;
                            errorcode = CHECKCM_ERROR_INCONSISTENCY_SO;
                        }
                    }
                    else
                    {
                        berror = 1;
                        errorcode = CHECKCM_ERROR_UNKNOWN;
                    }
                    break;

                case CacheState::LNMODIFIED:
                    // modified might have different data with the main memory
                    // compare and update data and state

                    if (*curstate == (char)CM_STATE_BC)
                    {
                        // update
                        *curstate = CM_STATE_MO;
                        memcpy(curdata, pline->data, linesize);
                    }
                    else if (pstate[taboffset] == (char)CM_STATE_SH)
                    {
                        // compare data
                        if (memcmp(curdata, pline->data, linesize) == 0)
                        {
                            // update
                            *curstate = CM_STATE_MO;
                        }
                        else
                        {
                            berror = 1;
                            errorcode = CHECKCM_ERROR_INCONSISTENCY_SO;
                        }
                    }
                    else if (pstate[taboffset] == (char)CM_STATE_MO)
                    {
                        berror = 1;
                        errorcode = CHECKCM_ERROR_INCONSISTENCY_DOUBLEOWNER;
                    }
                    else
                    {
                        berror = 1;
                        errorcode = CHECKCM_ERROR_UNKNOWN;
                    }
                    break;

                case CacheState::LNOWNED:       // same as modified
                    // owned might have different data
                    // compare and update

                    if (*curstate == (char)CM_STATE_BC)
                    {
                        // update
                        *curstate = CM_STATE_MO;
                        memcpy(curdata, pline->data, linesize);
                    }
                    else if (pstate[taboffset] == (char)CM_STATE_SH)
                    {
                        // compare data
                        if (memcmp(curdata, pline->data, linesize) == 0)
                        {
                            // update
                            *curstate = CM_STATE_MO;
                        }
                        else
                        {
                            berror = 1;
                            errorcode = CHECKCM_ERROR_INCONSISTENCY_SO;
                        }
                    }
                    else if (pstate[taboffset] == (char)CM_STATE_MO)
                    {
                        berror = 1;
                        errorcode = CHECKCM_ERROR_INCONSISTENCY_DOUBLEOWNER;
                    }
                    else
                    {
                        berror = 1;
                        errorcode = CHECKCM_ERROR_UNKNOWN;
                    }
                    break;

                case CacheState::LNREADPENDING:
					if (bIgnoreLockingStates)
					{
						// do something
					}
					else
					{
						berror = true;
						errorcode = CHECKCM_ERROR_LOCKINGSTATEINCACHE;
					}
                    break;
                    
                case CacheState::LNREADPENDINGI:
					if (bIgnoreLockingStates)
					{
						// do something
					}
					else
					{
						berror = true;
						errorcode = CHECKCM_ERROR_LOCKINGSTATEINCACHE;
					}
                    break;

                case CacheState::LNWRITEPENDINGI:
					if (bIgnoreLockingStates)
					{
						// do something
					}
					else
					{
						berror = true;
						errorcode = CHECKCM_ERROR_LOCKINGSTATEINCACHE;
					}
                    break;

                case CacheState::LNWRITEPENDINGM:
					if (bIgnoreLockingStates)
					{
						// do something
					}
					else
					{
						berror = true;
						errorcode = CHECKCM_ERROR_LOCKINGSTATEINCACHE;
					}
                    break;

                case CacheState::LNWRITEPENDINGE:
					if (bIgnoreLockingStates)
					{
						// do something
					}
					else
					{
						berror = true;
						errorcode = CHECKCM_ERROR_LOCKINGSTATEINCACHE;
					}
                    break;

                default:
                    break;
                }

                // error report
                if (berror)
                {
                    switch (errorcode)
                    {
                    case CHECKCM_ERROR_LOCKINGSTATEINCACHE:
                        cout << "line@address " << hex << lineaddr << "-" << lineaddr+linesize << dec << " at locking state" << endl;
                        break;

                    case CHECKCM_ERROR_INCONSISTENCY_SS:
                        cout << "line@address " << hex << lineaddr << "-" << lineaddr+linesize << dec << " S-S inconsistency" << endl;
                        break;

                    case CHECKCM_ERROR_INCONSISTENCY_SO:
                        cout << "line@address " << hex << lineaddr << "-" << lineaddr+linesize << dec << " S-O inconsistency" << endl;
                        break;

                    case CHECKCM_ERROR_INCONSISTENCY_DOUBLEOWNER:
                        cout << "line@address " << hex << lineaddr << "-" << lineaddr+linesize << dec << " Double Owner" << endl;
                        break;

                    case CHECKCM_ERROR_UNKNOWN:
                        cout << "line@address " << hex << lineaddr << "-" << lineaddr+linesize << dec << " unknow error" << endl;
                        break;

                    default:
                        free(pmemdata);
                        free(pstate);
                        return false;
                    }
                }

            }
        }

    }

    return true;
}

#endif


// check L2 caches portal function
bool checkcacheandmemory()
{
    int errc;
    char* pmemdata;
    char* pstate;
    bool bret = checkcacheandmemory(errc, pmemdata, pstate);	// the last parameter should be false or empty, the force option is provided for debug option
    free(pmemdata);
    free(pstate);
    return bret;
}

// dump only L2 caches
void dumpcacheandmemory(ofstream& filestream, bool bforce)
{
	int errcode=CHECKCM_ERROR_NONE;
	char* pmemdata;
	char* pstate;
	if (checkcacheandmemory(errcode, pmemdata, pstate, bforce))		// the last parameter should be false or empty for normal case. the force option is provided only for debug
	{
		if (errcode == CHECKCM_ERROR_NONE)
			filestream.write(pmemdata, LinkMGS::s_oLinkConfig.m_nMemorySize);
	}
	else
	{
		cout << "error occurs during consistency check. no file memory and cache consistency." << endl;
	}

	free(pmemdata);
	free(pstate);
}

void reviewmemorysystem()
{
    reviewsystemstates();
}

#ifdef MEM_MODULE_STATISTICS
void printstatistics(const char* filename)
{
    ofstream statfile;

    statfile.open(filename);
    if (statfile.fail())
    {
        cout << "cannot open statistics file." << endl;
        statfile.close();
    }

    systemprintstatistics(statfile);

    statfile.close();
}
#endif

ofstream g_osMonitorFile;
unsigned __int64 g_u64MonitorAddress = 0;

void automonitoraddress(unsigned __int64 address)
{
    g_u64MonitorAddress = address;
}

void startmonitorfile(const char* filename)
{
    if (g_osMonitorFile.is_open())
        return;

    g_osMonitorFile.open(filename);

    if (g_osMonitorFile.fail())
    {
        cout << "cannot open monitor file." << endl;
        g_osMonitorFile.close();
    }
}


void stopmonitorfile()
{
    if (g_osMonitorFile.is_open())
        g_osMonitorFile.close();

/*
    cout << nib << endl;
    vector<ST_request*>::iterator iter;
    for (iter=vecpib.begin();iter!=vecpib.end();iter++)
    {
        ST_request* req = *iter;
        cout << dec << req->starttime << "  " << hex << req->getlineaddress() << endl;
    }
*/
}

void monitormemoryaddress(unsigned __int64 address)
{
    if (g_osMonitorFile.is_open()&&g_osMonitorFile.good())
        monitoraddress(g_osMonitorFile, address);
    else
        monitoraddress(cout, address);
}


