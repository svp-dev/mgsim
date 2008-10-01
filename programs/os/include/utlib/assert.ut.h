#ifndef __UTLIB_ASSERT_H
#define __UTLIB_ASSERT_H

thread void __assert(const char* msg, const char *fname, int fline);

#define _ASSERT(Msg, File, Line) do {				\
    family __f;							\
    const char *__m = (Msg);					\
    const char *__file = (File);				\
    int __line = (Line);					\
    create(__f;;1;1;1;0;) __assert(__m, __file, __line);	\
    sync(__f);							\
  } while(0)

#define ASSERT(EX) do { \
    if (!(EX)) _ASSERT(#EX, __FILE__, __LINE__); \
  } while(0);

#endif
