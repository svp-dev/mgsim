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

char buf[123] = { 0 };

__attribute__((always_inline))
void die(const char *where)
{
    output_string(where, 2);
    output_string(": ", 2);
    output_string(strerror(errno), 2);
    output_char('\n', 2);
    svp_abort();
}

const char *testconf = "\0PLACES: 1"; // for "make check": this program is single-threaded.

void fill(void)
{
    int i, j;
    for (i = 6; i < sizeof(buf) - 5; ++i)
        buf[i] = (char)('A' + (i & 0xf));
    for (i = 0, j = 0; i < sizeof(buf) - 6; i += 8, j++)
        strcpy(buf + i + j, "hello");
}

int test(void)
{
    int i, j;
    fill();

    for (i = 0; i < sizeof(buf); ++i)
    {
        if ((i & 0xf) == 0)
        {
            output_char('\n', 2);
            output_uint(i, 2);
            output_char(' ', 2);
        }
        output_char(buf[i], 2);
    }
    output_char('\n', 2);

    for (i = 0, j = 0; i < sizeof(buf) - 6; i += 8, j++)
        if (strncmp(buf + i + j, "hello", 5) != 0)
            die("data mismatch");
    return 0;
}
