#ifndef MEMORY_PREDEFINE_H
#define MEMORY_PREDEFINE_H

#define _CRT_SECURE_NO_DEPRECATE 1
#define _CRT_NONSTDC_NO_DEPRECATE 1

#include <systemc.h>
#include <cassert>

//////////////////////////////////////////
// with Token coherence or not
#define TOKEN_COHERENCE
//////////////////////////////////////////

#include "dcswitch.h"

#include "../simlink/smdatatype.h"

#define SIMULATE_DATA_TRANSACTION

#define CONST_DRIVEN_MODE_RANDOM    0
#define CONST_DRIVEN_MODE_TRACE     1
#define CONST_DRIVEN_MODE_MIKE      2

#if defined(SIMULATE_DRIVEN_MODE) && (SIMULATE_DRIVEN_MODE == CONST_DRIVEN_MODE_MIKE)
    #ifndef SIMULATE_DATA_TRANSACTION
    #define SIMULATE_DATA_TRANSACTION
    #endif
#endif

//////////////////////////////////////////////////////////////////////////
#include <iomanip>
#include <vector>
#include <set>
#include <list>
#include <map>
#include "simcontrol.h"
#include <stdint.h>

using namespace std;
using namespace MemSim;
//////////////////////////////////////////////////////////////////////////

namespace MemSim{
//{ memory simulator namespace


#define INITIATOR_TABLE_SIZE    7
#define ADD_INITIATOR_BUS(req, a)       assert(req->curinitiator < INITIATOR_TABLE_SIZE); req->initiatortable[req->curinitiator] = (SimObj*)(a); req->curinitiator++
#define ADD_INITIATOR_NODE(req, a)      assert(req->curinitiator < INITIATOR_TABLE_SIZE); req->initiatortable[req->curinitiator] = (SimObj*)(a); req->curinitiator++

#define IS_NODE_INITIATOR(req, a)   (get_initiator_node(req) == (SimObj*)a)
#define IS_BUS_INITIATOR(req, a)   (get_initiator_bus(req) == (SimObj*)a)


// NOTE:
// 1. about bitmaks, token, state, dataavailable, IsLineAtCompleteState()
//    to judge whether a line has data or not always use functions or attributes, 
//    like dataavailable and IsLineAtCompleteState()
//    request with token > 0 doesn't represent data availability, 
//    since it may just collects tokens from directory.
//    lines with tokens doens't represents data availability, 
//    since it may just get token from the request described above. 
//    bitmask in local request represents the size of the data
//    bitmask in remote request represents the data availability or updated data position
//    bitmask in remote read requests represents data acquired
//    bitmaks in remote write requests represents updated data, 
//    while data availability is represented in the tokens
//    (check IsRequestWithCompleteData, IsRequestWithNoData, IsRequestWithModifiedData)
//    bitmask in read-pending lines presents availability of data
//    bitmask in write-pending lines represents updated data
//    while data availability is represented in the tokens and functions. 
//    (check IsLineAtCompleteState for details)


//typedef sc_uint<64> UINT64S;
typedef long long UINT64S;
typedef unsigned __int32 UINT32;

// define changable types
//typedef unsigned __int64 __address_t;
typedef uint64_t __address_t;

class MemoryState
{
public:
    static const __address_t MEMORY_SIZE;

    enum SIGNAL{
        SIGNAL_DEFAULT,
        SIGNAL_READ_DONE,
        SIGNAL_WRITE_DONE,
        SIGNAL_ERROR,
        SIGNAL_READ_REDIRECT,
        SIGNAL_WRITE_REDIRECT
    };

#ifdef TOKEN_COHERENCE
    enum REQUEST{
        // basic local requests
        REQUEST_NONE = 0,                       // NO: None Exist                                       non-exist request
        REQUEST_READ = 1,                       // LR: Local Read                                       local read
        REQUEST_WRITE = 2,                      // LW: Local Write                                      local write
        REQUEST_READ_REPLY = 3,                 // RR: Read Reply                                       read reply
        REQUEST_WRITE_REPLY = 4,                // WR: Write Reply -                                    write reply

        // network request
        REQUEST_ACQUIRE_TOKEN = 6,              // AT: acquire token                                    acquire token for both read and write, similar to IV
        REQUEST_ACQUIRE_TOKEN_DATA = 7,         // AD: acquire token&data                                    acquire token for both read and write, similar to RE, RS, SR, ER
        REQUEST_DISSEMINATE_TOKEN_DATA = 8,     // DD: disseminate token                                disseminate token for other cacheline, directory or memory, similar to EV, WB

        // backward invalidation
        REQUEST_INVALIDATE_BR = 9,              // IB: Invalidate request Broadcast                     broadcast invalidation to L1 caches, 
                                                // for level 1 caches

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
        Request_LOCALDIR_NOTIFICATION = 10,     // the notification is used for caches to notify local directory the changes in the token status in the cache
#endif
    };

#else
    enum REQUEST{
        // basic ST requests
        REQUEST_READ = 0,                        // LR: Local Read                                       local read
        REQUEST_WRITE = 1,                        // LW: Local Write                                      local write
        REQUEST_READ_REPLY = 2,                // RR: Read Reply                                       read reply
        REQUEST_MERGE_READ_REPLY = 3,           // MP: Merge Read Reply                                 read reply
        REQUEST_WRITE_REPLY = 4,              // WR: Write Reply -                                    write reply
        REQUEST_READ_REDIRECT = 5,              // LR: Local Read *                                     local read
        REQUEST_MERGE_READ_REDIRECT = 6,        // MR: Merge Read Redirect                              local read
        REQUEST_WRITE_REDIRECT = 7,             // LW: Local Write *                                    local_write
        REQUEST_READ_MERGE = 8,                 // RG: Merged Read                                      local read
        REQUEST_WRITE_MERGE = 9,                // WG: Merged Write -*                                  local write

