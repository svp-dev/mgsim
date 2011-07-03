//
// mtconf.c: this file is part of the Microgrid simulator.
//
// Copyright (C) 2010,2011 the Microgrid project.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// The complete GNU General Public Licence Notice can be found as the
// `COPYING' file in the root directory.
//

#include "mtconf.h"
#include "mgsim.h"
#include <svp/testoutput.h>
#include <svp/mtmalloc.h>
#include <svp/abort.h>
#include <svp/delegate.h>

int verbose_boot = 1;
clock_t boot_ts = 0;
time_t boot_time = 0;

confword_t mgconf_ftes_per_core = (confword_t)-1;
confword_t mgconf_ttes_per_core = (confword_t)-1;
confword_t mgconf_core_freq = (confword_t)-1;
confword_t mgconf_master_freq = (confword_t)-1;

sl_place_t mg_io_place_id;
struct mg_device_info mg_devinfo;
uint16_t  mg_io_dca_devid;

size_t mg_uart_devid = (size_t)-1;
size_t mg_lcd_devid = (size_t)-1;
size_t mg_rtc_devid = (size_t)-1;
size_t mg_rpc_devid = (size_t)-1;
size_t mg_rpc_chanid = (size_t)-1;
size_t mg_cfgrom_devid = (size_t)-1;
size_t mg_argvrom_devid = (size_t)-1;
size_t mg_gfxctl_devid = (size_t)-1;
size_t mg_gfxfb_devid = (size_t)-1;
volatile uint32_t *mg_gfx_ctl;
void *mg_gfx_fb;


// local version of strcmp to avoid 
// a dependency on the stdlib.
static int
__strcmp(const unsigned char *s1, const unsigned char *s2)
{
	while (*s1 == *s2++)
		if (*s1++ == '\0')
			return (0);
	return (*s1 - *(s2 - 1));
}



static
void detect_lcd(size_t devid, void *addr) 
{
    uint32_t lcdcfg = *(uint32_t*)addr;
    if (verbose_boot)
    {
        output_string("* lcd at 0x", 2);
        output_hex(addr, 2);
        output_string(", size ", 2);
        output_uint((lcdcfg >> 16) & 0xffff, 2);
        output_char('x', 2);
        output_uint(lcdcfg & 0xffff, 2);
        output_char('.', 2);
        output_ts(2);
        output_char('\n', 2);
    }
    if (mg_lcd_devid == (size_t)-1)
    {
        mg_lcd_devid = devid; 
    }
}

static
void detect_uart(size_t devid, void *addr)
{
    if (verbose_boot)
    {
        output_string("* uart at 0x", 2);
        output_hex(addr, 2);
        output_char('.', 2);
        output_ts(2);
        output_char('\n', 2);
    }
    if (mg_uart_devid == (size_t)-1)
    {
        mg_uart_devid = devid; 
    }    
}

static
void detect_gfx(size_t devid, void *addr)
{
    /* obtain the address of the framebuffer */
    uint32_t fb_devid = *((uint32_t*)addr + 9);
    void *fb_addr = mg_devinfo.base_addrs[fb_devid];

    if (verbose_boot)
    {
        output_string("* gfxctl at 0x", 2);
        output_hex(addr, 2);
        output_string(", framebuffer at 0x", 2);
        output_hex(fb_addr, 2);
        output_char('.', 2);
        output_ts(2);
        output_char('\n', 2);
    }

    if (mg_gfxctl_devid == (size_t)-1)
    {
        mg_gfxfb_devid = fb_devid;
        mg_gfx_fb = fb_addr;
        mg_gfxctl_devid = devid; 
        mg_gfx_ctl = addr;
    }    
}

static
void detect_rtc(size_t devid, void *addr)
{
    boot_time = ((uint32_t*)addr)[5];
    if (verbose_boot)
    {
        output_string("* rtc at 0x", 2);
        output_hex(addr, 2);
        output_char('.', 2);
        output_ts(2);
        output_char('\n', 2);
    }
    if (mg_rtc_devid == (size_t)-1)
    {
        mg_rtc_devid = devid; 
    }    
}

