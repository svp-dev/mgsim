#ifndef MEMORY_PREDEFINE_H
#define MEMORY_PREDEFINE_H

#include <systemc.h>
#include <cassert>
#include <iomanip>
#include <vector>
#include <set>
#include <list>
#include <map>
#include "simcontrol.h"
#include <stdint.h>

using namespace std;

namespace MemSim
{

#define INITIATOR_TABLE_SIZE    7

#define ADD_INITIATOR(req, a) \
    assert(req->curinitiator < INITIATOR_TABLE_SIZE); \
    req->initiatortable[req->curinitiator] = (SimObj*)(a); \
    req->curinitiator++

#define IS_INITIATOR(req, a)   (get_initiator(req)  == (SimObj*)(a))


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

typedef uint64_t __address_t;

class MemoryState
{
public:
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

        Request_LOCALDIR_NOTIFICATION = 10,     // the notification is used for caches to notify local directory the changes in the token status in the cache
    };
};

class CacheState : public MemoryState
{
public:
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
    };
 
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

    enum INJECTION_POLICY{
        IP_NONE = 0,                    // NO INJECTION AT ALL
        IP_EMPTY_1EJ = 1                // INJECT INTO EMPTY CACHES, 
                                        // eject out immediately when meeting directory
                                        // no inject into other local levels
    };

    // Racing situation
    // ER will not pickup the newly written data from the WPI or WPM 
    // IV or RE or ER is responsible to bring the new data to the target around the ring. 
    // the updated value are kept in cacheline and marked by the bitmasks for WPI or WPM lines

private:
    static unsigned int s_nTotalToken;

public:
    static void SetTotalTokenNum(unsigned int ntoken) { s_nTotalToken = ntoken; }
    static unsigned int GetTotalTokenNum() { return s_nTotalToken; }
};

// for a 64 X 1 byte cacheline
#define CACHE_BIT_MASK_WIDTH    64      // for totally 128 bits are used for the mask
#define CACHE_REQUEST_ALIGNMENT 1       // 1-byte alignment

// the line size of the cacheline
extern unsigned int g_nCacheLineSize;

struct ST_request;

struct cache_line_t
{
    __address_t tag;
    sc_time     time;
    CacheState::CACHE_LINE_STATE state;

    char *data;
    char  bitmask[CACHE_BIT_MASK_WIDTH/8];      // bit mask is defined to hold written data before reply comes back
                                                // it does not always represent the validness of word segment within the line
                                                // for WP states, bitmask only indicates the newly written data. 

    __address_t getlineaddress(unsigned int nset, unsigned int nsetbits) const
    {
        return ((tag << nsetbits) + nset) * g_nCacheLineSize;
    };
    
    bool islinecomplete() const
    {
        for (int i = 0; i < CACHE_BIT_MASK_WIDTH/8; ++i) 
            if (bitmask[i] != (char)0xff)
                return false;
        return true;
    };
    
    void removemask()
    {
        for(int i = 0; i < CACHE_BIT_MASK_WIDTH/8; ++i)
            bitmask[i] = 0;
    };

    unsigned int tokencount;
    
    unsigned int gettokenglobalvisible() const
    {
        if (invalidated) return 0;
        if (tlock) return 0;
        return tokencount;
    }

    unsigned int gettokenlocalvisible() const
    {
        return tokencount;
    }

    unsigned int gettokenlocked() const
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

    bool llock;         // prevent the pending line from being further accessed
                        // currently used only for lines in the merge buffer

    // check whether the line state signify the line is complete
    bool IsLineAtCompleteState();
};

struct dir_line_t
{
    __address_t getlineaddress(unsigned int nset, unsigned int nsetbits, unsigned int nsplitbits)
    {
        return ((tag << (nsetbits + nsplitbits)) + nset) * g_nCacheLineSize;
    };
    __address_t tag;
    sc_time time;
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

    bool bskipdispatch;         // if true, skip dispatching the request to the memory module
                                // when a request returns without any token or data (uncessful).
                                // if false, the failed request should be sent to the memory directly.

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
};

struct dir_set_t
{
    dir_line_t *lines;
};

int lg2(int n);

