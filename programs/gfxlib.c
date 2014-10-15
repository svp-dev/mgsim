#include <svp/testoutput.h>
#include <svp/mgsim.h>
#include <stddef.h>

#include "gfxlib.h"
#include "mtconf.h"

typedef struct
{
    uint32_t cmd1;
    uint32_t psize;
    uint32_t paddr;
    uint32_t cmd2;
    uint32_t mode;
    uint32_t start;
    uint32_t pitch;
    uint32_t size;
    uint32_t offset;
    uint32_t screen_size;
} gfx_command;


// Chunks are blocks of space in video memory;
// They can be used to store palettes or canvases.
#define FB_SIZE 5*1024*1024
#define MAX_CHUNKS (FB_SIZE / (256*256*4))
#define MAX_CHUNK_SIZE (FB_SIZE / MAX_CHUNKS)
static int g_chunks_alloc[MAX_CHUNKS];

// Canvases and palettes are data structures visible by programs. They
// are stored in chunks, so there cannot be more than there are chunks
// available.
#define MAX_PALETTES MAX_CHUNKS
static gfx_palette g_palettes[MAX_CHUNKS];
#define MAX_CANVASES MAX_CHUNKS
static gfx_canvas  g_canvases[MAX_CHUNKS];

// Frames are data structures visible by programs.
// They are stored in the first chunk, so there cannot be more
// than there can fit in one chunk.
#define MAX_FRAMES 30 // (MAX_CHUNK_SIZE / sizeof(gfx_command))
static gfx_frame   g_frames[MAX_FRAMES];
static volatile gfx_command* g_commands = 0;

static
int gfx_canvas_to_chunk_index(gfx_canvas *c)
{
    return c - &g_canvases[0];
}
static
int gfx_palette_to_chunk_index(gfx_palette *c)
{
    return c - &g_palettes[0];
}
static
int gfx_frame_to_command_index(gfx_frame *f)
{
    return f - &g_frames[0];
}
static
uint32_t gfx_chunk_base_offset(int chunk_index)
{
    return chunk_index * MAX_CHUNK_SIZE;
}
static
void * gfx_chunk_base_data(int chunk_index)
{
    return (char*)mg_gfx_fb + chunk_index * MAX_CHUNK_SIZE;
}

/******************** canvas operations ***********************/

void gfx_attach_canvas(gfx_frame *f, gfx_canvas *c, int resize_frame)
{
    int i = gfx_frame_to_command_index(f);
    int ch = gfx_canvas_to_chunk_index(c);
    g_commands[i].mode = ((!!f->palette) << 16) | c->bpp;
    g_commands[i].start = gfx_chunk_base_offset(ch) / 4;
    g_commands[i].pitch = c->pitch * 8 / c->bpp;
    g_commands[i].size = (c->w << 16) | c->h;
    if (resize_frame)
        g_commands[i].screen_size = (c->w << 16) | c->h;
    g_commands[i].cmd2 = 0x206;
}

void gfx_detach_canvas(gfx_frame *f, gfx_canvas *c)
{
    int i = gfx_frame_to_command_index(f);
    g_commands[i].cmd2 = 0x306;
    g_commands[i].mode = 0;
    g_commands[i].start = 0;
    g_commands[i].pitch = 0;
    g_commands[i].size = 0;
    f->canvas = 0;
}

gfx_canvas* gfx_alloc_canvas(int w, int h, int bpp)
{
    // pitch can be either w, or if bpp == 1 the first multiple of 8
    // equal or greater.
    uint32_t pitch = (bpp == 1) ? ((w + 7) & ~7) : w;
    // pitch must be first multiple of 4 larger or equal to w*bpp/8
    if ((pitch * bpp / 8) * h > MAX_CHUNK_SIZE)
        return 0;

    for (int i = 0; i < MAX_CHUNKS; ++i)
        if (g_chunks_alloc[i] == 0)
        {
            g_chunks_alloc[i] = 1;
            g_canvases[i].bpp = bpp;
            g_canvases[i].w = w;
            g_canvases[i].h = h;
            g_canvases[i].pitch = pitch * bpp / 8;
            g_canvases[i].pixels = gfx_chunk_base_data(i);
            return &g_canvases[i];
        }
    return 0;
}

