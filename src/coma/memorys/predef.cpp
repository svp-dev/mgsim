#include "predef.h"
#include <string>
#include "inttypes.h"
using namespace std;
using namespace MemSim;

#include "string.h"

#include "setassociativeprop.h"
#include "../simlink/linkmgs.h"

namespace MemSim{
//{ memory simulator namespace


#ifdef WIN32
HANDLE __hColorConsole;
#endif

const __address_t MemoryState::MEMORY_SIZE = 0x7000000;
unsigned int ST_request::s_nRequestAlignedSize;

vector<SimObj*>  &g_vecAllSimObjs = SimObj::s_vecObj;


unsigned int pow2(unsigned int n)
{
    return (1 << n);
}

int lg2(int n)
{
	int r = 0;

	while (n > 1)
	{
		r++;
		n /= 2;
	}

	return r;
}

unsigned int lg2(unsigned int n)
{
	int r = 0;

	while (n > 1)
	{
		r++;
		n /= 2;
	}

	return r;
}

void validatename(char *name)
{

	for(unsigned int i=0;i<strlen(name);i++) 
		if (name[i] == '.')
			name[i] = '_';
}

void print_cline_state(CacheState::CACHE_LINE_STATE state, bool bshort)
{
    switch(state)
    {
        case CacheState::CLS_INVALID:
            clog << (bshort?"CI":"CLS_INVALID") ;
            break;
            
        case CacheState::CLS_SHARER:
            clog << (bshort?"CS":"CLS_SHARER") ;
            break;

        case CacheState::CLS_OWNER:
            clog << (bshort?"CO":"CLS_ONWER") ;
            break;

        default:
            clog << "error" << endl;
            break;
    }
}

void print_cline(cache_line_t* line, bool bshort)
{
    if (bshort)
    {
        print_cline_state(line->state, bshort);
        clog << "[" << line->tokencount << "]" << (line->pending?"^":"") << (line->invalidated?"#":"") << (line->priority?"+":"") << endl;
    }
    else
    {
        print_cline_state(line->state, bshort);
        clog << " : " << line->tokencount << "; Pending: " << (line->pending?"yes":"no") << "; Invalidated: " << (line->invalidated?"yes":"no") << "; Priority: " << (line->priority?"yes":"no") << endl;
    }
}

void print_cline_data(cache_line_t* line)
{
    print_cline(line, false);

    //print bitmask
    clog << "mask : ";
    for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
    {   
        clog << hex << setw(2) << setfill('0') << (unsigned int)(unsigned char)line->bitmask[i] << " ";
    }       

    // print data;
    for (unsigned int i=0;i<g_nCacheLineSize;i++)
    {   
        if (i % 32 == 0)
            clog << endl << "\t";
        clog << hex << setw(2) << setfill('0') << (unsigned int)(unsigned char)line->data[i] << " ";
    }       
    clog << endl;
}

void print_dline_state(CacheState::DIR_LINE_STATE state, bool , bool bshort)
{
    switch(state)
    {
        case CacheState::DLS_INVALID:
            clog << (bshort?"DI":"DLS_INVALID") ;
            break;
            
        case CacheState::DLS_CACHED:
            clog << (bshort?"DC":"DLS_CACHED") ;
            break;

//        case CacheState::CLS_OWNDER:
//            clog << (bshort?"DO":"DLS_ONWER") ;
//            break;

        default:
            clog << "error" << endl;
            break;
    }
}

void print_dline(dir_line_t* line, bool broot, bool bshort)
{
    if (bshort)
    {
        print_dline_state(line->state, broot, bshort);
        //clog << "[" << line->tokencount << "]" << (line->pending?"P":"") << (line->invalidated?"#":"") << endl;
        //clog << "[" << line->tokencount << "]" << (line->pending?"P":"") << (line->invalidated?"#":"") << endl;
    }
    else
    {
        print_dline_state(line->state, broot, bshort);
        //clog << " : " << line->tokencount << "; Pending: " << (line->pending?"yes":"no") << "; Invalidated: " << (line->invalidated?"yes":"no") << endl;
        //clog << " : " << line->tokencount << "; Pending: " << (line->pending?"yes":"no") << "; Invalidated: " << (line->invalidated?"yes":"no") << endl;
    }
}


void print_request_type(ST_request* req)
{
	switch(req->type)
	{
#ifdef TOKEN_COHERENCE
    // token coherence cases
    case MemoryState::REQUEST_NONE:
        clog << "RT: " << "REQUEST_NONE" << "; " ;
        break;

    case MemoryState::REQUEST_READ:
        clog << "RT: " << "REQUEST_READ" << "; " ;
        break;

    case MemoryState::REQUEST_WRITE:
        clog << "RT: " << "REQUEST_WRITE" << "; " ;
        break;

    case MemoryState::REQUEST_READ_REPLY:
        clog << "RT: " << "REQUEST_READ_REPLY" << "; " ;
        break;

    case MemoryState::REQUEST_WRITE_REPLY:
        clog << "RT: " << "REQUEST_WRITE_REPLY" << "; " ;
        break;

    case MemoryState::REQUEST_ACQUIRE_TOKEN:
        clog << "RT: " << "REQUEST_ACQUIRE_TOKEN" << "; " ;
        break;

    case MemoryState::REQUEST_ACQUIRE_TOKEN_DATA:
        clog << "RT: " << "REQUEST_ACQUIRE_TOKEN_DATA" << "; " ;
        break;

    case MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA:
        clog << "RT: " << "REQUEST_DISSEMINATE_TOKEN_DATA" << "; " ;
        break;

    case MemoryState::REQUEST_INVALIDATE_BR:
        clog << "RT: " << "REQUEST_INVALIDATE_BR" << "; " ;
        break;

#else
    // non-token coherence cases
	case MemoryState::REQUEST_READ:
		clog << "RT: " << "REQUEST_READ"<<  "; " ;
		break;
	case MemoryState::REQUEST_WRITE:
		clog << "RT: " << "REQUEST_WRITE" << "; " ;
		break;
	case MemoryState::REQUEST_READ_REPLY_X:
		clog << "RT: " << "REQUEST_READ_REPLY" << "; " ;
		break;
	case MemoryState::REQUEST_WRITE_REPLY_X:
		clog << "RT: " << "REQUEST_WRITE_REPLY" << "; " ;
		break;
	case MemoryState::REQUEST_READ_REDIRECT:
		clog << "RT: " << "REQUEST_READ_REDIRECT" << "; " ;
		break;
	case MemoryState::REQUEST_WRITE_REDIRECT:
		clog << "RT: " << "REQUEST_WRITE_REDIRECT" << "; " ;
		break;
	case MemoryState::REQUEST_INVALIDATE:
		clog << "RT: " << "REQUEST_INVALIDATE" << "; " ;
		break;
    case MemoryState::REQUEST_INVALIDATE_BR:
        clog << "RT: " << "REQUEST_INVALIDATE_BR" << "; " ;
        break;
	case MemoryState::REQUEST_LINE_REPLACEMENT:
		clog << "RT: " << "REQUEST_LINE_REPLACEMENT" << "; " ;
		break;
	case MemoryState::REQUEST_REMOTE_READ_SHARED:
		clog << "RT: " << "REQUEST_REMOTE_READ_SH" << "; " ;
		break;
    case MemoryState::REQUEST_REMOTE_READ_EXCLUSIVE:
        clog << "RT: " << "REQUEST_REMOTE_READ_EX" << "; " ;
        break;
    case MemoryState::REQUEST_REMOTE_SHARED_READ_REPLY:
        clog << "RT: " << "REQUEST_SH_READ_REPLY" << "; " ;
        break;
    case MemoryState::REQUEST_REMOTE_EXCLUSIVE_READ_REPLY:
        clog << "RT: " << "REQUEST_EX_READ_REPLY" << "; " ;
        break;
	//case MemoryState::REQUEST_DATA_EXCLUSIVE:
	//	clog << "RT: " << "REQUEST_DATA_EXCLUSIVE" << "; " ;
	//	break;
	case MemoryState::REQUEST_READ_MERGE:
		clog << "RG: " << "REQUEST_READ_MERGE" << "; " ;
		break;
	case MemoryState::REQUEST_MERGE_READ_REDIRECT:
		clog << "MR: " << "REQUEST_MERGE_READ_REDIRECT" << "; " ;
		break;
	case MemoryState::REQUEST_MERGE_READ_REPLY:
		clog << "MP: " << "REQUEST_MERGE_READ_REPLY" << "; " ;
		break;
    case MemoryState::REQUEST_WRITE_BACK:
        clog << "WB: " << "REQUEST_WRITE_BACK" << "; " ;
        break;
    case MemoryState::REQUEST_EVICT:
        clog << "EV: " << "REQUEST_EVICT" << "; " ;
        break;
#endif
	default:
		clog << "error Type" << "; " ;
		break;
	}

    if (req->bqueued)
        clog << "[Q]" ;
}

#ifndef TOKEN_COHERENCE
void print_request(ST_request* req, bool popped)
{
	clog << "Request(" << req << ") :: ";
	print_request_type(req);
    //clog << " LineAddress: " << FMT_ADDR(req->getlineaddress()) << "; ReqAddress: " << FMT_ADDR(req->getreqaddress()) << "; Size (original): " << FMT_ADDR(req->nsize) << "; Ini: " /*<< req->initiator*/ << "[" << req->nameini << "]" << endl;
	clog << " LineAddress: " << FMT_ADDR(req->getlineaddress()) << "; ReqAddress: " << FMT_ADDR(req->getreqaddress()) << "; Size (original): " << FMT_ADDR(req->nsize) << "; Ini: " /*<< req->initiator*/ << "[" << ( ( (req->type==MemoryState::REQUEST_INVALIDATE_BR)||((req->type==MemoryState::REQUEST_WRITE_REPLY)&&(!req->bprocessed)))?"NULL":((SimObj*)get_initiator_node(req, popped))->GetObjectName() ) << "]" << endl;
    char *xt = (char*)malloc(1000);
	xt[0] = '\0';
    if (popped == false)
    {
        req->RequestInfo2Text(xt, false, true, true);
        clog << xt << endl;
    }
    free(xt);
}

#else
void print_request(ST_request* req, bool popped)
{
	clog << "Request(" << req << ") :: ";
	print_request_type(req);
    for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
        clog << hex << (unsigned int)(unsigned char)req->bitmask[i] << " ";
    clog << "; ";
    //clog << " LineAddress: " << FMT_ADDR(req->getlineaddress()) << "; ReqAddress: " << FMT_ADDR(req->getreqaddress()) << "; Size (original): " << FMT_ADDR(req->nsize) << "; Ini: " /*<< req->initiator*/ << "[" << req->nameini << "]" << endl;
    clog << " " << req->tokenacquired << "|" << req->tokenrequested << " " << (req->dataavailable?"A":"N") << " " << (req->bpriority?"P":"") << (req->btransient?"!":"") << "; ";
	clog << " LineAddress: " << FMT_ADDR(req->getlineaddress()) << "; ReqAddress: " << FMT_ADDR(req->getreqaddress()) << "; Size (original): " << FMT_ADDR(req->nsize) << ";";
    clog << " Ini: " << req->curinitiator << " [";
    if  ( (req->bmerged)||(req->type==MemoryState::REQUEST_INVALIDATE_BR)||((req->type==MemoryState::REQUEST_WRITE_REPLY)&&(!req->bprocessed)))
    {
        clog << "NULL]" << endl;
    }
    else
    {
//          clog << ((SimObj*)get_initiator_node(req, popped))->GetObjectName() << "]" << endl;

        for (int i=req->curinitiator-(popped?0:1);i>=0;i--)
        {
            clog << ((SimObj*)req->initiatortable[i])->GetObjectName();
            if (i == 0)
            {
                clog << "]" << endl;
            }
            else
            {
                clog << ":";
            }
        }
        clog << 5; 
    }
    char *xt = (char*)malloc(1000);
	xt[0] = '\0';
    if (popped == false)
    {
        req->RequestInfo2Text(xt, false, true, true);
        clog << xt << endl;
    }
    free(xt);
}

#endif


void generate_random_data(char *data, unsigned int size)
{
    for (unsigned int i=0;i<size;i++)
        data[i] = (rand()%0xff);
}

//void generate_random_data(UINT32 *data, unsigned int size)
//{
//    for (unsigned int i=0;i<size;i++)
//        data[i] = (rand()%0xffffffff);
//}

SimObj* get_initiator_bus(ST_request* req, bool popped)
{
//    assert((req->curinitiator == 0) || (req->curinitiator == 1)); 
    unsigned int index = req->curinitiator;

    if (popped)
        index++;

    assert(index > 0);
    return req->initiatortable[index-1];
}

SimObj* get_initiator_node(ST_request* req, bool popped)
{
//    assert(req->curinitiator == 2);
    unsigned int index = req->curinitiator;

    if (popped)
        index++;
    return req->initiatortable[index-1];
}

SimObj* pop_initiator_bus(ST_request* req)
{
    //    assert((req->curinitiator == 0) || (req->curinitiator == 1)); 
    assert(req->curinitiator > 0);
    return req->initiatortable[--req->curinitiator];
}

SimObj* pop_initiator_node(ST_request* req)
{
    //    assert(req->curinitiator == 2);
    assert(req->curinitiator > 0);
    return req->initiatortable[--req->curinitiator];
}


//////////////////////////////////////////////////////////////////////////
// class member functions

// return the request type
const char* MemoryState::RequestName(int requesttype, bool shortname)
{
    const char* ret;

    switch (requesttype)
    {
#ifdef TOKEN_COHERENCE
    // token coherence cases
    case REQUEST_NONE:
        ret = (shortname)?"NO":"REQUEST_NONE";
        break;

    case REQUEST_READ:
        ret = (shortname)?"LR":"REQUEST_READ";
        break;

    case REQUEST_WRITE:
        ret = (shortname)?"LW":"REQUEST_WRITE";
        break;

    case REQUEST_READ_REPLY:
        ret = (shortname)?"RR":"REQUEST_READ_REPLY";
        break;

    case REQUEST_WRITE_REPLY:
        ret = (shortname)?"WR":"REQUEST_WRITE_REPLY";
        break;

    case REQUEST_ACQUIRE_TOKEN:
        ret = (shortname)?"AT":"REQUEST_ACQUIRE_TOKEN";
        break;

    case REQUEST_ACQUIRE_TOKEN_DATA:
        ret = (shortname)?"AD":"REQUEST_ACQUIRE_TOKEN_DATA";
        break;

    case REQUEST_DISSEMINATE_TOKEN_DATA:
        ret = (shortname)?"DD":"REQUEST_DISSEMINATE_TOKEN_DATA";
        break;

    case REQUEST_INVALIDATE_BR:
        ret = (shortname)?"IB":"REQUEST_INVALIDATE_BR";
        break;

#else
    // non-token coherence cases
    case REQUEST_READ:
        ret = (shortname)?"LR":"REQUEST_READ";
        break;

    case REQUEST_WRITE:
        ret = (shortname)?"LW":"REQUEST_WRITE";
        break;

    case REQUEST_READ_REPLY_X:
        ret = (shortname)?"RR":"REQUEST_READ_REPLY_X";
        break;

    case REQUEST_MERGE_READ_REPLY:
        ret = (shortname)?"WP":"REQUEST_MERGE_READ_REPLY";
        break;

    case REQUEST_WRITE_REPLY_X:
        ret = (shortname)?"WR":"REQUEST_WRITE_REPLY_X";
        break;

    case REQUEST_READ_REDIRECT:
        ret = (shortname)?"LR":"REQUEST_READ_REDIRECT";
        break;

    case REQUEST_MERGE_READ_REDIRECT:
        ret = (shortname)?"MR":"REQUEST_MERGE_READ_REDIRECT";
        break;

    case REQUEST_WRITE_REDIRECT:
        ret = (shortname)?"LW":"REQUEST_WRITE_REDIRECT";
        break;

    case REQUEST_READ_MERGE:
        ret = (shortname)?"RG":"REQUEST_READ_MERGE";
        break;

    case REQUEST_WRITE_MERGE:
        ret = (shortname)?"WG":"REQUEST_WRITE_MERGE";
        break;

    case REQUEST_INVALIDATE:
        ret = (shortname)?"IV":"REQUEST_INVALIDATE";
        break;

    case REQUEST_INVALIDATE_BR:
        ret = (shortname)?"IB":"REQUEST_INVALIDATE_BR";
        break;

    case REQUEST_LINE_REPLACEMENT:
        ret = (shortname)?"RL":"REQUEST_LINE_REPLACEMENT";
        break;

    case REQUEST_REMOTE_READ_SHARED:
        ret = (shortname)?"RS":"REQUEST_REMOTE_READ_SHARED";
        break;

    case REQUEST_REMOTE_READ_EXCLUSIVE:
        ret = (shortname)?"RE":"REQUEST_REMOTE_READ_EXCLUSIVE";
        break;

    case REQUEST_REMOTE_SHARED_READ_REPLY:
        ret = (shortname)?"SR":"REQUEST_REMOTE_SHARED_READ_REPLY";
        break;

    case REQUEST_REMOTE_EXCLUSIVE_READ_REPLY:
        ret = (shortname)?"ER":"REQUEST_REMOTE_EXCLUSIVE_READ_REPLY";
        break;

    case REQUEST_WRITE_BACK:
        ret = (shortname)?"WB":"REQUEST_WRITE_BACK";
        break;

    case REQUEST_EVICT:
        ret = (shortname)?"EV":"REQUEST_EVICT";
        break;

#endif 

    default:
		ret = NULL;
        cerr << "***error***: wrong request type."<< endl;
        break;
    }
        
    return ret;
}

bool ST_request::Conform2BitVecFormat()
{
    // initialize bit vector
    for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
        bitmask[i] = 0;

    if (nsize == 0)
    {
        assert(false);
        return false;
    }

    // alignment is required 
    assert(offset%CACHE_REQUEST_ALIGNMENT == 0);

    unsigned int dataend = offset + nsize;

    for (unsigned int i=0;i<g_nCacheLineSize;i+=CACHE_REQUEST_ALIGNMENT)
    {
        unsigned int maskhigh = i/(8*CACHE_REQUEST_ALIGNMENT);
        unsigned int masklow = i%(8*CACHE_REQUEST_ALIGNMENT);
        char maskbit;

        if (i<offset)
            maskbit = 0;
        else if (i<dataend)
            maskbit = 1;
        else
            maskbit = 0;

        maskbit = maskbit << masklow;

        bitmask[maskhigh] |= maskbit;
    }

    return true;
}

bool ST_request::Conform2SizeFormat()
{
    unsigned int validsize = 0;
    unsigned int validoffset = 0;
    bool bsegstart = false;
    bool bsegend = false;

    // 00 -> 10 -> 01 -> 01 | error
    // check whether the mask is valid to trasform
    for (unsigned int i=0;i<g_nCacheLineSize;i+=CACHE_REQUEST_ALIGNMENT)
    {
        unsigned int maskhigh = i/(8*CACHE_REQUEST_ALIGNMENT);
        unsigned int masklow = i%(8*CACHE_REQUEST_ALIGNMENT);

        char maskbit = 1 << masklow;

        if ((bitmask[maskhigh]&maskbit) == 0)
        {
            if (!bsegend)
            {
                if (bsegstart == false)
                    continue;
                else
                {
                    bsegend = true;
                    validsize = i - validoffset;
                    bsegstart = false;
                }
            }
            else
            {
                if (bsegstart == false)
                    continue;
                else
                    assert(false);
            }
        }
        else
        {
            if (!bsegend)
            {
                if (!bsegstart)
                {
                    validoffset = i;
                    bsegstart = true;
                }
                else
                {
                    continue;
                }
            }
            else
            {
                assert(false);
                return false;
            }
        }
    }

    nsize = validsize;
    offset = validoffset;

    return true;
}

unsigned int CacheState::s_nTotalToken = 0;

#ifdef TOKEN_COHERENCE
// buffer should be preallocated, no more check will be carried out
char* CacheState::CacheStateName(CACHE_LINE_STATE nlinestate, unsigned int ntoken, bool pending, bool invalidated, bool priority, bool tlock, char* pstatename, bool shortname)
{
    assert(pstatename != NULL);

    unsigned int ntotal = GetTotalTokenNum();

    assert(ntoken <= ntotal);

    if (nlinestate == CLS_INVALID)
    {
        assert(ntoken == 0);
        assert(priority == false);

        if (shortname)
            sprintf(pstatename, "I%02d", ntoken);
        else
            sprintf(pstatename, "Invalid - %d", ntoken);
    }
    else if (pending == false)
    {
        if ((ntoken < ntotal)&&(nlinestate == CLS_SHARER))
        {
            if (shortname)
                sprintf(pstatename, "%s%02d", (priority?"S":"s"), ntoken);
            else
                sprintf(pstatename, "%s - %d", (priority?"SHARED":"shared"), ntoken);
        }
        else if ((ntoken < ntotal)&&(nlinestate == CLS_OWNER))
        {
            if (shortname)
                sprintf(pstatename, "%s%02d", (priority?"O":"o"), ntoken);
            else
                sprintf(pstatename, "%s - %d", (priority?"OWNED":"owned"), ntoken);
        }
        else if ((ntoken == ntotal)&&(nlinestate == CLS_SHARER))
        {
            if (shortname)
                sprintf(pstatename, "%s%02d", (priority?"E":"e"), ntoken);
            else
                sprintf(pstatename, "%s - %d", (priority?"EXCLUSIVE":"exclusive"), ntoken);
        }
        else if ((ntoken == ntotal)&&(nlinestate == CLS_OWNER))
        {
            if ((ntoken == ntotal)&&(nlinestate == CLS_OWNER))
                sprintf(pstatename, "%s%02d", (priority?"M":"m"), ntoken);
            else
                sprintf(pstatename, "%s - %d", (priority?"MODIFIED":"modified"), ntoken);
        }
        else
        {
            cout << ntoken << " " << nlinestate << " " << pending << " " << invalidated << " " << priority << endl;
            cout << ntotal << " " << (ntoken < ntotal) << endl;
            cout << (nlinestate == CLS_OWNER) << endl;
            assert(false);
        }
    }

    else
    {
        if ((nlinestate == CLS_SHARER)&&(!invalidated))
        {
            // probably the number of tokens doesn't matter JXXX
            if (shortname)
                sprintf(pstatename, "%s%02d%s", (priority?"R":"r"), ntoken, (tlock?"!":""));
            else
                sprintf(pstatename, "%s - %d%s", (priority?"READPENDING":"readpending"), ntoken, (tlock?"!":""));
        }
        else if ((nlinestate == CLS_SHARER)&&(invalidated))
        {
            // probably the number of tokens doesn't matter JXXX
            if (shortname)
                sprintf(pstatename, "%s%02d%s", (priority?"T":"t"), ntoken, (tlock?"!":""));
            else
                sprintf(pstatename, "%s - %d%s", (priority?"READPENDINGI":"readpendingi"), ntoken, (tlock?"!":""));
        }
        else if ((ntoken == 0)&&(nlinestate == CLS_OWNER)&&(!invalidated))
        {
            // writependingi
            if (shortname)
                sprintf(pstatename, "%s%02d%s", (priority?"P":"p"), ntoken, (tlock?"!":""));
            else
                sprintf(pstatename, "%s - %d%s", (priority?"WRITEPENDINGI":"writependingi"), ntoken, (tlock?"!":""));
        }
        else if ((ntoken > 0)&&(nlinestate == CLS_OWNER)&&(!invalidated))
        {
            // writependingM
            if (shortname)
                sprintf(pstatename, "%s%02d%s", (priority?"W":"w"), ntoken, (tlock?"!":""));
            else
                sprintf(pstatename, "%s - %d%s", (priority?"WRITEPENDINGM":"writependingm"), ntoken, (tlock?"!":""));
        }
        else if ((nlinestate == CLS_OWNER)&&(invalidated))
        {
            if (shortname)
                sprintf(pstatename, "%s%02d%s", (priority?"U":"u"), ntoken, (tlock?"!":""));
            else
                sprintf(pstatename, "%s - %d%s", (priority?"WRITEPENDINGE":"writependingme"), ntoken, (tlock?"!":""));
        }
        else
        {
            assert(false);
        }
    }

    return pstatename;
}

// locked may not precise, since it might require specific request to test whether the buffer is locked !!! IMPORTANT
char* CacheState::CacheStateName(CACHE_LINE_STATE nlinestate, unsigned int ntoken, bool pending, bool invalidated, bool priority, bool tlock, bool msbhit, bool msblock, char* pstatename, bool shortname)
{
    assert(pstatename != NULL);

    unsigned int ntotal = GetTotalTokenNum();

    assert(ntoken <= ntotal);

    if (nlinestate == CLS_INVALID)
    {
        assert(ntoken == 0);
        assert(priority == false);

        if (shortname)
            sprintf(pstatename, "I%02d ", ntoken);
        else
            sprintf(pstatename, "Invalid - %d", ntoken);
    }
    else if (pending == false)
    {
        if ((ntoken < ntotal)&&(nlinestate == CLS_SHARER))
        {
            if (shortname)
                sprintf(pstatename, "%s%02d ", (priority?"S":"s"), ntoken);
            else
                sprintf(pstatename, "%s - %d", (priority?"SHARED":"shared"), ntoken);
        }
        else if ((ntoken < ntotal)&&(nlinestate == CLS_OWNER))
        {
            if (shortname)
                sprintf(pstatename, "%s%02d ", (priority?"O":"o"), ntoken);
            else
                sprintf(pstatename, "%s - %d", (priority?"OWNED":"owned"), ntoken);
        }
        else if ((ntoken == ntotal)&&(nlinestate == CLS_SHARER))
        {
            if (shortname)
                sprintf(pstatename, "%s%02d ", (priority?"E":"e"), ntoken);
            else
                sprintf(pstatename, "%s - %d", (priority?"EXCLUSIVE":"exclusive"), ntoken);
        }
        else if ((ntoken == ntotal)&&(nlinestate == CLS_OWNER))
        {
            if ((ntoken == ntotal)&&(nlinestate == CLS_OWNER))
                sprintf(pstatename, "%s%02d ", (priority?"M":"m"), ntoken);
            else
                sprintf(pstatename, "%s - %d", (priority?"MODIFIED":"modified"), ntoken);
        }
        else
        {
            cout << ntoken << " " << nlinestate << " " << pending << " " << invalidated << " " << priority << endl;
            cout << ntotal << " " << (ntoken < ntotal) << endl;
            cout << (nlinestate == CLS_OWNER) << endl;
            assert(false);
        }
    }

    else
    {
        if ((nlinestate == CLS_SHARER)&&(!invalidated))
        {
            // probably the number of tokens doesn't matter JXXX
            if (shortname)
                sprintf(pstatename, "%s%02d%s", (priority?"R":"r"), ntoken, (tlock?"!":" "));
            else
                sprintf(pstatename, "%s - %d%s", (priority?"READPENDING":"readpending"), ntoken, (tlock?"!":" "));
        }
        else if ((nlinestate == CLS_SHARER)&&(invalidated))
        {
            // probably the number of tokens doesn't matter JXXX
            if (shortname)
                sprintf(pstatename, "%s%02d%s", (priority?"T":"t"), ntoken, (tlock?"!":" "));
            else
                sprintf(pstatename, "%s - %d%s", (priority?"READPENDINGI":"readpendingi"), ntoken, (tlock?"!":" "));
        }
        else if ((ntoken == 0)&&(nlinestate == CLS_OWNER)&&(!invalidated))
        {
            // writependingi
            if (shortname)
                sprintf(pstatename, "%s%02d%s", (priority?"P":"p"), ntoken, (tlock?"!":" "));
            else
                sprintf(pstatename, "%s - %d%s", (priority?"WRITEPENDINGI":"writependingi"), ntoken, (tlock?"!":" "));
        }
        else if ((ntoken > 0)&&(nlinestate == CLS_OWNER)&&(!invalidated))
        {
            // writependingM
            if (shortname)
                sprintf(pstatename, "%s%02d%s", (priority?"W":"w"), ntoken, (tlock?"!":" "));
            else
                sprintf(pstatename, "%s - %d%s", (priority?"WRITEPENDINGM":"writependingm"), ntoken, (tlock?"!":" "));
        }
        else if ((nlinestate == CLS_OWNER)&&(invalidated))
        {
            if (shortname)
                sprintf(pstatename, "%s%02d%s", (priority?"U":"u"), ntoken, (tlock?"!":" "));
            else
                sprintf(pstatename, "%s - %d%s", (priority?"WRITEPENDINGE":"writependingme"), ntoken, (tlock?"!":" "));
        }
        else
        {
            assert(false);
        }
    }

    if (shortname)
        sprintf(pstatename, "%s%s", pstatename, ((msblock)?"=":(msbhit?".":" ")));
    else
        sprintf(pstatename, "%s%s", pstatename, ((msblock)?" MSB Locked":(msbhit?"MSB Hit":"MSB None")));


    return pstatename;
}

// return the name of the directory state name
char* CacheState::DirectoryStateName(DIR_LINE_STATE nlinestate, unsigned int ntoken, bool breserved, bool priority, char* pstatename, bool shortname)
{
    switch (nlinestate)
    {
    case CacheState::DLS_INVALID:
        if (breserved)
            sprintf(pstatename, "%s", ((shortname)?"*00":"#R - DLS_INVALID"));
        else
            sprintf(pstatename, "%s", ((shortname)?"-00":"DLS_INVALID"));
        break;

    //case DRRESERVED:
        //ret = (shortname)?"R":"DRRESERVED";
        //break;

    case CacheState::DLS_CACHED:
         if (breserved)
            if (shortname)
                sprintf(pstatename, "*%s%02d", (priority?"C":"c"), ntoken);
            else
                sprintf(pstatename, "#R - %s - %02d", (priority?"DLS_CACHED":"dls_cached"), ntoken);
        else
            if (shortname)
                sprintf(pstatename, "+%s%02d", (priority?"C":"c"), ntoken);
            else
                sprintf(pstatename, "%s - %02d", (priority?"DLS_CACHED":"dls_cached"), ntoken);
        break;

    default:
        cerr << "***error***: wrong directory state for MOSI-ext protocol" << endl;
        return NULL;
        break;
    }

    return pstatename;
}


// return the name of a certain directory line state
const char* dir_line_t::StateName(bool root, bool shortname)
{
    if (root)
    {
        const char* ret;

        switch (state)
        {
        case CacheState::DLS_INVALID:
            if (breserved)
                ret = (shortname)?"*":"#R - DLS_INVALID";
            else
                ret = (shortname)?"-":"DLS_INVALID";
            break;

        //case DRRESERVED:
            //ret = (shortname)?"R":"DRRESERVED";
            //break;

        case CacheState::DLS_CACHED:
            if (breserved)
                ret = (shortname)?"c":"#R - DLS_CACHED";
            else
                ret = (shortname)?"C":"DLS_CACHED";
            break;

        default:
            ret = NULL;
            cerr << "***error***: wrong directory state for TOK-ext protocol" << endl;
            break;
        }

        return ret;
    }
    else
    {
        const char* ret;

        switch (state)
        {
        case CacheState::DLS_INVALID:
            ret = (shortname)?"-":"DLS_INVALID";
            break;

        //case DRRESERVED:
            //ret = (shortname)?"R":"DRRESERVED";
            //break;

        case CacheState::DLS_CACHED:
            if (priority)
                ret = (shortname)?"#":"DLS_CACHED(P)";
            else
                ret = (shortname)?"+":"DLS_CACHED";
            break;

        default:
            ret = NULL;
            cerr << "***error***: wrong directory state for TOK-ext protocol" << endl;
            assert(false);
            break;
        }

        return ret;
    }
}
#else

// return the name of a certain cache state
const char* CacheState::CacheStateName(int cachestate, int cachetype, bool shortname)
{
    const char * ret = NULL;

    switch (cachetype)
    {
    case PT_VI: // V-I protocol
        {
            switch (cachestate)
            {
            case LNINVALID:
                ret = (shortname)?"-":"LNINVALID";
                break;

            case LNVALID:
                ret = (shortname)?"V":"LNVALID";
                break;

            default:
                cerr << "***error***: wrong cache state for V-I protocol" << endl;
                break;
            }
        }
        break;

    case PT_MOSI: // MOSI-ext protocol
        {
            switch (cachestate)
            {
            case LNINVALID:
                ret = (shortname)?"-":"LNINVALID";
                break;

            case LNSHARED:
                ret = (shortname)?"S":"LNSHARED";
                break;

            case LNMODIFIED:
                ret = (shortname)?"M":"LNMODIFIED";
                break;

            case LNOWNED:
                ret = (shortname)?"O":"LNOWNED";
                break;

            case LNREADPENDING:
                ret = (shortname)?"R":"LNREADPENDING";
                break;

            case LNREADPENDINGI:
                ret = (shortname)?"T":"LNREADPENDINGI";
                break;

            case LNWRITEPENDINGI:
                ret = (shortname)?"P":"LNWRITEPENDINGI";
                break;

            case LNWRITEPENDINGE:
                ret = (shortname)?"U":"LNWRITEPENDINGE";
                break;

            case LNWRITEPENDINGM:
                ret = (shortname)?"W":"LNWRITEPENDINGM";
                break;

            default:
                cerr << "***error***: wrong cache state for MOSI-ext protocol" << endl;
                break;
            }
        }
        break;

    default:
        cerr << "***error***: wrong cache type" << endl;
        break;
    }

    return ret;
}


// return the name of a certain directory line state
const char* dir_line_t::StateName(bool shortname)
{
    const char* ret;

    switch (state)
    {
    case CacheState::DRINVALID:
        if (breserved)
            ret = (shortname)?"*":"#R - DRINVALID";
        else
            ret = (shortname)?"-":"DRINVALID";
        break;

    //case DRRESERVED:
        //ret = (shortname)?"R":"DRRESERVED";
        //break;

    case CacheState::DRSHARED:
        if (breserved)
            ret = (shortname)?"s":"#R - DRSHARED";
        else
            ret = (shortname)?"S":"DRSHARED";
        break;

    case CacheState::DREXCLUSIVE:
        if (breserved)
            ret = (shortname)?"e":"#R - DREXCLUSIVE";
        else
            ret = (shortname)?"E":"DREXCLUSIVE";
        break;

    default:
		ret = NULL;
        cerr << "***error***: wrong directory state for MOSI-ext protocol" << endl;
        break;
    }

    return ret;
}
#endif

// convert request info into Text
char* ST_request::RequestInfo2Text(char* ptext, bool shortversion, bool withdata, bool bprint)
{
    assert(ptext!=NULL);

    if (shortversion)
    {
        sprintf(ptext, "[%s|%s](%c|%c)@ 0x%016llx - 0x%016llx", MemoryState::RequestName(type, shortversion), ((SimObj*)get_initiator_node(this))->GetObjectName(), (IsIndUASet()?'u':'-'), (IsIndAASet()?'a':'-'), getreqaddress(), getreqaddress()+nsize);
        if (bprint)
        {
             clog << ptext;
             ptext[0] = '\0';
        }
    }
    else
    {
        unsigned int linesize = g_nCacheLineSize;
//        sprintf(ptext, "[%s|%s](%c|%c)(0x%08x)@ 0x%016llx - 0x%016llx |<=0x%016llx - 0x%016llx=>|\n\t", MemoryState::RequestName(type, shortversion), ((SimObj*)get_initiator_node(this))->GetObjectName(), (IsIndUASet()?'u':'-'), (IsIndAASet()?'a':'-'), (int)this, getreqaddress(), getreqaddress()+nsize-1, getlineaddress(), getlineaddress()+linesize-1);
        sprintf(ptext, "[%s|%s](%c|%c)(%p)@ 0x%016llx - 0x%016llx |<=0x%016llx - 0x%016llx=>|\n\t", MemoryState::RequestName(type, shortversion), ((SimObj*)get_initiator_node(this))->GetObjectName(), (IsIndUASet()?'u':'-'), (IsIndAASet()?'a':'-'), this, getreqaddress(), getreqaddress()+nsize-1, getlineaddress(), getlineaddress()+linesize-1);

        unsigned int count = 0;
        for (unsigned int i=0;i<linesize;i++)
            if ( ((i<offset)||(i>=(offset+nsize))) || ((type == CacheState::REQUEST_ACQUIRE_TOKEN_DATA)&&(tokenacquired == 0)) )
            {
                if (!withdata)
                    sprintf(ptext, "%s-", ptext);
                else
                {
                    if (count == 32)
                    {
                        sprintf(ptext, "%s\n\t", ptext);
                        count = 0;
                    }

                    count++;

                    //if (!IsRequestWithCompleteData())
                    //if (IsRequestWithNoData())
                    if (false)
                    {
                        if (bprint)
                        {
                            clog << ptext;
                            ptext[0] = '\0';

#ifdef SPECIFIED_COLOR_OUTPUT
						    __TEXTCOLOR_GRAY();
#else
                            __TEXTCOLOR(clog,__TEXT_BLACK,true);
#endif
                        }

                        sprintf(ptext, "%s-- ", ptext);

                        if (bprint)
                        {
                            clog << ptext;
                            ptext[0] = '\0';
                            __TEXTCOLORNORMAL();
                        }
                    }
                    else
                    {
                        if (bprint)
                        {
                            clog << ptext;
                            ptext[0] = '\0';
                        }

                        sprintf(ptext, "%s%02x ", ptext, (unsigned int)(unsigned char)data[i]);

                        if (bprint)
                        {
                            clog << ptext;
                            ptext[0] = '\0';
                        }
                    }
                }
            }
            else
            {
                if (!withdata)
                    sprintf(ptext, "%sX", ptext);
                else
                {
                    if (count == 32)
                    {
                        sprintf(ptext, "%s\n\t", ptext);
                        count = 0;
                    }

                    count++;

                    if (IsRequestWithNoData())
                    {
                        if (bprint)
                        {
                            clog << ptext;
                            ptext[0] = '\0';

#ifdef SPECIFIED_COLOR_OUTPUT
						    __TEXTCOLOR_GRAY();
#else
                            __TEXTCOLOR(clog,__TEXT_BLACK,true);
#endif
                        }

                        sprintf(ptext, "%s-- ", ptext);

                        if (bprint)
                        {
                            clog << ptext;
                            ptext[0] = '\0';
                            __TEXTCOLORNORMAL();
                        }
                    }
                    else if (IsRequestWithModifiedData())
                    {
                        if (bprint)
                        {
                            clog << ptext;
                            ptext[0] = '\0';

#ifdef SPECIFIED_COLOR_OUTPUT
						    __TEXTCOLOR_RED();
#else
                            __TEXTCOLOR(clog,__TEXT_RED,true);
#endif
                        }

                        sprintf(ptext, "%s%02x ", ptext, (unsigned int)(unsigned char)data[i]);	

                        if (bprint)
                        {
                            clog << ptext;
                            ptext[0] = '\0';
                            __TEXTCOLORNORMAL();
                        }
                    }
                    else
                    {
                        if (bprint)
                        {
                            clog << ptext;
                            ptext[0] = '\0';
                        }

                        sprintf(ptext, "%s%02x ", ptext, (unsigned int)(unsigned char)data[i]);	// may use uninitialized value defined in data

                        if ( (bprint))
                        {
                            clog << ptext;
                            ptext[0] = '\0';
                        }
                    }

                }

            }
        sprintf(ptext, "%s\n", ptext);
        if (bprint)
        {
            clog << ptext;
            ptext[0] = '\0';
        }
    }

    return ptext;
}

bool ST_request::IsRequestWithCompleteData()
{
#ifndef TOKEN_COHERENCE
    if ( (type == MemoryState::REQUEST_REMOTE_EXCLUSIVE_READ_REPLY)||(type == MemoryState::REQUEST_REMOTE_SHARED_READ_REPLY)||(type == MemoryState::REQUEST_WRITE_BACK)||(type == MemoryState::REQUEST_READ_REPLY_X)||(type == MemoryState::REQUEST_MERGE_READ_REPLY) )
#else
    if (  ((type == MemoryState::REQUEST_ACQUIRE_TOKEN_DATA)&&(tokenacquired > 0)) || ((type == MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA)&&(tokenacquired > 0)) )
#endif
        return true;

    return false;
}

bool ST_request::IsRequestWithNoData()
{
    if (IsRequestWithCompleteData())
        return false;

#ifndef TOKEN_COHERENCE
    if ((type == MemoryState::REQUEST_REMOTE_READ_EXCLUSIVE) || (type == MemoryState::REQUEST_INVALIDATE) || (type == MemoryState::REQUEST_WRITE) || (type == MemoryState::REQUEST_WRITE_MERGE) || (type == MemoryState::REQUEST_WRITE_REDIRECT) )
#else
    if ( (type == MemoryState::REQUEST_WRITE) || (type == MemoryState::REQUEST_ACQUIRE_TOKEN) || ((type == MemoryState::REQUEST_ACQUIRE_TOKEN_DATA)&&( (tokenacquired > 0)||(tokenrequested == CacheState::GetTotalTokenNum()) ) ) || (type == MemoryState::REQUEST_READ_REPLY) )
#endif
        return false;

    return true;
}

bool ST_request::IsRequestWithModifiedData()
{
#ifndef TOKEN_COHERENCE
    if ((type == MemoryState::REQUEST_REMOTE_READ_EXCLUSIVE) || (type == MemoryState::REQUEST_INVALIDATE) || (type == MemoryState::REQUEST_REMOTE_EXCLUSIVE_READ_REPLY) || (type == MemoryState::REQUEST_WRITE) || (type == MemoryState::REQUEST_WRITE_MERGE) || (type == MemoryState::REQUEST_WRITE_REDIRECT) )
#else
    if ( (type == MemoryState::REQUEST_ACQUIRE_TOKEN) || ((type == MemoryState::REQUEST_ACQUIRE_TOKEN_DATA)&&(tokenrequested == CacheState::GetTotalTokenNum())) || ((type == MemoryState::REQUEST_WRITE_REPLY)&&(bmerged)) )
#endif
        return true;

    return false;
}

bool cache_line_t::IsLineAtCompleteState()
{
#ifndef TOKEN_COHERENCE
    if ( (state == CacheState::LNINVALID) || (state == CacheState::LNREADPENDING) || (state == CacheState::LNREADPENDINGI) || (state == CacheState::LNWRITEPENDINGI) || (state == CacheState::LNWRITEPENDINGE) )
#else
    if ( (state == CacheState::CLS_INVALID) /*|| (tokencount == 0)*/ )   // JXXX maybe not good for some policies. !!! 
#endif
        return false;

#ifdef TOKEN_COHERENCE
    if ((state == CacheState::CLS_SHARER)&&(pending))
    {
        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
            if (((unsigned char)bitmask[i]) != 0xff)
                return false;
    }
    else if ((state == CacheState::CLS_OWNER)&&(tokencount == 0))
    {
        return false;
    }
#endif

    return true;
}

void reviewsystemstates()
{
    clog.flush();
    cout.flush();
    cerr.flush();

    vector<SimObj*>::iterator iter;
    for (iter=g_vSimObjs.begin();iter!=g_vSimObjs.end();iter++)
    {
        SimObj* pobj = *iter;
        pobj->ReviewState(SimObj::SO_REVIEW_DETAIL);
    }

    clog.flush();
    cout.flush();
    cerr.flush();
}

#ifdef MEM_MODULE_STATISTICS
void systemprintstatistics(ofstream& statfile)
{
    vector<SimObj*>::iterator iter;
    for (iter=g_vSimObjs.begin();iter!=g_vSimObjs.end();iter++)
    {
        SimObj* pobj = *iter;
        pobj->DumpStatistics(statfile,0,0);
    }
}
#endif

//void AutoMonitorProc()
//{
//    if (!(g_osMonitorFile.is_open()&&g_osMonitorFile.good()))
//        return;
//
//    unsigned __int64 addr = g_u64MonitorAddress;
//    //cout << hex << "Address: 0x" << addr << dec << endl;
//    vector<SimObj*>::iterator iter;
//    for (iter=g_vSimObjs.begin();iter!=g_vSimObjs.end();iter++)
//    {
//        SimObj* pobj = *iter;
//        if (!pobj->IsAssoModule())
//            continue;
//
//        // associtive modules
//        SetAssociativeProp *psap = dynamic_cast<SetAssociativeProp*>(pobj);
//        if (psap->IsDirectory())
//            g_osMonitorFile << " ^";
//        psap->MonitorAddress(g_osMonitorFile, addr);
//    }
//    g_osMonitorFile << "  @ " << sc_time_stamp() << endl;
//
//    char *linedata = (char*)malloc(g_nCacheLineSize);
//    gatherlinevalue(addr, linedata);
//
//    g_osMonitorFile << "\t";
//    for (unsigned int i=0;i<g_nCacheLineSize;i++)
//    {
//        g_osMonitorFile << hex << setw(2) << setfill('0') << (unsigned int)(unsigned char)linedata[i] << " ";
//    }
//    g_osMonitorFile << endl << endl;
//
//    free(linedata);
//}

void AutoMonitorProc()
{
    static string sString;
    string stringcycle;
    if (!(g_osMonitorFile.is_open()&&g_osMonitorFile.good()))
        return;

    unsigned __int64 addr = g_u64MonitorAddress;
    //cout << hex << "Address: 0x" << addr << dec << endl;
    vector<SimObj*>::iterator iter;
    for (iter=g_vSimObjs.begin();iter!=g_vSimObjs.end();iter++)
    {
        SimObj* pobj = *iter;
        if (!pobj->IsAssoModule())
            continue;

        // associtive modules
        SetAssociativeProp *psap = dynamic_cast<SetAssociativeProp*>(pobj);
        if (psap->IsDirectory())
            g_osMonitorFile << " ^";
        psap->MonitorAddress(g_osMonitorFile, addr);
    }
    g_osMonitorFile << "  @ " << sc_time_stamp() << endl;

    char *linedata = (char*)malloc(g_nCacheLineSize);
    gatherlinevalue(addr, linedata);

    g_osMonitorFile << "\t";
    for (unsigned int i=0;i<g_nCacheLineSize;i++)
    {
        g_osMonitorFile << hex << setw(2) << setfill('0') << (unsigned int)(unsigned char)linedata[i] << " ";
    }
    g_osMonitorFile << endl << endl;

    free(linedata);
}

#ifdef MEM_MODULE_STATISTICS
void PerformStatistics()
{
    vector<SimObj*>::iterator iter;
    for (iter=g_vSimObjs.begin();iter!=g_vSimObjs.end();iter++)
    {
        SimObj* pobj = *iter;
        pobj->Statistics(SimObj::SO_STAT_ALL);
    }

}
#endif

void monitoraddress(ostream& ofs, __address_t addr)
{
    //cout << hex << "Address: 0x" << addr << dec << endl;
    vector<SimObj*>::iterator iter;
    for (iter=g_vSimObjs.begin();iter!=g_vSimObjs.end();iter++)
    {
        SimObj* pobj = *iter;
        if (!pobj->IsAssoModule())
            continue;

        // associtive modules
        SetAssociativeProp *psap = dynamic_cast<SetAssociativeProp*>(pobj);
        if (psap->IsDirectory())
            ofs << " ^";
        psap->MonitorAddress(ofs, addr);
    }
    ofs << endl;
}

//} memory simulator namespace
}