static
void detect_rpc(size_t devid, void *addr)
{
    volatile uint32_t* ctl = (uint32_t*)addr;

    // initialize DCA channel / notification channel 1
    ctl[1] = (1U << 16) | mg_io_dca_devid;
    if (verbose_boot)
    {
        output_string("* rpc interface at 0x", 2);
        output_hex(addr, 2);
        output_char('.', 2);
        output_ts(2);
        output_char('\n', 2);
    }
    if (mg_rpc_devid == (size_t)-1)
    {
        mg_rpc_devid = devid; 
        mg_rpc_chanid = 1;
    }    
}

static
void detect_rom(size_t devid, void *addr)
{
    uint32_t magic = *(uint32_t*)addr;
    if (magic == CONF_MAGIC)
    {
        if (verbose_boot)
        {
            output_string("* configuration ROM at 0x", 2);
            output_hex(addr, 2);
            output_char('.', 2);
            output_ts(2);
            output_char('\n', 2);
        }
        if (mg_cfgrom_devid == (size_t)-1)
        {
            mg_cfgrom_devid = devid;
        }
    }
    else if (magic == 0x56475241 /*ASCII "ARGV"*/)
    {
        if (verbose_boot)
        {
            output_string("* argument ROM at 0x", 2);
            output_hex(addr, 2);
            output_char('.', 2);
            output_ts(2);
            output_char('\n', 2);
        }
        if (mg_argvrom_devid == (size_t)-1)
        {
            mg_argvrom_devid = devid;
        }
    }
}

static struct 
{ 
    struct mg_device_id id;
    const char *name;
    void (*detect)(size_t, void*);
} known_devs[] =
{
    { { 1, 1, 1 }, "lcd", &detect_lcd },
    { { 1, 2, 1 }, "rtc", &detect_rtc },
    { { 1, 4, 1 }, "gfx", &detect_gfx },
    { { 1, 5, 1 }, "rom", &detect_rom },
    { { 1, 7, 1 }, "uart", &detect_uart },
    { { 1, 8, 1 }, "rpc", &detect_rpc },
    { { 0, 0, 0 }, 0, 0 }
};