void gfx_free_canvas(gfx_canvas *cv)
{
    int c = gfx_canvas_to_chunk_index(cv);
    g_chunks_alloc[c] = 0;
    for (unsigned i = 0; i < MAX_FRAMES; ++i)
        if (g_frames[i].canvas == cv)
            gfx_detach_canvas(g_frames + i, cv);
}

int gfx_draw_pixel_indexed(gfx_canvas *win, int x, int y, int value)
{
    if (x < 0 || y < 0 || x > win->w || y > win->h)
        return 0;

    switch (win->bpp)
    {
    case 1:
    {
        volatile uint8_t* p = ((uint8_t*)win->pixels) + y * win->pitch + (x / 8);
        uint8_t mask = 1 << (x % 8);
        if (!value)
            *p &= ~mask;
        else
            *p |= mask;
        break;
    }
    case 8:
        ((volatile uint8_t*)win->pixels)[y * win->pitch + x] = value;
        break;
    case 16:
        ((volatile uint16_t*)win->pixels)[y * win->pitch / 2 + x] = value;
        break;
    default:
        return 0;
    }
    return 1;
}

int gfx_draw_pixel_rgb(gfx_canvas *win, int x, int y, int r, int g, int b)
{
    if (x < 0 || y < 0 || x > win->w || y > win->h)
        return 0;

    switch (win->bpp)
    {
    case 8:
        ((volatile uint8_t*)win->pixels)[y * win->pitch + x] =
            (r & 0xe) | ((g & 0xe) >> 3) | ((b & 0xc) >> 6);
        break;
    case 16:
        ((volatile uint16_t*)win->pixels)[y * win->pitch / 2 + x] =
            ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | ((b & 0xf8) >> 3);
        break;
    case 24:
        ((volatile uint8_t*)win->pixels)[y * win->pitch + x*3] = r;
        ((volatile uint8_t*)win->pixels)[y * win->pitch + x*3 + 1] = g;
        ((volatile uint8_t*)win->pixels)[y * win->pitch + x*3 + 2] = b;
        break;
    case 32:
        ((volatile uint32_t*)win->pixels)[y * win->pitch / 4 + x] =
            (((uint32_t)r & 0xff) << 16) | (((uint32_t)g & 0xff) << 8) | ((uint32_t)b & 0xff);
        break;
    default:
        return 0;
    }
    return 1;
}

int gfx_resize_canvas(gfx_canvas *win, int new_w, int new_h, int new_bpp)
{
    // pitch can be either w, or if bpp == 1 the first multiple of 8
    // equal or greater.
    uint32_t pitch = (new_bpp == 1) ? ((new_w + 7) & ~7) : new_w;
    // pitch must be first multiple of 4 larger or equal to w*bpp/8
    if (pitch * new_h * new_bpp / 8 > MAX_CHUNK_SIZE)
        return 0;
    win->bpp = new_bpp;
    win->w = new_w;
    win->h = new_h;
    win->pitch = pitch * new_bpp / 8;
    for (unsigned i = 0; i < MAX_FRAMES; ++i)
        if (g_frames[i].canvas == win)
        {
            g_commands[i].pitch = pitch;
            g_commands[i].size = new_w << 16 | new_h;
        }
    return 1;
}


/******************** palette operations ***********************/

void gfx_attach_palette(gfx_frame *f, gfx_palette *p)
{
    int i = gfx_frame_to_command_index(f);
    int ch = gfx_palette_to_chunk_index(p);
    g_commands[i].psize = p->ncolors;
    g_commands[i].paddr = gfx_chunk_base_offset(ch) / 4;
    g_commands[i].cmd1 = 0x102;
    g_commands[i].mode |= 1 << 16;
}

