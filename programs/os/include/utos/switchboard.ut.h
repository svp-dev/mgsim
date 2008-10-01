#ifndef __UT_SWITCHBOARD_H
#define __UT_SWITCHBOARD_H

/* the following can be called by any thread
   in a family of 1 thread to perform communication
   with the switchboard.
*/
thread void utsys_sb_exchange(shared int service, shared void* p);

/* helper for commonly used construct */
#define UTSYS_SB_SEND(Code, Pointer) do {			\
    family __f;							\
    register int __code = (Code);				\
    register void *__ptr = (void*)(Pointer);			\
    create(__f;;1;1;1;0;) utsys_sb_exchange(__code, __ptr);	\
    sync(__f);							\
  } while(0)

#define UTSYS_SB_EXCHANGE(CodeVar, PointerVar) do {			\
    family __f;								\
    create(__f;;1;1;1;0;) utsys_sb_exchange(CodeVar, PointerVar);	\
    sync(__f);								\
  } while(0)

/* the following thread should be started by
   the boot code in a family of 1 thread */
thread void utsys_sb_manager();




/* predefined switchboard codes */
#define _SB_NONE 0
#define _SB_REGISTER_SERVICE 1
#define _SB_MEM_ALLOC 2
#define _SB_MEM_FREE 3
#define _SB_IO_WRSTRING 4
#define _SB_IO_RDSTRING 5


#endif
