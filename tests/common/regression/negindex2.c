#include <svp/testoutput.h>
#include <svp/delegate.h>

sl_def(foo, , sl_shparm(int, x)) 
{
    sl_setp(x, sl_getp(x) + 1);
} 
sl_enddef

const char *testconf = "\0PLACES: 1"; // for "make check": this program is single-threaded.

int test(void)
{
    int r;
    sl_create(,, -10, 100, 10, 0, , foo, sl_sharg(unsigned, count, 0));
    sl_sync(r);

    if (sl_geta(count) != 11)
        return 1;

    return 0;
}