        // extended transaction
        REQUEST_INVALIDATE = 10,                // IV: Invalidate                                       invalidation
                                                // DE: Data Exclusive                                    invalidation return
                                                // for level 2 caches
        REQUEST_INVALIDATE_BR = 11,             // IB: Invalidate request Broadcast                     broadcast invalidation to L1 caches, 
                                                // for level 1 caches
        REQUEST_LINE_REPLACEMENT = 12,          // RL: Line Replacement (similar to Injection)          line replacement
                                                // ???
        REQUEST_REMOTE_READ_SHARED = 13,            // RS: Remote Read to shared state                      remote read to shared
        REQUEST_REMOTE_READ_EXCLUSIVE = 14,         // RE: Remote Read to exclusive state                   remote read to exclusive
        REQUEST_REMOTE_SHARED_READ_REPLY = 15,      // SR: (to Shared State) Read Reply                        read reply to shared 
        REQUEST_REMOTE_EXCLUSIVE_READ_REPLY = 16,   // ER: (to Exclusive State) Read Reply                    read reply to exclusive
        // REQUEST_RACING_READ_REPLY,                // RER: Racing (to Exclusive State) Read Reply            racing read reply 
        // REQUEST_RACING_INVALIDATION,                // RIV: Racing invalidation                                racing invalidation
        // REQUEST_INJECT,                            // IJ: Inject request                                    inject request
        // REQUEST_DATA_EXCLUSIVE,                    // DE: Data Exclusive    ---- reduced to IV done         invalidation (acknoledgement return)
        REQUEST_WRITE_BACK = 17,                    // WB: Write Back to main memory                        write back to main memory
        REQUEST_EVICT = 18                          // EV: Eviction of shared data, will terminate at dir    eviction to of a shared data
    };
#endif

    // return the request type name
    static const char* RequestName(int requesttype, bool shortname = false);
};

class CacheState : public MemoryState
{
public:
    // replace policy
    enum REPLACE_POLICY{
        RP_LRU,    // least recently used block
        RP_RND,    // random one
        RP_FIFO    // the oldest block in the set
    };

    // write policy, hit and miss policy should be both assigned 
    enum WRITE_POLICY{
        WP_WRITE_BACK = 0,        // hit policy, BIT 0
                                // 0 : write back
                                // 1 : write through
        WP_WRITE_THROUGH = 1,

        WP_WRITE_ALLOCATE = 0,    // miss policy, BIT 1
                                // 0 : write allocate
                                // 1 : write around 
        WP_WRITE_AROUND = 2
    };

    // cache line mask used for cacheline state transition
    enum CACHE_LINE_MASK{
        LNMRRW = 4,         // Read Reply Waiting bit
        // bin: 0100
        LNMIAW = 8          // Invalidation Acknowledgement Waiting bit
        // bin: 1000
    };

#ifndef TOKEN_COHERENCE
    // cacheline state
    enum CACHE_LINE_STATE{
        LNINVALID = 0,      // The line is invalid
                            // bin: 0000
                            // used in all the protocols
                            // shortname: I or "-" as a symbol

        LNVALID = 1,        // The line is valid 
                            // bin: 0001    
                            // used in valid-invalid protocol
                            // shortname: V

        LNSHARED = 1,       // The line is in Shared state
                            // SHARED bit | !RRW | !IAW
                            // bin: 0001
                            // used in MOSI protocol
                            // shortname: S

        LNMODIFIED = 2,     // The line is in modified state
                            // MODIFIED bit | !RRW | !IAW
                            // bin: 0010
                            // used in MOSI protocol
                            // shortname: M

        LNOWNED = 3,        // Owned state
                            // Owned state.... *** 
                            // used in MOSI protocol
                            // shortname: O

        //LNEXCLUSIVE = 4,    // Exclusive state
        //                    // ....
        //                    // Used in MOSI protocol
                              // shortname: E

        LNREADPENDING = 5,  // Waiting for the answer from a remote read request
                            // ???SHARED | RRW
                            // used in MOSI protocol
                            // shortname: R

        LNREADPENDINGI = 6, // Waiting for the answer from a remote read request with Invalid data
                            // ???SHARED | RRW
                            // used in MOSI protocol
                            // shortname: T

        LNWRITEPENDINGI = 7,// Waiting for an invalidation to be passed around with Invalid data
                            // ???MODIFIED | RRW or MODIFIED | IAW or MODIFIED | RRW | IAW
                            // used in MOSI protocol
                            // shortname: P

        LNWRITEPENDINGM = 8,// Waiting for an invalidation to be passed around with Modified data
                            // ???MODIFIED | RRW or MODIFIED | IAW or MODIFIED | RRW | IAW
                            // used in MOSI protocol
                            // shortname: W

