#ifndef MGSIM_H
#define MGSIM_H

/*
 * This header collects "hard" constants that are decided by
 * convention and must be known from both hardware and software.
 */


/**** Ancillary processor registers ****/

#ifndef __MGSIM_ASR_IOBASE
#define __MGSIM_ASR_IOBASE 0x400
#endif
#ifndef __MGSIM_APR_IOBASE
#define __MGSIM_APR_IOBASE 0x500
#endif

#define mgsim_read_asr(DestVar, Register) do {                          \
        volatile unsigned long* __reg = ((unsigned long*)(void*)(__MGSIM_ASR_IOBASE)); \
        (DestVar) = (__typeof__(DestVar))__reg[Register];               \
    } while(0)

#define mgsim_read_apr(DestVar, Register) do {                          \
        volatile unsigned long* __reg = ((unsigned long*)(void*)(__MGSIM_APR_IOBASE)); \
        (DestVar) = (__typeof__(DestVar))__reg[Register];               \
    } while(0)

/* Ancillary system registers */

#define ASR_SYSTEM_VERSION    0
#define ASR_CONFIG_CAPS       1
#define ASR_DELEGATE_CAPS     2
#define ASR_SYSCALL_BASE      3
#define ASR_NUM_PERFCOUNTERS  4
#define ASR_PERFCOUNTERS_BASE 5
#define ASR_IO_PARAMS1        6
#define ASR_IO_PARAMS2        7
#define ASR_AIO_BASE          8
#define ASR_PNC_BASE          9
#define ASR_PID               10
#define NUM_ASRS              11

/* value for ASR_SYSTEM_VERSION: to be updated whenever the list of ASRs
   changes */
#define ASR_SYSTEM_VERSION_VALUE  1

/*
// Suggested:
// #define APR_SEP                0
// #define APR_TLSTACK_FIRST_TOP  1
// #define APR_TLHEAP_BASE        2
// #define APR_TLHEAP_SIZE        3
// #define APR_GLHEAP             4
*/



/**** Configuration data ****/

/* the following constants refer to the configuration blocks when an
   ActiveROM is configured with source "CONFIG". */

/* the first word of the configuration area */
#define CONF_MAGIC    0x53474d23

/* the first word of each configuration block, that determines its
   structure. */
#define CONF_TAG_TYPETABLE 1
#define CONF_TAG_ATTRTABLE 2
#define CONF_TAG_RAWCONFIG 3
#define CONF_TAG_OBJECT    4
#define CONF_TAG_SYMBOL    5

/* the first word of a "value" entry for object and relation
   properties. */
#define CONF_ENTITY_VOID   1
#define CONF_ENTITY_SYMBOL 2
#define CONF_ENTITY_OBJECT 3
#define CONF_ENTITY_UINT   4


#endif
