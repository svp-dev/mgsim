#ifndef _STAT_DEF_H
#define _STAT_DEF_H

#include "../simlink/smdatatype.h"

#include <vector>
using namespace std;

typedef struct __stat_stru_request_t{
    bool                    valid;
    unsigned __int64        address;    // line address
    unsigned int            type;
    unsigned int            offset;
    unsigned int            size;
    void*                   ptr;
} stat_stru_request_t;

typedef struct __stat_stru_line_t{
    unsigned int            index;      // index within the set
    unsigned __int64        address;    // line address
    unsigned int            state;
    unsigned int            counter;    // only for directory
} stat_stru_line_t;

typedef struct __stat_stru_set_t{
    unsigned int            index;      // index of the set
    stat_stru_line_t       *lines;
} stat_stru_set_t;

typedef struct __stat_stru_req_latency_t{
    void*                       ptr;      // index of the set
    double                      start;
    double                      end;
    double                      latency;
    unsigned __int64            address;
    unsigned int                type;
//    char                        data[64];   // data segment, only debuging
} stat_stru_req_latency_t;

typedef struct __stat_stru_req_sent_t{
    unsigned int            read;
    unsigned int            write;
} stat_stru_req_sent_t;


// when vector is used for pipeline, vec[0] represent top, which is the earliest request in the pipeline
typedef vector<stat_stru_request_t> stat_stru_request_list_t;


typedef unsigned int stat_stru_size_t;

typedef unsigned __int64 stat_stru_address_t;

enum STAT_MEMORY_COMP_DEFINITION{
    STAT_MEMORY_COMP_REQUEST_NO,        // total request number so far
    STAT_MEMORY_COMP_PROCESSING,        // current processing request
    STAT_MEMORY_COMP_STORE,        // current processing request
    STAT_MEMORY_COMP_PIPELINE,          // pipeline
    STAT_MEMORY_COMP_INCOMING_FIFO,     // incoming fifo
    STAT_MEMORY_COMP_ALL                // all 
};

enum STAT_MEMORY_TYPE_DEFINITION{
    STAT_MEMORY_TYPE_REQUEST,
    STAT_MEMORY_TYPE_SIZE,
    STAT_MEMORY_TYPE_ADDRESS,
    STAT_MEMORY_TYPE_REQUEST_LIST,
    STAT_MEMORY_ALL
};

enum STAT_CACHE_COMP_DEFINITION{
    STAT_CACHE_COMP_REQUEST_NO,        // total request number so far
    STAT_CACHE_COMP_PROCESSING_INI,    // current processing INI request
    STAT_CACHE_COMP_PROCESSING_NET,    // current processing NET request
    STAT_CACHE_COMP_PIPELINE_INI,      // pipeline INI
    STAT_CACHE_COMP_PIPELINE_NET,      // pipeline NET
    STAT_CACHE_COMP_CLEANSING_INI,     // pipeline cleansing 
    STAT_CACHE_COMP_SEND_NODE_INI,     // request sent from ini
    STAT_CACHE_COMP_SEND_NODE_NET,     // request sent from pas
    STAT_CACHE_COMP_INCOMING_FIFO_INI, // incoming fifo INI
    STAT_CACHE_COMP_INCOMING_FIFO_NET, // incoming fifo NET
    STAT_CACHE_COMP_TEST,               // only testing purpose
    STAT_CACHE_COMP_ALL                // all 
};

enum STAT_RTDIR_COMP_DEFINITION{
    STAT_RTDIR_COMP_REQUEST_NO,        // total request number so far
    STAT_RTDIR_COMP_PROCESSING_NET,    // current processing INI request
    STAT_RTDIR_COMP_PROCESSING_BUS,    // current processing NET request
    STAT_RTDIR_COMP_PIPELINE_NET,      // pipeline INI
    STAT_RTDIR_COMP_PIPELINE_BUS,      // pipeline NET
    STAT_RTDIR_COMP_CLEANSING_NET,     // pipeline cleansing 
    STAT_RTDIR_COMP_SEND_NODE_NET,     // request sent from ini
    STAT_RTDIR_COMP_SEND_NODE_BUS,     // request sent from pas
    STAT_RTDIR_COMP_INCOMING_FIFO_NET, // incoming fifo INI
    STAT_RTDIR_COMP_INCOMING_FIFO_BUS, // incoming fifo NET
    STAT_RTDIR_COMP_SET_TRACE,         // total request number so far
    STAT_RTDIR_COMP_TEST,               // only testing purpose
    STAT_RTDIR_COMP_ALL                // all 
};

enum STAT_PROC_COMP_DEFINITION{
    STAT_PROC_COMP_LATENCY,             // request reply latency, endtime-starttime
    STAT_PROC_COMP_REQ_SENT,            // number of requests sent to memory system
    STAT_PROC_COMP_ALL
};

#endif
