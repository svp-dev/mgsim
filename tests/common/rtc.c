#include <svp/testoutput.h>
#include <svp/mgsim.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "mtconf.h"

const char *testconf = "\0PLACES: 1"; // for "make check": this program is single-threaded.

int test(void)
{
    sys_detect_devs();
    sys_conf_init();

    volatile uint32_t *rtcctl = (void*)mg_devinfo.base_addrs[mg_rtc_devid];
    volatile long *int0 = (void*)mg_devinfo.channels;

    output_string("resolution (real microseconds): ", 1);
    output_uint(rtcctl[0], 1);

    output_string("\nenabling notifications: ", 1);
    *int0 = 1; // allow receiving interrupts
    rtcctl[3] = 1; // get all events
    rtcctl[1] = 200000; // notify every 200ms
    output_string("done\n", 1);

    clock_t before = clock();
    int i;
    for (i = 0; i < 5; ++i)
    {
        output_string("waiting...\n", 1);
        long a = *int0;
        output_int(a, 1);
        output_char(' ', 1);
        output_uint(rtcctl[5], 1);
        output_char(' ', 1);
        output_uint(rtcctl[4], 1);
        output_char('\n', 1);
    };
    clock_t after = clock();
    output_string("done: ", 1);
    output_uint(after-before, 1);
    output_string(" cpu cycles.\n", 1);

    // disable clock so there
    // is no process left running to terminate.
    rtcctl[1] = 0;

    return 0;
}
