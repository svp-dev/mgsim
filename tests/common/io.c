#include <svp/testoutput.h>
#include <svp/mgsim.h>
#include <svp/abort.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "mtconf.h"

char buf[500] = { 0 };

__attribute__((always_inline))
void die(const char *where)
{
    output_string(where, 2);
    output_string(": ", 2);
    output_string(strerror(errno), 2);
    output_char('\n', 2);
    svp_abort();
}

int test(void)
{
    sys_detect_devs();
    sys_conf_init();

    
    int fd;
    int i;
    
    if ((fd = open("testinput", O_WRONLY|O_TRUNC|O_CREAT, 0666)) == -1)
        die("open1");
    
    for (i = 0; i < 100; ++i)
      if (write(fd, "hello", 5) != 5)
       die("write");
        
   if (close(fd) != 0)
        die("close1");

    if ((fd = open("testinput", O_RDONLY)) == -1)
       die("open2");
    
    char *p = buf;
    for (i = 0; i < 100; ++i)
   {
      if (read(fd, p, 5) != 5)
        die("read");
      if (strncmp(p, "hello", 5) != 0)
        die("data mismatch");
      p += 5;
   }
        
   if (close(fd) != 0)
      die("close2");

    return 0;
}
