#ifndef MEMORY_PREDEFINE_H
#define MEMORY_PREDEFINE_H

#include <systemc.h>
#include <vector>
#include <stdint.h>
#include <algorithm>
#include <queue>

namespace MemSim
{

typedef unsigned int CacheID;
typedef uint64_t     MemAddr;

class CacheState
{
public:
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

static const size_t MAX_MEMORY_OPERATION_SIZE = 64;

// the line size of the cacheline
extern unsigned int g_nCacheLineSize;

struct Message;

template<class InputIterator, class EqualityComparable>
static inline bool contains(InputIterator first, InputIterator last, const EqualityComparable& value)
{
    return std::find(first, last, value) != last;
}

struct cache_line_t
{
    bool    valid;
    MemAddr tag;
    sc_time time;

    char data   [MAX_MEMORY_OPERATION_SIZE];
    bool bitmask[MAX_MEMORY_OPERATION_SIZE]; // bit mask is defined to hold written data before reply comes back
                                             // it does not always represent the validness of word segment within the line
                                             // for WP states, bitmask only indicates the newly written data. 

    unsigned int tokencount;
    
    bool dirty;         // Whether the cache-line contains dirty data?
    
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

    unsigned int gettokenglobalvisible() const
    {
        return (invalidated || tlock) ? 0 : tokencount;
    }

    // check whether the line state signify the line is complete
    bool IsLineAtCompleteState() const
    {
        assert(valid);
        if (!dirty && pending)
        {
            return !contains(bitmask, bitmask + MAX_MEMORY_OPERATION_SIZE, false);
        }
        return (!dirty || tokencount != 0);
    }
};

int lg2(int n);

// data always starts from the line-aligned address
// so the data may actually start from the middle
struct Message
{
    enum Type
    {
        // Basic local messages
        NONE,                       // NO: None Exist
        READ,                       // LR: Local Read
        WRITE,                      // LW: Local Write
        READ_REPLY,                 // RR: Read Reply
        WRITE_REPLY,                // WR: Write Reply

        // Network messages
        ACQUIRE_TOKEN,              // AT: acquire token
        ACQUIRE_TOKEN_DATA,         // AD: acquire token & data
        DISSEMINATE_TOKEN_DATA,     // DD: disseminate token

        LOCALDIR_NOTIFICATION,      // the notification is used for caches to notify local directory the changes in the token status in the cache
    };
    
    Message()
    {
        bqueued=false;
        bprocessed=false;
        tokenrequested=0;
        tokenacquired=0;
        dataavailable=false;
        bpriority=false;
        btransient=false;
        bmerged=false;
        std::fill(bitmask, bitmask + MAX_MEMORY_OPERATION_SIZE, false);
    };

    Message(const Message* msg)
    {
        *this = *msg;
        bqueued = false;
        msbcopy.clear();
        bpriority = false;
        btransient = false;
    };
    
    static unsigned int s_nRequestAlignedSize;

    Type            type;
    MemAddr         address;
    unsigned int    size;
    char            data   [MAX_MEMORY_OPERATION_SIZE];
    bool            bitmask[MAX_MEMORY_OPERATION_SIZE];
    CacheID         source;
    unsigned int    pid;
    unsigned long   tid;          // Thread ID in case of writes
    
    std::vector<Message*> msbcopy;
    bool    bqueued;    // JXXX this property is only used for cache fetching and queueing purpose
                        // it should not be a part of the request
                        // this property should be initialized as false

    bool    bprocessed; // this flag represent the request is preprocessed 
                        // *** in COUTING mechanism, the flag is representing repeated access requests

    unsigned char refcount; // reference counter for broadcasting requests IB/WR_BR

    bool bpriority;     // represent priority token for the ring based protocol
                        // similar to micro06-ring

    bool btransient;    // represents that token request has has only transient tokens
    bool bmerged;       // merged request only used for msb

    unsigned int tokenacquired;     // normally means token acquired
                                    // for EV, WB request, it means token held by request
    unsigned int tokenrequested;    // normally means token requested
                                    // for EV, WB requests, 0 means EV, TotalTokenNumber means WB

    bool dataavailable; // for network request, to represent whether it's a reply or not

    // transient tokens cannot be grabbed by anybody, but can be transformed into permanent token by priority token
    // permanent tokens can be stored and grabed by the lines.
    unsigned int gettokenpermanent() const
    {
        return btransient ? 0 : tokenacquired;
    }

    // check whether the request state signify the request is complete
    bool IsRequestWithCompleteData() const
    {
        switch (type)
        {
            case ACQUIRE_TOKEN_DATA:
            case DISSEMINATE_TOKEN_DATA:
                return (tokenacquired > 0);
            
            default:
                return false;
        }
    }

    // check whether the request state indicates no data 
    bool IsRequestWithNoData() const
    {
        switch (type)
        {
            case WRITE:
            case ACQUIRE_TOKEN:
            case READ_REPLY:
                return false;
                
            case DISSEMINATE_TOKEN_DATA:
                return (tokenacquired == 0);
                    
            case ACQUIRE_TOKEN_DATA:
                return (tokenacquired == 0 && tokenrequested != CacheState::GetTotalTokenNum());
            
            default:
                return true;
        }
    }

    // check whether the request state indicates newly modified data (RE, ER, IV)
    bool IsRequestWithModifiedData() const
    {
        switch (type)
        {
            case ACQUIRE_TOKEN:
                return true;
                
            case ACQUIRE_TOKEN_DATA:
                return (tokenrequested == CacheState::GetTotalTokenNum());
                
            case WRITE_REPLY:
                return bmerged;
                
            default:
                return false;
        }
    }
};

// pipeline register array
class pipeline_t
{
    std::vector<Message*> m_lstRegisters;
    size_t m_current;

public:
    void copy(std::vector<Message*>& other) const
    {
        for (size_t i = m_current; i < m_lstRegisters.size(); ++i)
            other.push_back(m_lstRegisters[i]);
            
        for (size_t i = 0; i < m_current; ++i)
            other.push_back(m_lstRegisters[i]);
    }

    void reset(const std::vector<Message*>& other)
    {
        assert(other.size() == m_lstRegisters.size());
        m_lstRegisters = other;
        m_current = 0;
    }

    Message* shift(Message* msg)
    {
        std::swap(msg, m_lstRegisters[m_current]);
        m_current = (m_current + 1) % m_lstRegisters.size();
        return msg;
    }

    Message* top() const
    {
        return m_lstRegisters[m_current]; 
    }

    pipeline_t(size_t n)
        : m_lstRegisters(n, NULL), m_current(0)
    {
    }
};

}

#endif