        LNWRITEPENDINGE = 9,// Waiting for an invalidation/exclusive read to be passed around with Invalid data
                            // This state indicate that the line is invalidated by another IV/RE/ER to the same line
                            // and this line is being invalidated, however waiting for the reply from its own. 
                            // After its reply comes back, the line can be then changed to INVALID state. 
                            // ?? 
                            // used in MOSI protocol

        //LNWRITEPENDINGO = 9 // Waiting for an invalidation to be passed around with Owned data
        //                    // ???MODIFIED | RRW or MODIFIED | IAW or MODIFIED | RRW | IAW
        //                    // used in MOESI protocol
  //                          // shortname: U
    };

    // Directory line state
    enum DIRECTORY_LINE_STATE{
        DRINVALID,      // the sub-group doessn't have the data
                        // shortname: I or "-" as a symbol
        //DRRESERVED,     // the sub-group has request going on now. 
                        // current directory has the count equal to 0. 
                        // however, the aux flag and queuehead and queuetail are not empty
                        // shortname: R
        DRSHARED,       // the sub-group has the data
                        // normal dir:  has shared data owned data owned data, but probably other sub-group also has the copy
                        // root dir:    has shared non-dirty data
                        // shortname: S
        DREXCLUSIVE     // the sub-group has the data
                        // normal dir:  only the sub-group has the data
                        // root dir:    has dirty data
                        // shortname: E
    };
#else
    // cacheline state
    enum CACHE_LINE_STATE{
        CLS_INVALID = 0,    // invalid state
        CLS_SHARER = 1,     // normal: for shared, non-dirty
        CLS_OWNER = 2,      // Owner of the data, normally means dirty data 
    };

    // dirline state
    enum DIR_LINE_STATE{
        DLS_INVALID = 0,    // invalid, for invalid and sharing
        DLS_CACHED = 1,     // normal; group has the data
//        DLS_SHARER = 1,     // Normal, for invalid and sharing
//        DLS_OWNER = 2       // Owner, for dirty and owned
    };
 
#endif // not token coherence

    // Cacheline update method
    enum LINE_UPDATE_METHOD{
        LUM_NO_UPDATE = 0,            // no update necessary for the data
                                    // for instance a reed at any state will not incur an update
        LUM_STORE_UPDATE = 1,       // local store update
                                    // update will clear the bitmask to only the request mask
        LUM_PRIMARY_UPDATE = 2,     // primary update will update 
                                    // when a local write reply has been received
                                    // the newly input are the most recent
                                    // and input data will be kept
                                    // * this is an incremental update
        LUM_INCRE_OVERWRITE = 2,    // same as primary update
                                    // ** primary(incre-overwrite) update will update the empty and 
                                    // ** occupied slots. it does not guarantee a complete cacheline
        LUM_FEEDBACK_UPDATE = 3,    // feedback update when a reply 
                                    // from the network side has been received
                                    // the data in the cacheline are the most recent
                                    // the data in the line will be kept
                                    // * this will normally make the line complete
        LUM_INCRE_COMPLETION = 3,   // same as feedback update
                                    // ** feedback(incre-completion) update will update the empty 
                                    // ** slots and completes the cacheline. available data will not be touched

        LUM_CLR_PRIMARY_UPDATE=4,   // clear the bitmask first before carrying out the primary update
                                    // for WP states only

        LUM_NOM_FEEDBACK_UPDATE=5,  // non-mask feedback update
                                    // feedback update without updating the bitmasks 
                                    // used for updating the WPI (or WPM, but this might not be necessary )lines, but without changing the masks
                                    // this is used when WPI met with remote SR requests

        LUM_RAC_FEEDBACK_UPDATE=6,    // racing feedback update
                                    // WPI or WPM lines will not be updated with data, but not the whole bitmask. 
                                    // instead only the updated part of the newly written data carried by the IV, RE, ER are incremented
    };

    // Request update method
    enum REQUEST_UPDATE_METHOD{
        RUM_ALL,                    // update the request regardless the mask bits in the request
        RUM_NON_MASK,                // update the request only on the bits unmasked
        RUM_MASK,                    // update the request only on the bits masked
        RUM_NONE                    // update nothing in the request
    };

    enum PROTOCOL_TYPE{
        PT_VI = 0,                  // V-I protocol
        PT_MOSI = 1,                // MOSI-ext protocol
    //    PT_TOKEN = 2                // Token cohernence protocol
    };

//#if defined(SIMULATE_DRIVEN_MODE) && (SIMULATE_DRIVEN_MODE == CONST_DRIVEN_MODE_MIKE)
    // backward broadcast invalidation policy
    enum BACKWARD_BRAODCAST_INVALIDATION{
        BBI_EAGER,                  // backward invalidation EAGER policy
                                    // always keep consistency between L2 cache and the corresponding L1 caches
                                    // whenever a line in L2 is invalidated, evicted or written back, (anyway disappear)
                                    // an backward invalidation will have to be delivered to L1 caches associated
                                    // in this scheme, if the IV, RE, or ER misses the L2 cache. 
                                    // the backward broadcast invalidation will not be sent

        BBI_LAZY                    // backward invalidation LAZY policy
                                    // not always keep the consistency between L2 and L1 caches
                                    // when a line in L2 is evicted or written back
                                    // the backward broadcast invalidation will not be sent
                                    // however all the IV, RE, or ER will be delivered back to L1 caches
                                    // no matter it's a miss or a hit on the target. 
                                    // read snooping on the bus could reduce local read 


