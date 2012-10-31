#include <svp/testoutput.h>
#include <svp/mgsim.h>
#include <stdint.h>
#include <stddef.h>

#include "mtconf.h"

const char *testconf = "\0PLACES: 1"; // for "make check": this program is single-threaded.

int test(void)
{
    sys_detect_devs();
    sys_conf_init();

    uint32_t *cfg = mg_devinfo.base_addrs[mg_cfgrom_devid];

    output_string("config magic: ", 1);
    output_hex(cfg[0], 1);
    output_char('\n', 1);

    if (cfg[0] != 0x53474d23)
        return 1; // failure

    return 0;
}
