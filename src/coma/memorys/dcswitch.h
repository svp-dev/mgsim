#ifndef __DEBUG_SWITCH_H
#define __DEBUG_SWITCH_H

// debug level details
#define MEM_DEBUG_LEVEL_NONE                0
#define MEM_DEBUG_LEVEL_LOG                 1
#define MEM_DEBUG_LEVEL_TRACE_AND_LOG       2
#define MEM_DEBUG_LEVEL_ALL                 3


/////////////////////////////
// manual log enabling switch
//#define MEM_ENABLE_SIM_LOG


////////////////////////
// debug enabling switch
#ifdef MDBLOG
#define MEM_ENABLE_DEBUG    MEM_DEBUG_LEVEL_ALL
#else
//#define MEM_ENABLE_DEBUG    MEM_DEBUG_LEVEL_LOG
//#define MEM_ENABLE_DEBUG    MEM_DEBUG_LEVEL_ALL
#define MEM_ENABLE_DEBUG    MEM_DEBUG_LEVEL_NONE

#endif

// enable specific switches for debug
#if defined(MEM_ENABLE_DEBUG) && (MEM_ENABLE_DEBUG == MEM_DEBUG_LEVEL_LOG)
    #define MEM_ENABLE_SIM_LOG
#elif defined(MEM_ENABLE_DEBUG) && (MEM_ENABLE_DEBUG == MEM_DEBUG_LEVEL_TRACE_AND_LOG)
    #define MEM_ENABLE_SIM_LOG
    #define MEM_DEBUG_TRACE
#elif defined(MEM_ENABLE_DEBUG) && (MEM_ENABLE_DEBUG == MEM_DEBUG_LEVEL_ALL)
    #define MEM_ENABLE_SIM_LOG
    #define MEM_DEBUG_TRACE
    #define MEM_STORE_SEQUENCE_DEBUG
//    #define MEM_MODULE_STATISTICS
//    #define MEM_REQUEST_PROGRESS
//    #define MEM_DATA_VERIFY
#endif


//////////////////////
// data prefill switch
//#define MEM_DATA_PREFILL


// JTEMP ***
#define TEMP_BASIC_DEBUG_CM 1   
#define TEMP_BASIC_DEBUG_MM 2

//#define TEMP_BASIC_DEBUG    0   // NONE
//#define TEMP_BASIC_DEBUG      1   //CMLINK
//#define TEMP_BASIC_DEBUG    2   // MEMORY
//#define TEMP_BASIC_DEBUG    3   // CMLINK + MEMORY


// Level one cache snooping
//#define MEM_CACHE_LEVEL_ONE_SNOOP

// Backward update instead of invalidation
//#define MEM_BACKWARD_INVALIDATION_UPDATE


/////////////////////////////////////
// Directory implemention definitions
#define MEMSIM_DIRECTORY_IMPLEMENTATION_NAIVE 0         // support naive implementation 
#define MEMSIM_DIRECTORY_IMPLEMENTATION_SRQ 1           // support SRQ solutions
// detail descriptions in directorytok.h


// define the memory implementation type
#define MEMSIM_DIRECTORY_IMPLEMENTATION MEMSIM_DIRECTORY_IMPLEMENTATION_NAIVE

#ifndef MEMSIM_DIRECTORY_IMPLEMENTATION
#define MEMSIM_DIRECTORY_IMPLEMENTATION MEMSIM_DIRECTORY_IMPLEMENTATION_NAIVE
#endif


// Memory directory implementation for remote directory counting
#if defined(MEMSIM_DIRECTORY_IMPLEMENTATION) && (MEMSIM_DIRECTORY_IMPLEMENTATION == MEMSIM_DIRECTORY_IMPLEMENTATION_NAIVE)
#define MEMSIM_DIRECTORY_REQUEST_COUNTING
#endif


#endif