                                    // *** the read snooping on the bus is useful for both policies
                                    // be cautious about both policy *** !!!
    };

    enum INJECTION_POLICY{
        IP_NONE = 0,                    // NO INJECTION AT ALL
        IP_EMPTY_1EJ = 1                // INJECT INTO EMPTY CACHES, 
                                        // eject out immediately when meeting directory
                                        // no inject into other local levels
    };
//#endif


    // Racing situation
    // ER will not pickup the newly written data from the WPI or WPM 
    // IV or RE or ER is responsible to bring the new data to the target around the ring. 
    // the updated value are kept in cacheline and marked by the bitmasks for WPI or WPM lines

#ifndef TOKEN_COHERENCE
    // return the name of a certain cache state
    // cache type:  PT_VI:      represents V-I protocol
    //              PT_MOSI:    represents MOSI-ext protocol
    const char* CacheStateName(int cachestate, int cachetype, bool shortname = false);
#else
    // get cache state name with pre-allocated buffer
    char* CacheStateName(CACHE_LINE_STATE nlinestate, unsigned int ntoken, bool pending, bool invalidated, bool priority, bool tlock, char* pstatename, bool shortname = false);
    char* CacheStateName(CACHE_LINE_STATE nlinestate, unsigned int ntoken, bool pending, bool invalidated, bool priority, bool tlock, bool msbhit, bool msblock, char* pstatename, bool shortname = false);
    char* DirectoryStateName(DIR_LINE_STATE nlinestate, unsigned int ntoken, bool breserved, bool priority, char* pstatename, bool shortname = false);
#endif

private:
    static unsigned int s_nTotalToken;

public:
    static void SetTotalTokenNum(unsigned int ntoken){s_nTotalToken = ntoken;}
    static unsigned int GetTotalTokenNum(){return s_nTotalToken;};
};

// 16 X 4 bytyes can be covered for a cacheline, which is a 64-byte cacheline
//#define CACHE_BIT_MASK_WIDTH    16    // for totally 16 bits are used for the mask
//#define CACHE_REQUEST_ALIGNMENT 4        // 4-byte alignment

// for a 128 X 1 byte cacheline
//#define CACHE_BIT_MASK_WIDTH    128        // for totally 128 bits are used for the mask
//#define CACHE_REQUEST_ALIGNMENT 1        // 1-byte alignment

// for a 64 X 1 byte cacheline
#define CACHE_BIT_MASK_WIDTH    64        // for totally 128 bits are used for the mask
#define CACHE_REQUEST_ALIGNMENT 1        // 1-byte alignment

// the bit width size of the cacheline
extern unsigned int &g_nCacheLineWidth;

// the line size of the cacheline
extern unsigned int &g_nCacheLineSize;

//extern struct _cache_set;
struct _ST_request;



typedef struct _cache_line
{
    __address_t getlineaddress(unsigned int nset, unsigned int nsetbits){return ((tag << (g_nCacheLineWidth+nsetbits))|(nset<<g_nCacheLineWidth));};
    bool islinecomplete(){  // only check the line bitmasks
        for (int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++) 
            if (bitmask[i] != (char)0xff)
                return false;
        return true;
    };
    __address_t tag;
    sc_time time;
    CacheState::CACHE_LINE_STATE state;

#ifdef SIMULATE_DATA_TRANSACTION
    char *data;
#endif

    char bitmask[CACHE_BIT_MASK_WIDTH/8];       // bit mask is defined to hold written data before reply comes back
                                                // it does not always represent the validness of word segment within the line
                                                // for WP states, bitmask only indicates the newly written data. 
    void removemask(){for(int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++) bitmask[i]=0;};

    unsigned int queuehead;
    unsigned int queuetail;

    unsigned int tokencount;
    unsigned int gettokenglobalvisible()
    {
        if (invalidated)
            return 0;
        if (tlock)
            return 0;

        return tokencount;
    }

    unsigned int gettokenlocalvisible()
    {
        return tokencount;
    }

    unsigned int gettokenlocked()
    {
        if (tlock)
            return tokencount;

        return 0;
    }

    // invalidated is focusing on state
    bool invalidated;   // whether the line is already invalidated
                        // invalidated-line's token will not count. 
                        // or say they are counted as zero to remote request. 
                        // but to local request, they are still available,
                        // which can easily represent the validness of data
                        // without consulting the local bit-mask.
                        // thus, to remote request, 
                        // invalidated lines will not change their token number. 

    bool priority;      // priority flag represent priority token in the system, 
                        // which in this system will facilitate the invalidation
                        // similar to micro06-ring paper

    bool breserved;     // reserved for special purpose. 
                        // for instance the owner-ev to R will transfer the R to W state
                        // however it shouldn't be carried out directly, but with reserved flag
                        // RESERVED 

    // tlock is focusing on token
    // when tlock == true, invalidated must be true
    bool tlock;         // token lock represent that all the tokens in the line are locked, 
                        // which can only be unlocked by priority token

    bool pending;       // pending request, either read or write pending

private:
    bool llock;         // prevent the pending line from being further accessed
                        // currently used only for lines in the merge buffer

public:
    void SetLineLock(bool lock){llock = lock;}          // currently only used for lines in the merge buffer
    bool IsLineLocked(struct _ST_request* req = NULL){return llock;}         // currently only used for lines in the merge buffer

    // check whether the line state signify the line is complete
    bool IsLineAtCompleteState();
} cache_line_t;

typedef struct _dir_line
{
    __address_t getlineaddress(unsigned int nset, unsigned int nsetbits){return ((tag << (g_nCacheLineWidth+nsetbits))|(nset<<g_nCacheLineWidth));};
    __address_t tag;
    sc_time time;
    //char state;
    CacheState::DIR_LINE_STATE state;
    bool breserved;     // reserved flag represents the cacheline cannot be processed immediately
                        // there are requests suspended on the line, 
                        // either being processed in memory system 
                        // or queued in the linked list buffer
                        // any state with reserved flag will be queued in the linked list
                        // INVALID <= RS/RE : reserved CA/EX (no queue)
                        // reserved CA/EX <= normal requests : perform default action as no reserved flag, 
                        //                                     but queue requests afterwards
                        // reserved CA/EX <= EV/WB : reduce counter and perform action, but no more queue
                        // reserved CA/EX <= counter == 0 still have queue : change to reserved INVALID
                        // reserved INVALID <= normal incoming requests : perform default action, but queue requests
                        //                                                set a flag in the request to indicate 
                        //                                                the request is newline request (even same address)
                        // reserved *** <= without judging from the line type, if the request is newline request : 
                        //                                                the request will be send to MEM, at the moment, 
                        //                                                request type should be RS/RE, and line state should be res-INV
                        //                                                remove the line from the line queue
                        // reserved *** <= without judging from the line type, none queued requests are available (all queued requests were processed) : remove reserved flag
                        // *** for non-root directory, the flag represent pending.

    unsigned int tokencount;    // the number of tokens that the directory itself has
    unsigned int tokengroup;    // the number of tokens that the local group has
                                // maybe this is not necessary for root directory
                                // local directory uses ntokenline and ntokenrem for the purpose

    bool bdataavailable;        // represent whether the data is available in the local group
                                // when the flag is true, the data must be there, 
                                // when the flag is false, the data might not be there

    bool priority;              // represent the priority token

#ifdef MEMSIM_DIRECTORY_REQUEST_COUNTING
    unsigned int nrequestin;    // remote request in local level
                                // with remote request inside the local level,
                                // the directory line can still be evicted.
                                // * DD will not be counted in any way
                                // * since it might never get out the directory.
                                // * to avoid inform the directory when consumed, it will not be counted
                                // * this means that when nrequestin reaches 0, 
                                // tokengroup might not be 0
    unsigned int nrequestout;   // local requests in global level

    int ntokenline;     // tokens inside on the lines
                                 // *** in naive directory scheme
                                 // *** the tokencount represent the token directory hold, 
                                 // *** token group holds the tokens that local caches has 
                                 // *** the two have no intersections in local directory. 

    int ntokenrem;     // tokens inside from remote request
    bool grouppriority;         // local group priority, excluding tokens held by directory 

#endif

    unsigned int counter;
    char aux;       // some additional state for extension
                    // auxiliary loading means data is being read from memory, 
                    // auxiliary defer means requests are suspended on the line
                    // * write-back will not result in an auxiliary state
                    // * loading line might have a queue
                    // * deferred line must have a queue
                    // ** deferred lines is shown in line queue
                    // ** loading lines are not present in the line queue

                    // newly arrived requests will check the corresponding line
                    // if it's defer flag or loading flag, the request will be suspended in the request queue

    unsigned int queuehead; // head of the suspended request queue

    unsigned int queuetail; // tail of the suspended request queue, used to append new request

    unsigned int setid;     // just to make it simpler to find set id;

//#ifdef SIMULATE_DATA_TRANSACTION
//    char *data;
//#endif

    //char bitmask[CACHE_BIT_MASK_WIDTH/8];     // bit mask is defined to hold written data before reply comes back
    const char* StateName(bool root = true, bool shortname = false);
} dir_line_t;


typedef struct _cache_set
{
    cache_line_t *lines;
} cache_set_t;

typedef struct _dir_set
{
    dir_line_t *lines;
} dir_set_t;

int lg2(int n);
void validatename(char *name);

/*
#ifdef WIN32
extern class SimObj;
#else
class SimObj;
#endif
*/

// request->data always starts from the line-aligned address
// so the request data may actually start from the middle
typedef struct _ST_request
{
    _ST_request(){initiatortable = new SimObj*[INITIATOR_TABLE_SIZE]; data = (char*)calloc(s_nRequestAlignedSize, sizeof(char)); curinitiator = 0; ref = NULL;dcode=0;bqueued=false;bnewline=false;bprocessed=false;tokenrequested=0;tokenacquired=0;dataavailable=false;bpriority=false;btransient=false;bmerged=false; for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++) bitmask[i]=0;
#ifndef MEM_CACHE_LEVEL_ONE_SNOOP
        bbackinv=false;
#endif
    };
    _ST_request(struct _ST_request* req){
        *this=*req;initiatortable = new SimObj*[INITIATOR_TABLE_SIZE]; /*_ST_request();*/
        data = (char*)malloc(s_nRequestAlignedSize); memcpy(data, req->data, s_nRequestAlignedSize);
        memcpy(initiatortable, req->initiatortable, INITIATOR_TABLE_SIZE*sizeof(SimObj*));
        curinitiator = req->curinitiator;ref=NULL;bqueued=false;bpriority=false;btransient=false;
    };
    ~_ST_request(){delete[] initiatortable;free(data);};

