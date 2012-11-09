#ifndef MGSIM_H
#define MGSIM_H

/*
 * This header collects "hard" constants that are decided by
 * convention and must be known from both hardware and software.
 */


/**** Ancillary processor registers ****/

#if defined(__mtalpha__)
#define mgsim_read_asr(DestVar, Register) __asm__ __volatile__("getasr %1, %0" : "=r"(DestVar) : "I"(Register))
#define mgsim_read_apr(DestVar, Register) __asm__ __volatile__("getapr %1, %0" : "=r"(DestVar) : "I"(Register))
#elif defined(__mtsparc__)
/* on MT-SPARC, the Microgrid "ASRs" are different from the SPARC ISA ASRs'.
 * The SPARC ASRs are for example ASR0 for %y, ASR15 for STBAR, and ASR19/ASR20 for Microthreaded opcodes. 
 * The Microgrid "ASRs" 0-N are mapped to SPARC ASRs 7-(7+N).
 * The Microgrid "APRs" 0-N are mapped to SPARC APRs 21-(21+N).
 */
#define mgsim_read_asr(DestVar, Register) __asm__ __volatile__("rd %%asr%1, %0" : "=r"(DestVar) : "I"((Register)+7))
#define mgsim_read_apr(DestVar, Register) __asm__ __volatile__("rd %%asr%1, %0" : "=r"(DestVar) : "I"((Register)+21))
#elif defined(__mips__)
/* on MIPS, ASR0 is CP2 register $6; APR0 is CP2 register $16. */
#define mgsim_read_asr(DestVar, Register) __asm__ __volatile__("mfc2 %0, $%1" : "=r"(DestVar) : "I"((Register)+6))
#define mgsim_read_apr(DestVar, Register) __asm__ __volatile__("mfc2 %0, $%1" : "=r"(DestVar) : "I"((Register)+16)) 
#endif

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
