#ifndef __UT_IO_H
#define __UT_IO_H

/* this should be called once at startup */
thread void utsys_io_init();

/* library threads */
thread void utsys_io_write_bytes(int n, const char *bytes);
thread void utsys_io_read_bytes(shared int n, char *bytes);

/* possibly often used construct */
#define UTSYS_IO_WRITE(N, Bytes) do {			      \
    family __f;						      \
    int __n = (N);					      \
    const char *__bytes = (Bytes);			      \
    create(__f;;1;1;1;0;) utsys_io_write_bytes(__n, __bytes); \
    sync(__f);						      \
  } while(0)

#endif