    static unsigned int s_nRequestAlignedSize;
    __address_t getlineaddress(unsigned nlinebit){return (addresspre << nlinebit);};
    __address_t getlineaddress(){return (addresspre << g_nCacheLineWidth);};
    __address_t getreqaddress(unsigned nlinebit){return (addresspre << nlinebit)+offset;};
    __address_t getreqaddress(){return (addresspre << g_nCacheLineWidth)+offset;};

    SimObj **initiatortable;
    unsigned int curinitiator;
    //Feedback_if* initiator;
    unsigned int pid;
    unsigned int familyID;
    MemoryState::REQUEST type;

    // request from and to the processors uses addresspre, nsize, offset to determine the address range 
    __address_t addresspre;
    unsigned int nsize;     // number of UINT32 words
    unsigned int offset;    // number of UINT32 words

    // request on the network uses addresspre, nsize, offset to determine the address range 
    char bitmask[CACHE_BIT_MASK_WIDTH/8];       // bit mask to identify the valid data segments of a request
    
    //__address_t address;
    //__address_t oriaddress;
    //unsigned size;      // number of UINT32 words
    //unsigned orisize;      // number of UINT32 words
    //char data[CONST_REQUEST_DATA_SIZE];    // line size alert
    char *data;         // line size alert
    unsigned long* ref; // reference point to the request in MG simulator
    bool    bqueued;    // JXXX this property is only used for cache fetching and queueing purpose
                        // it should not be a part of the request
                        // this property should be initialized as false