void sys_conf_init(void)
{
    /* attempt to find the config ROM */
    if (mg_cfgrom_devid == (size_t)-1)
    {
        if (verbose_boot)
        {
            output_string("* no configuration ROM, unable to read configuration", 2);
            output_ts(2);
            output_char('\n', 2);
        }
        return;
    }

    struct block
    {
        confword_t tag;
        confword_t next;
        confword_t payload[];
    };

    confword_t system_type_id = (confword_t)-1;
    confword_t system_version_id = (confword_t)-1;
    confword_t system_freq_id = (confword_t)-1;
    confword_t core_type_id = (confword_t)-1;
    confword_t core_freq_id = (confword_t)-1;
    confword_t core_pid_id = (confword_t)-1;
    confword_t core_threads_id = (confword_t)-1;
    confword_t core_families_id = (confword_t)-1;
    
    if (verbose_boot)
    {
        output_string("* reading configuration...\n", 2);
    }

    const confword_t *cfg = mg_devinfo.base_addrs[mg_cfgrom_devid];
    for (const struct block *p = (const struct block*)(cfg + 1); p->tag != 0; p = (const struct block*)(cfg + p->next))
    {
#define get_symbol(n) ((char*)(cfg + n + 1))

        switch(p->tag)
        {
        case CONF_TAG_TYPETABLE:
        {
            size_t ntypes = p->payload[0];
            for (size_t i = 0; i < ntypes; ++i)
            {
                confword_t name = p->payload[1 + 2*i];
                const confword_t *attrtable = cfg + p->payload[1 + 2*i + 1];
                size_t nattrs = attrtable[0];
                if (system_type_id == (confword_t)-1 && __strcmp(get_symbol(name), "system") == 0)
                {
                    system_type_id = i;
                    for (size_t j = 0; j < nattrs; ++j)
                    {
                        confword_t aname = attrtable[2 + j];
                        if (system_version_id == (confword_t)-1 && __strcmp(get_symbol(aname), "version") == 0)
                            system_version_id = j;
                        else if (system_freq_id == (confword_t)-1 && __strcmp(get_symbol(aname), "masterfreq") == 0)
                            system_freq_id = j;
                    }
                }
                if (__strcmp(get_symbol(name), "cpu") == 0)
                {
                    core_type_id = i;
                    for (size_t j = 0; j < nattrs; ++j)
                    {
                        confword_t aname = attrtable[2 + j];
                        if (core_freq_id == (confword_t)-1 && __strcmp(get_symbol(aname), "freq") == 0)
                            core_freq_id = j;
                        else if (core_threads_id == (confword_t)-1 && __strcmp(get_symbol(aname), "threads") == 0)
                            core_threads_id = j;
                        else if (core_families_id == (confword_t)-1 && __strcmp(get_symbol(aname), "families") == 0)
                            core_families_id = j;
                        else if (core_pid_id == (confword_t)-1 && __strcmp(get_symbol(aname), "pid") == 0)
                            core_pid_id = j;
                    }                    
                }
            }
            break;
        }
        case CONF_TAG_OBJECT:
        {
            confword_t t = p->payload[0];
            if (t == system_type_id)
            {
                mgconf_master_freq = p->payload[1 + 2*system_freq_id + 1];
                if (verbose_boot)
                {
                    output_string("  - system version: ", 2);
                    output_string(get_symbol(p->payload[1 + 2*system_version_id + 1]), 2);
                    output_string(".\n  - master frequency: ", 2);
                    output_uint(mgconf_master_freq, 2);
                    output_string("MHz.\n", 2);
                }
                // ensure we do this only once
                system_type_id = (confword_t)-1;
            }
            else if (t == core_type_id)
            {
                confword_t pid = p->payload[1 + 2*core_pid_id + 1],
                    cf = p->payload[1 + 2*core_freq_id + 1],
                    ttes = p->payload[1 + 2*core_threads_id + 1],
                    ftes = p->payload[1 + 2*core_families_id + 1];
                if (mgconf_core_freq != (confword_t)-1 && 
                    (cf != mgconf_core_freq || ttes != mgconf_ttes_per_core || ftes != mgconf_ftes_per_core))
                {
                    output_string("# warning: core ", 2);
                    output_uint(pid, 2);
                    output_string(" has different properties - the grid is not homogeneous!\n", 2);
                }
                else if (mgconf_core_freq == (confword_t)-1)
                {
                    mgconf_core_freq = cf;
                    mgconf_ttes_per_core = ttes;
                    mgconf_ftes_per_core = ftes;
                    if (verbose_boot)
                    {
                        output_string("  - core frequency: ", 2);
                        output_uint(mgconf_core_freq, 2);
                        output_string("MHz.\n  - ", 2);
                        output_uint(mgconf_ttes_per_core, 2);
                        output_string(" thread and ", 2);
                        output_uint(mgconf_ftes_per_core, 2);                    
                        output_string(" family entries per core.\n", 2);
                    }
                }
            }
        }
        }
    }
    if (verbose_boot)
    {
        output_string("  done.", 2);
        output_ts(2);
        output_char('\n', 2);
    }
}

#define MAX_NDEVS 128

static void *_dev_addrs[MAX_NDEVS];
static struct mg_device_id _dev_ids[MAX_NDEVS];