void gfx_detach_palette(gfx_frame *f, gfx_palette *p)
{
    int i = gfx_frame_to_command_index(f);
    g_commands[i].cmd1 = 0x302;
    g_commands[i].psize = 0;
    g_commands[i].paddr = 0;
    g_commands[i].mode &= 0xffff;
    f->palette = 0;
}


gfx_palette* gfx_alloc_palette(unsigned ncolors)
{
    if (ncolors < 0 || ncolors > (MAX_CHUNK_SIZE / 4))
        return 0;

    for (int i = 0; i < MAX_CHUNKS; ++i)
        if (g_chunks_alloc[i] == 0)
        {
            g_chunks_alloc[i] = 1;
            g_palettes[i].ncolors = ncolors;
            g_palettes[i].color_data = gfx_chunk_base_data(i);
            return &g_palettes[i];
        }
    return 0;
}

void gfx_free_palette(gfx_palette *p)
{
    int c = gfx_palette_to_chunk_index(p);
    g_chunks_alloc[c] = 0;
    for (unsigned i = 0; i < MAX_FRAMES; ++i)
        if (g_frames[i].palette == p)
            gfx_detach_palette(g_frames + i, p);
}

int gfx_resize_palette(gfx_palette *p, unsigned ncolors)
{
    if (ncolors < 0 || ncolors > (MAX_CHUNK_SIZE / 4))
        return 0;
    p->ncolors = ncolors;
    for (unsigned i = 0; i < MAX_FRAMES; ++i)
        if (g_frames[i].palette == p)
            g_commands[i].psize = ncolors;
    return 1;
}

