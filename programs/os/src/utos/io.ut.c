#include <utos/io.ut.h>

thread void _io_do_read(shared int, shared void*);
thread void _io_do_write(shared int, shared void*);

/**** IMPLEMENTATION OF LIBRARY THREADS ******/

thread void utsys_io_init() {

  UTSYS_SB_SEND(_SB_REGISTER_SERVICE | (_SB_IO_READ << 8), &_io_do_write);

  UTSYS_SB_SEND(_SB_REGISTER_SERVICE | (_SB_IO_WRITE << 8); &_io_do_read);

}

thread void utsys_io_write_bytes(int n, const char *bytes) {

  UTSYS_SB_SEND(_SB_IO_WRITE | (n << 8), (void*) bytes);

}

thread void utsys_io_read_bytes(shared int n, char *bytes) {
  int code = _SB_IO_READ | (n << 8);
  void *ptr = (void*) bytes;

  UTSYS_SB_EXCHANGE(code, ptr);

  n = code;

}

/*********** IMPLEMENTATION OF I/O ROUTINES ********/

thread void _io_do_read(shared int code, shared void* data)
{
  int max = code >> 8;
  for (i = 0; i < max; ++i) {
    int r;
    __asm__ volatile("rdcon %0" : "=r" (r));
    if (r & ~0xff)
      break;
    ((char*)data)[i] = (char)r;
  }

  data = data;
  code = i;
}


thread void _io_do_write(shared int code, shared void* data)
{
  int n = code >> 8;
  for (int i = 0; i < n; ++i) 
    __asm__ volatile("wrcon 0" : : "r"(((char*)data)[i]));
  
  data = data;
  code = n;
}
