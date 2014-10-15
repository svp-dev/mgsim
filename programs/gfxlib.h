#ifndef MGSIM_GFXLIB_H
#define MGSIM_GFXLIB_H

#include <stdint.h>

struct gfx_palette;
struct gfx_canvas;
typedef struct
{
    struct gfx_palette *palette;
    struct gfx_canvas *canvas;
    // private:
    int used;
} gfx_frame;

typedef struct gfx_palette
{
    uint32_t ncolors;
    uint32_t *color_data;
} gfx_palette;

typedef struct gfx_canvas
{
    uint32_t bpp;
    uint16_t w;
    uint16_t h;
    uint32_t pitch;
    void *pixels;
} gfx_canvas;

int gfx_initialize(void);

void gfx_set_resolution(int w, int h);
void gfx_get_resolution(int *w, int *h);
void gfx_get_max_resolution(int *w, int *h);
void gfx_dump_image(int key, int stream, int dump_ts);

gfx_canvas* gfx_alloc_canvas(int w, int h, int bpp);
void gfx_free_canvas(gfx_canvas *cv);
int gfx_draw_pixel_indexed(gfx_canvas *win, int x, int y, int value);
int gfx_draw_pixel_rgb(gfx_canvas *win, int x, int y, int r, int g, int b);
int gfx_resize_canvas(gfx_canvas *win, int new_w, int new_h, int new_bpp);

gfx_palette* gfx_alloc_palette(unsigned ncolors);
void gfx_free_palette(gfx_palette *p);
int gfx_resize_palette(gfx_palette *p, unsigned ncolors);
void gfx_set_palette_color_unchecked(gfx_palette *p, unsigned index, int r, int g, int b);
int gfx_set_palette_color(gfx_palette *p, unsigned index, int r, int g, int b);
int gfx_load_1bit_palette(gfx_palette *p, int r0, int g0, int b0, int r1, int g1, int b1);
int gfx_load_1bit_bw_palette(gfx_palette *p);
int gfx_load_8bit_grayscale_palette(gfx_palette *p);
int gfx_load_5bit_cga_palette(gfx_palette *p);

gfx_frame* gfx_alloc_frame(int x, int y);
void gfx_free_frame(gfx_frame *f);

void gfx_move_frame(gfx_frame *f, int x, int y);
void gfx_resize_frame(gfx_frame *f, int screen_w, int screen_h);
void gfx_hide_frame(gfx_frame *f);
void gfx_show_frame(gfx_frame *f);

void gfx_attach_canvas(gfx_frame *f, gfx_canvas *c, int resize_frame);
void gfx_detach_canvas(gfx_frame *f, gfx_canvas *c);
void gfx_attach_palette(gfx_frame *f, gfx_palette *p);
void gfx_detach_palette(gfx_frame *f, gfx_palette *p);

#endif