// request->data always starts from the line-aligned address
// so the request data may actually start from the middle
struct ST_request
{
    ST_request()
    {
        data = (char*)calloc(s_nRequestAlignedSize, sizeof(char));
        curinitiator = 0;
        bqueued=false;
        msbcopy=NULL;
        bprocessed=false;
        tokenrequested=0;
        tokenacquired=0;
        dataavailable=false;
        bpriority=false;
        btransient=false;
        bmerged=false;
        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++) bitmask[i]=0;
        ndirection=0xff;
        bbackinv=false;
    };

    ST_request(struct ST_request* req)
    {
        *this = *req;
        data = (char*)malloc(s_nRequestAlignedSize);
        memcpy(data, req->data, s_nRequestAlignedSize);
        memcpy(initiatortable, req->initiatortable, INITIATOR_TABLE_SIZE*sizeof(SimObj*));
        curinitiator = req->curinitiator;
        bqueued=false;
        msbcopy=NULL;
        bpriority=false;
        btransient=false;
    };
    
    ~ST_request()
    {
        free(data);
    };

    static unsigned int s_nRequestAlignedSize;
    __address_t getlineaddress(unsigned nlinebit) { return (addresspre << nlinebit); };
    __address_t getlineaddress() { return (addresspre * g_nCacheLineSize); };
    __address_t getreqaddress(unsigned nlinebit) { return (addresspre << nlinebit)+offset; };
    __address_t getreqaddress() { return addresspre * g_nCacheLineSize + offset; };

    SimObj* initiatortable[INITIATOR_TABLE_SIZE];
    unsigned int curinitiator;
    unsigned int pid;
    MemoryState::REQUEST type;

    // request from and to the processors uses addresspre, nsize, offset to determine the address range 
    __address_t addresspre;
    unsigned int nsize;     // number of 32-bit words
    unsigned int offset;    // number of 32-bit words

    // request on the network uses addresspre, nsize, offset to determine the address range 
    char bitmask[CACHE_BIT_MASK_WIDTH/8];       // bit mask to identify the valid data segments of a request
    
    char *data;         // line size alert
    std::vector<ST_request*>* msbcopy;
    unsigned long tid;          // Thread ID in case of writes
    bool    bqueued;    // JXXX this property is only used for cache fetching and queueing purpose
                        // it should not be a part of the request
                        // this property should be initialized as false

    bool    bprocessed; // this flag represent the request is preprocessed 
                        // *** in COUTING mechanism, the flag is representing repeated access requests

    bool    bbackinv;   // this flag also used for WRITE_REPLY backward broadcast decision

    unsigned char refcount; // reference counter for broadcasting requests IB/WR_BR

    bool bpriority;     // represent priority token for the ring based protocol
                        // similar to micro06-ring

    bool btransient;    // represents that token request has has only transient tokens
    bool bmerged;       // merged request only used for msb

    unsigned int tokenacquired;     // normally means token acquired
                                    // for EV, WB request, it means token held by request
    unsigned int tokenrequested;    // normally means token requested
                                    // for EV, WB requests, 0 means EV, TotalTokenNumber means WB

    unsigned char ndirection;   // Direction to send a request, after suspension.

    bool dataavailable; // for network request, to represent whether it's a reply or not

    // transient tokens cannot be grabbed by anybody, but can be transformed into permanent token by priority token
    // permanent tokens can be stored and grabed by the lines.
    unsigned int gettokenpermanent() const
    {
        return btransient ? 0 : tokenacquired;
    }

    // check whether the request state signify the request is complete
    bool IsRequestWithCompleteData();

    // check whether the request state indicates no data 
    bool IsRequestWithNoData();

    // check whether the request state indicates newly modified data (RE, ER, IV)
    bool IsRequestWithModifiedData();

    // convert the processor format to bit vector format
    void Conform2BitVecFormat();

    // convert the bit-vector format to processor format
    void Conform2SizeFormat();
};

// pipeline register array
class pipeline_t
{
    std::vector<ST_request*> m_lstRegisters;
    size_t m_current;

public:
    void copy(vector<ST_request*>& other) const
    {
        for (size_t i = m_current; i < m_lstRegisters.size(); ++i)
            other.push_back(m_lstRegisters[i]);
            
        for (size_t i = 0; i < m_current; ++i)
            other.push_back(m_lstRegisters[i]);
    }

    void reset(const vector<ST_request*>& other)
    {
        assert(other.size() == m_lstRegisters.size());
        m_lstRegisters = other;
        m_current = 0;
    }

    ST_request* shift(ST_request* req)
    {
        std::swap(req, m_lstRegisters[m_current]);
        m_current = (m_current + 1) % m_lstRegisters.size();
        return req;
    }

    ST_request* top() const
    {
        return m_lstRegisters[m_current]; 
    }

    pipeline_t(size_t n)
        : m_lstRegisters(n, NULL), m_current(0)
    {
    }
};

static SimObj* get_initiator(ST_request* req)
{
    assert(req->curinitiator > 0);
    return req->initiatortable[req->curinitiator - 1];
}

static void pop_initiator(ST_request* req)
{
    assert(req->curinitiator > 0);
    --req->curinitiator;
}

}

#endif