    bool    bprocessed; // this flag represent the request is preprocessed 
                        // *** in COUTING mechanism, the flag is representing repeated access requests

    bool    bnewline;   // represent this request will be a request on a newline on the directory
                        // which means the directory at the moment should have a invalid (empty) line 
                        // and the request should be able to grab the line and occupy it with new address
                        // the newline flag should only be set on RS/RE requests

#ifndef MEM_CACHE_LEVEL_ONE_SNOOP
    bool    bbackinv;   // this flag also used for WRITE_REPLY backward broadcast decision

    unsigned char refcount; // reference counter for broadcasting requests IB/WR_BR
#endif

    bool bpriority;     // represent priority token for the ring based protocol
                        // similar to micro06-ring

    bool btransient;    // represents that token request has has only transient tokens
    unsigned int dcode;    // the code are necessary for directory to decode
                        // code can be updated by caches and directory
                        // [bit 0] indicator u-a - indicator unavailable: 
                        //     indicate the original cache will become READPENDINGI 
                        //     and the data will be self-invalidated after use
                        // [bit 1] indicator a-a - indicator already-available: 
                        //     indicate the cache was already counted by the directory
                        //     and the directory should correct its counter value before processing the req
                        // default code for both indicators is always 0b00
                        //////////////////////////////////////////////////////////////////////////
                        // indicator u-a is set when:
                        // a. when RS request passes a WPI
                        // b. when SR request passes a WPM
                        // the indicator u-a is handled by the directory like this
                        // a. directory will not increase the counter when coded RS or SR passes
                        // 
                        // indicator a-a is set when
                        // a. when RS returns ReadPending again
                        // the indicator a-a is handled by the directory as follows
                        // a. the directory will correct the counter and maybe even state if necessary
                        //
                        // when BOTH indicators are set:
                        // only u-a indicator will be dealt with as above
                        // both indicator will be reset

    bool bmerged;       // merged request only used for msb

    double starttime;   // for statistics
    unsigned int tokenacquired;     // normally means token acquired
                                    // for EV, WB request, it means token held by request
    unsigned int tokenrequested;    // normally means token requested
                                    // for EV, WB requests, 0 means EV, TotalTokenNumber means WB

    // transient tokens cannot be grabbed by anybody, but can be transformed into permanent token by priority token
    // permanent tokens can be stored and grabed by the lines.
    unsigned int gettokenpermanent()
    {
        if (btransient == false)
            return tokenacquired;

        return 0;
    }

    bool dataavailable; // for network request, to represent whether it's a reply or not
    bool dirty;         // represent whether the request carry dirty data

    bool IsIndUASet(){return ((dcode&0x01) != 0);};
    bool IsIndAASet(){return ((dcode&0x02) != 0);};
    void SetIndUA(bool status=true){dcode &= (~0x01); dcode |= (status?0x01:0);};
    void SetIndAA(bool status=true){dcode &= (~0x02); dcode |= (status?0x02:0);};

    // convert the request information into text
    // the buffer for ptext should be preallocated.
    // reutrn the string pointer, same as ptext
    char* RequestInfo2Text(char* ptext, bool shortversion=true, bool withdata=false, bool bprint=false);

    // check whether the request state signify the request is complete
    bool IsRequestWithCompleteData();

    // check whether the request state indicates no data 
    bool IsRequestWithNoData();

    // check whether the request state indicates newly modified data (RE, ER, IV)
    bool IsRequestWithModifiedData();

    // only for debugging purpose
    void clear(){addresspre = 0;nsize = 0;offset = 0;data = NULL;type = MemoryState::REQUEST_NONE;curinitiator = 0; ref = NULL;dcode=0;bqueued=false;bnewline=false;bprocessed=false;tokenrequested=0;tokenacquired=0;dataavailable=false;bpriority=false;btransient=false;bmerged=false;}