void sys_detect_devs(void)
{
    size_t i, j;

    /* check the system version */
    long system_version;
    mgsim_read_asr(system_version, ASR_SYSTEM_VERSION);
    if (system_version != ASR_SYSTEM_VERSION_VALUE)
    {
        output_string("Execution system type ", 2);
        output_uint(system_version, 2);
        output_string(" does not match expected value ", 2);
        output_uint(ASR_SYSTEM_VERSION_VALUE, 2);
        output_char('\n', 2);
        svp_abort();
    }

    /* ensure that we are connected to I/O */
    uint32_t io_params;
    size_t n_iodevs, n_chans;
    mgsim_read_asr(io_params, ASR_IO_PARAMS1);
    if (io_params == 0)
    {
        if (verbose_boot)
        {
            output_string("* not connected to I/O, unable to read configuration.", 2);
            output_ts(2);
            output_char('\n', 2);
        }
        return;
    }
    if (verbose_boot)
    {
        output_string("* connected to I/O, ", 2);
        output_uint(n_iodevs = (io_params & 0xff), 2);
        output_string(" devices, ", 2);
        output_uint(n_chans = ((io_params >> 8) & 0xff), 2);
        output_string(" notification channels.", 2);
        output_ts(2);
        output_char('\n', 2);
    }    

    mg_io_dca_devid = (io_params >> 24) & 0xff;

    uint32_t io_params2;
    mgsim_read_asr(io_params2, ASR_IO_PARAMS2);
    size_t dev_as_sz = 1UL << (io_params2 & 0xff);

    char *aio_base;
    mgsim_read_asr(aio_base, ASR_AIO_BASE);

    /* save the notification address */
    long *pnc_base;
    mgsim_read_asr(pnc_base, ASR_PNC_BASE);

    mg_devinfo.nchannels = n_chans;
    mg_devinfo.channels = pnc_base;
    mg_io_place_id = get_current_place();

    /* try to find the SMC */
    size_t smc_id = (io_params >> 16) & 0xff;
    void *smc = aio_base + dev_as_sz * smc_id;
    size_t ndevs = 0;
    if (verbose_boot)
    {
        output_string("* smc at 0x", 2);
        output_hex(smc, 2);
        output_string(", enumerates ", 2);
        output_uint(ndevs = *(uint64_t*)smc, 2);
        output_string(" devices.", 2);
        output_ts(2);
        output_char('\n', 2);
    }

    if (ndevs > n_iodevs)
    {
        output_string("# warning: not all devices are visible from the I/O core.", 2);
        ndevs = n_iodevs;
    }

    /* copy the enumeration data */
    mg_devinfo.ndevices = ndevs;

    struct mg_device_id *devenum = &_dev_ids[0];

#ifndef MGSIM_TEST_SUITE
    if (ndevs > MAX_NDEVS)
    {
        devenum = (struct mg_device_id*)dlmalloc(ndevs * sizeof(struct mg_device_id));
        if (devenum == NULL)
        {
            output_string("Unable to allocate memory to copy the enumeration data!\n", 2);
            svp_abort();
        }
    }
#endif
    for (i = 0; i < ndevs; ++i)
        devenum[i] = (((struct mg_device_id*)smc) + 1)[i];

    mg_devinfo.enumeration = devenum;
    if (verbose_boot)
    {
        output_string("* I/O enumeration data at 0x", 2);
        output_hex(devenum, 2);
        output_char('.', 2);
        output_ts(2);
        output_char('\n', 2);
    }

    /* set up the device base addresses */

    void **addrs = &_dev_addrs[0];

#ifndef MGSIM_TEST_SUITE
    if (ndevs > MAX_NDEVS)
    {
        addrs = (void**)dlmalloc(ndevs * sizeof(void*));
        if (addrs == NULL)
        {
            output_string("Unable to allocate memory to set up the device base addresses!\n", 2);
            svp_abort();
        }
    }
#endif

    const char *devname;
    for (i = 0; i < ndevs; ++i)
    {
        addrs[i] = aio_base + dev_as_sz * i;
    }
    mg_devinfo.base_addrs = addrs;

    /* detect the devices */

    for (i = 0; i < ndevs; ++i)
    {
        devname = 0;

        /* try to recognize some useful devices */
        for (j = 0; known_devs[j].name != 0; ++j)
        {
            if (known_devs[j].id.provider == devenum[i].provider &&
                known_devs[j].id.model == devenum[i].model &&
                known_devs[j].id.revision == devenum[i].revision)
            {
                if (known_devs[j].detect != 0)
                {
                    known_devs[j].detect(i, addrs[i]);
                }
                else if (verbose_boot)
                {
                    output_string("* unknown device ", 2);
                    output_uint(devenum[i].provider, 2);
                    output_char('/', 2);
                    output_uint(devenum[i].model, 2);
                    output_char('/', 2);
                    output_uint(devenum[i].revision, 2);
                    output_string(" at 0x", 2);
                    output_hex(addrs[i], 2);
                    output_char('.', 2); 
                    output_ts(2);
                    output_char('\n', 2);
                }
                break;
            }
        }
    }

}

