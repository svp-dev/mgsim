#ifndef __UT_MEM_H
#define __UT_MEM_H

/* call this once at program startup */
thread void utsys_mem_init();

thread void utsys_mem_alloc(shared int size, shared void *p);
thread void utsys_mem_free(shared void *p);


#define UTSYS_ALLOC(N, Pointer) do {			\
    family __f;						\
    int __sz = (N);					\
    void *__p = (Pointer);				\
    create(__f;;1;1;1;0;) utsys_mem_alloc(__sz, __p);	\
    sync(__f);						\
    (Pointer) = __p;					\
  } while(0)

#define UTSYS_FREE(Pointer) do {		\
    family __f;					\
    void *__p = (Pointer);			\
    create(__f;;1;1;1;0;) utsys_mem_free(__p);	\
    sync(__f);					\
  } while(0)


#define