    // convert the processor format to bit vector format
    bool Conform2BitVecFormat();

    // convert the bit-vector format to processor format
    bool Conform2SizeFormat();

} ST_request;

void print_cline_state(CacheState::CACHE_LINE_STATE state, bool bshort = true);
void print_dline_state(CacheState::DIR_LINE_STATE state, bool broot = true, bool bshort = true);

void print_cline(cache_line_t* line, bool bshort = true);
void print_dline(cache_line_t* line, bool broot = true, bool bshort = true);

void print_cline_data(cache_line_t*);
void print_request_type(ST_request* req);
void print_request(ST_request* req, bool popped = false);       // popped: print popped initiator with request



// pipeline register array
 typedef struct __pipeline_t{
   typedef vector<ST_request*> lst_t;

   const lst_t& getlst(void) const { return m_lstRegisters; }

   unsigned front_i(void) const { return current; }
   unsigned back_i(void) const { 
     if (current == 0) return m_lstRegisters.size()-1; 
     else return current-1;
   }
   void shift_i(void) {
     ++current;
     if (current >= m_lstRegisters.size())
       current = 0;
   }

   void copy(lst_t& other) const {
     unsigned i;
     for (i = current; i < m_lstRegisters.size(); ++i)
       other.push_back(m_lstRegisters[i]);
     for (i = 0; i < current; ++i) 
       other.push_back(m_lstRegisters[i]);
   }

   void reset(const lst_t& other) {
     assert(other.size() == m_lstRegisters.size());
     m_lstRegisters = other;
     current = 0;
   }

   __pipeline_t(unsigned int n) : current(0) { 
     m_lstRegisters.resize(n); 
     for (unsigned int i = 0; i < n; ++i) m_lstRegisters[i] = 0; 
   }

   ST_request* shift(ST_request* req) {
     ST_request* t = m_lstRegisters[front_i()];
     shift_i();
     m_lstRegisters[back_i()] = req;
     return t;
   }

   ST_request* top() const {
     return m_lstRegisters[front_i()]; 
   }
   ST_request* bottom() const {
     return m_lstRegisters[back_i()];
   }
   void print() {
     unsigned i;
     unsigned j;
     for (i = current, j = 0; i < m_lstRegisters.size(); ++i, ++j)
       {
	 clog << '[' << j << "] ";
	 if (m_lstRegisters[i] == 0)
	   clog << NULL << endl;
	 else
	   print_request(m_lstRegisters[i]);
       }
     for (i = 0; i < current; ++i, ++j) {
	 clog << '[' << j << "] ";
	 if (m_lstRegisters[i] == 0)
	   clog << NULL << endl;
	 else
	   print_request(m_lstRegisters[i]);
     }
   }

   protected:
   vector<ST_request*> m_lstRegisters;
   unsigned current;


 } pipeline_t;

 /*
typedef struct __pipeline_t{
    list<ST_request*>   m_lstRegisters;
    unsigned int        m_nSize;

    // pipeline registers initialized with NULL
    __pipeline_t(unsigned int n){m_nSize = n; for(unsigned int i=0;i<n;i++) m_lstRegisters.push_back(NULL);}
    ST_request* shift(ST_request* req){
        assert(m_lstRegisters.size() == m_nSize);
        m_lstRegisters.push_back(req);
        ST_request* ret = m_lstRegisters.front();
        m_lstRegisters.pop_front();
        return ret;
    }
    ST_request* top(){assert(m_lstRegisters.size() == m_nSize);return m_lstRegisters.front();}
    ST_request* bottom(){assert(m_lstRegisters.size() == m_nSize);return m_lstRegisters.back();}
    void print(){
        list<ST_request*>::iterator iter;
        int i=0;
        for (iter=m_lstRegisters.begin();iter!=m_lstRegisters.end();iter++,i++)
        {
            clog << "[" << i << "] ";
            if (*iter == NULL)
                clog << "NULL" << endl;
            else 
                print_request(*iter);
        }
    }
} pipeline_t;
 */



#define NOTIFY_PORT(port) ((sc_event &)port.default_event()).notify()

#define LOG_HEAD_OUTPUT sc_time_stamp() << "# " << name() << "." << __FUNCTION__ << "." << dec << __LINE__ << ": " 
#define LOGN_HEAD_OUTPUT sc_time_stamp() << "# " << __FUNCTION__ << "." << dec << __LINE__ << ": " 
#define ERR_HEAD_OUTPUT sc_time_stamp() << "[E] " << __FUNCTION__ << "."<< dec << __LINE__ << ": " 
#define TMP_HEAD_OUTPUT sc_time_stamp() << " ***T*** " 
#define TMP_REQBEG_PUTPUT sc_time_stamp() << " ***T*** ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ \n" 
#define TMP_REQEND_PUTPUT sc_time_stamp() << " ***T*** -------------------------------------------------------------- \n" 

// 32 bit alert 
#define FMT_ADDR(addr) hex << setfill('0') << setw(8) << addr
// 32 bit alert 
#define FMT_DTA(data) hex << setfill('0') << setw(8) << data

// widthed data
#define FMT_WID_DTA(data, wid) hex << setfill('0') << setw(wid) << data

// 16 bit alert
#define FMT_SETIDX(set) hex << setfill('0') << setw(4) << set
// 64 bit alert
#define FMT_TAG(tag) hex << setfill('0') << setw(16) << tag

