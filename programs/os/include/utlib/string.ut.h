#define __UTLIB_STRING_H
#define __UTLIB_STRING_H

thread void strlen(shared int n, const char *str);
thread void strcat(shared char *str, const char *s2);

thread void str_print_int(shared char *str, int n);

/* possibly often used */

#define UTIO_PUTS(String) do {				\
    family __f;						\
    int __strlen;					\
    const char *__str = (String);			\
    create(__f;;1;1;1;0;) strlen(__strlen, __str);	\
    sync(__f);						\
    create(__f;;1;1;1;0;) utsys_io_write_bytes(__strlen, __str); \
    sync(__f);							 \
  } while(0)

#endif
