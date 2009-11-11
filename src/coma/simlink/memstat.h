#ifndef MEMSTAT_H
#define MEMSTAT_H
namespace MemSim{

// within memory
extern unsigned int g_uMemoryAccessesL;
extern unsigned int g_uMemoryAccessesS;

extern unsigned int g_uHitCountL;
extern unsigned int g_uTotalL;
extern unsigned int g_uHitCountS;
extern unsigned int g_uTotalS;

extern double g_fLatency;


// from CMLink
extern uint64_t g_uAccessDelayS;
extern uint64_t g_uAccessDelayL;

extern unsigned int g_uAccessL;
extern unsigned int g_uAccessS;


// retry or conflict couting
//extern unsigned int g_uRetryS;
//extern unsigned int g_uRetryL;
//
//extern uint64_t g_uRetryDelayS;
//extern uint64_t g_uRetryDelayL;
//
//
//extern unsigned int g_uRingorderS;
//extern unsigned int g_uRingorderL;
//
//extern uint64_t g_uRingorderDelayS;
//extern uint64_t g_uRingorderDelayL;
//
//
//extern unsigned int g_uCompeteS;
//extern unsigned int g_uCompeteL;
//
//extern uint64_t g_uCompeteDelayS;
//extern uint64_t g_uCompeteDelayL;
//



extern unsigned int g_uConflictS;
extern unsigned int g_uConflictL;

extern uint64_t g_uConflictDelayS;
extern uint64_t g_uConflictDelayL;

extern unsigned int g_uConflictAddL;
extern unsigned int g_uConflictAddS;

extern unsigned int g_uProbingLocalLoad;
}

extern void MemStatMarkConflict(unsigned long* ptr);
#endif