void generate_random_data(char *data, unsigned int size = 1);
//void generate_random_data(UINT32 *data, unsigned int size = 1);

SimObj* get_initiator_bus(ST_request* req, bool popped = false);      // prev: whether to get popped initiator 
SimObj* get_initiator_node(ST_request* req, bool popped = false);     // prev: whether to get popped initiator

SimObj* pop_initiator_bus(ST_request* req);
SimObj* pop_initiator_node(ST_request* req);

extern vector<SimObj*>  g_vSimObjs;
extern vector<SimObj*>  &g_vecAllSimObjs;
void reviewsystemstates();
#ifdef MEM_MODULE_STATISTICS
void systemprintstatistics(ofstream& statfile);
#endif
void monitoraddress(ostream&, __address_t);

void AutoMonitorProc();

#ifdef MEM_MODULE_STATISTICS
void PerformStatistics();
#endif


//extern unsigned int nib;
//extern vector<ST_request*> vecpib;

// COLOR DEFINITION
// BACK GROUND COLORS ARE NOT DEFINED IN THE FOLLOWING TEXT

#define SPECIFIED_COLOR_OUTPUT

#ifdef WIN32
#include <windows.h>
#define __TEXT_BLACK                0
#define __TEXT_RED                  FOREGROUND_RED
#define __TEXT_GREEN                FOREGROUND_GREEN
#define __TEXT_BLUE                 FOREGROUND_BLUE
#define __TEXT_YELLOW               FOREGROUND_GREEN|FOREGROUND_RED
#define __TEXT_MAGENTA              FOREGROUND_BLUE|FOREGROUND_RED
#define __TEXT_CYAN                 FOREGROUND_BLUE|FOREGROUND_GREEN
#define __TEXT_WHITE                FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_BLUE
#define __TEXT_HIGHLIGHT            FOREGROUND_INTENSITY

extern HANDLE __hColorConsole;

#define __TEXTCOLOR(outps,color,bhigh)  do{\
                                        __hColorConsole = GetStdHandle(STD_OUTPUT_HANDLE);\
                                        SetConsoleTextAttribute(__hColorConsole, color|(bhigh?FOREGROUND_INTENSITY:0));\
                                        }while(0)

#define __TEXTCOLORNORMAL()             do{\
                                        __hColorConsole = GetStdHandle(STD_OUTPUT_HANDLE);\
                                        SetConsoleTextAttribute(__hColorConsole, __TEXT_WHITE);\
                                        }while(0)

#ifdef SPECIFIED_COLOR_OUTPUT
#define __TEXTCOLOR_GRAY()             __TEXTCOLOR(clog,__TEXT_BLACK,true)
#define __TEXTCOLOR_CYAN()             __TEXTCOLOR(clog, __TEXT_CYAN, true)
#define __TEXTCOLOR_YELLOW()         __TEXTCOLOR(clog, __TEXT_YELLOW, false)
#define __TEXTCOLOR_RED()             __TEXTCOLOR(clog,__TEXT_RED,true)
#define __TEXTCOLOR_GREEN()             __TEXTCOLOR(clog,__TEXT_GREEN,true)
#endif

#else
#define __TEXT_BLACK                30
#define __TEXT_RED                  31
#define __TEXT_GREEN                32
#define __TEXT_BLUE                 34
#define __TEXT_YELLOW               33
#define __TEXT_MAGENTA              35
#define __TEXT_CYAN                 36
#define __TEXT_WHITE                37

// extra needs to be defined to make it work with printf
#define __TEXTCOLOR(outfs,color,bhigh)  outfs << "\033[" << (bhigh?1:0) <<";" << color << "m"
//#define __TEXTCOLOR(outfs,color,bhigh)  do{}while(0)
//#define __TEXTCOLOR(outfs,color,bhigh)  do{clog.flush();clog << "\033[0;"<<32<<"m";clog.flush();}while(0)
//#define __TEXTCOLOR(outfs,color,bhigh)  std::clog << "\033[0;" << 32 << "m" << "a"
//#define __TEXTCOLOR(outfs,color,bhigh)  std::clog << "\033[0;" << 32 << "m"

//#define __TEXTCOLORNORMAL()             do{cout << "\033[0;" << __TEXT_WHITE << "m"; clog << "\033[0;" << __TEXT_WHITE << "m";} while(0)

//#ifdef SPECIFIED_COLOR_OUTPUT
//#define __TEXTCOLOR_GRAY()             std::clog << "\033[1;30m"
//#define __TEXTCOLOR_CYAN()             std::clog << "\033[1;36m"
//#define __TEXTCOLOR_YELLOW()         std::clog << "\033[0;33m"
//#define __TEXTCOLOR_RED()             std::clog << "\033[1;31m"
//#define __TEXTCOLOR_GREEN()             std::clog << "\033[1;32m"
//#endif

// non color output in linux
#ifdef SPECIFIED_COLOR_OUTPUT
#define __TEXTCOLOR_GRAY()             std::clog << ""
#define __TEXTCOLOR_CYAN()             std::clog << ""
#define __TEXTCOLOR_YELLOW()         std::clog << ""
#define __TEXTCOLOR_RED()             std::clog << ""
#define __TEXTCOLOR_GREEN()             std::clog << ""

#define __TEXTCOLORNORMAL()             do{\
}while(0)

#endif


#endif  //color

//} memory simulator namespace
}

#endif  //headerfile