void gfx_set_palette_color_unchecked(gfx_palette *p, unsigned index, int r, int g, int b)
{
    volatile uint32_t* palette = p->color_data;
    palette[index] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

int gfx_set_palette_color(gfx_palette *p, unsigned index, int r, int g, int b)
{
    if (index < 0 || index > p->ncolors)
        return 0;
    gfx_set_palette_color_unchecked(p, index, r, g, b);
    return 1;
}

int gfx_load_1bit_palette(gfx_palette *p, int r0, int g0, int b0, int r1, int g1, int b1)
{
    if (p->ncolors < 2)
        return 0;

    gfx_set_palette_color_unchecked(p, 0, r0, g0, b0);
    gfx_set_palette_color_unchecked(p, 1, r1, g1, b1);
    return 1;
}

int gfx_load_1bit_bw_palette(gfx_palette *p)
{
    return gfx_load_1bit_palette(p, 0, 0, 0, 255, 255, 255);
}

int gfx_load_8bit_grayscale_palette(gfx_palette *p)
{
    if (p->ncolors < 256)
        return 0;

    for (int i = 0; i < 256; ++i)
        gfx_set_palette_color_unchecked(p, i, i, i, i);
    return 1;
}

int gfx_load_5bit_cga_palette(gfx_palette *p)
{
    if (p->ncolors < 16)
        return 0;

    gfx_set_palette_color_unchecked(p, 0, 0, 0, 0);
    gfx_set_palette_color_unchecked(p, 1, 0, 0, 0x80);
    gfx_set_palette_color_unchecked(p, 2, 0, 0x80, 0);
    gfx_set_palette_color_unchecked(p, 3, 0, 0x80, 0x80);
    gfx_set_palette_color_unchecked(p, 4, 0x80, 0, 0);
    gfx_set_palette_color_unchecked(p, 5, 0x80, 0, 0x80);
    gfx_set_palette_color_unchecked(p, 6, 0x80, 0x80, 0);
    gfx_set_palette_color_unchecked(p, 7, 0xc0, 0xc0, 0xc0);
    gfx_set_palette_color_unchecked(p, 8, 0x80, 0x80, 0x80);
    gfx_set_palette_color_unchecked(p, 9, 0, 0, 0xff);
    gfx_set_palette_color_unchecked(p, 10, 0, 0xff, 0);
    gfx_set_palette_color_unchecked(p, 11, 0, 0xff, 0xff);
    gfx_set_palette_color_unchecked(p, 12, 0xff, 0, 0);
    gfx_set_palette_color_unchecked(p, 13, 0xff, 0, 0xff);
    gfx_set_palette_color_unchecked(p, 14, 0xff, 0xff, 0);
    gfx_set_palette_color_unchecked(p, 15, 0xff, 0xff, 0xff);
    return 1;
}


/******************** frame operations ***********************/

void gfx_free_frame(gfx_frame *f)
{
    int i = gfx_frame_to_command_index(f);
    g_commands[i].cmd1 = 0x309;
    g_frames[i].canvas = 0;
    g_frames[i].palette = 0;
    g_frames[i].used = 0;
}

gfx_frame* gfx_alloc_frame(int x, int y)
{
    for (int i = 0; i < MAX_FRAMES; ++i)
        if (g_frames[i].used == 0)
        {
            g_frames[i].used = 1;
            g_frames[i].canvas = 0;
            g_frames[i].palette = 0;
            g_commands[i].cmd1 = 0x302;
            g_commands[i].cmd2 = 0x306;
            g_commands[i].offset = x << 16 | (y & 0xffff);
            g_commands[i].screen_size = 0;
            return &g_frames[i];
        }
    return 0;
}

void gfx_move_frame(gfx_frame *f, int x, int y)
{
    int cmd = gfx_frame_to_command_index(f);
    g_commands[cmd].offset = (x << 16) | (y & 0xffff);
}

void gfx_resize_frame(gfx_frame *f, int screen_w, int screen_h)
{
    int cmd = gfx_frame_to_command_index(f);
    g_commands[cmd].screen_size = (screen_w << 16) | screen_h;
}

void gfx_hide_frame(gfx_frame *f)
{
    int cmd = gfx_frame_to_command_index(f);
    g_commands[cmd].cmd2 = 0x306;
}

void gfx_show_frame(gfx_frame *f)
{
    if (f->canvas) {
        int cmd = gfx_frame_to_command_index(f);
        g_commands[cmd].cmd2 = 0x206;
    }
}

void gfx_set_resolution(int w, int h)
{
    if (mg_gfx_ctl == 0 || mg_gfx_fb == 0)
        return ;
    mg_gfx_ctl[1] = w;
    mg_gfx_ctl[2] = h;
    mg_gfx_ctl[0] = 1;
}

void gfx_get_resolution(int *w, int *h)
{
    if (mg_gfx_ctl == 0 || mg_gfx_fb == 0)
    { *w = *h = 0; return; }
    *w = mg_gfx_ctl[1];
    *h = mg_gfx_ctl[2];
}

void gfx_get_max_resolution(int *w, int *h)
{
    if (mg_gfx_ctl == 0 || mg_gfx_fb == 0)
    { *w = *h = 0; return; }
    *w = mg_gfx_ctl[6];
    *h = mg_gfx_ctl[7];
}

void gfx_dump_image(int key, int stream, int dump_ts)
{
    if (mg_gfx_ctl == 0 || mg_gfx_fb == 0)
        return;
    mg_gfx_ctl[5] = key;
    mg_gfx_ctl[4] = (dump_ts << 8) | stream;
}

int gfx_initialize(void)
{
    if (mg_gfx_ctl == 0 || mg_gfx_fb == 0)
        return 0;
    g_commands = (gfx_command*)(void*)mg_gfx_fb;

    // all commands are initially no-ops.
    for (int i = 0; i < MAX_FRAMES; ++i)
    {
        g_frames[i].used = 0;
        g_commands[i].cmd1 = 0x309;
    }

    // chunk 0 is reserved.
    g_chunks_alloc[0] = 1;
    for (int i = 1; i < MAX_CHUNKS; ++i)
        g_chunks_alloc[i] = 0;
    return 1;
}
