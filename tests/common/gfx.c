#include <svp/testoutput.h>
#include <svp/mgsim.h>
#include <stdint.h>
#include <stddef.h>

#include "mtconf.h"
#include "gfxlib.h"

const char *testconf = "\0PLACES: 1"; // for "make check": this program is single-threaded.

int test(void)
{
    sys_detect_devs();
    sys_conf_init();

    if (!gfx_initialize())
        return 1;

    gfx_set_resolution(640, 480);

    int w, h;
    gfx_get_resolution(&w, &h);

    output_string("screen width: ", 1);
    output_uint(w, 1);
    output_char('\n', 1);
    output_string("screen height: ", 1);
    output_uint(h, 1);
    output_char('\n', 1);

    output_string("preparing data...\n", 1);

    int k = 1;

    // first a simple 4-bit square
    gfx_palette *p1 = gfx_alloc_palette(256);
    output_string("palette ", 1);
    output_hex(p1->color_data - (uint32_t*)mg_gfx_fb, 1);
    output_char('\n', 1);
    gfx_load_1bit_palette(p1, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc);
    output_string("data ", 1);
    output_hex(p1->color_data[0], 1);
    output_char(' ', 1);
    output_hex(p1->color_data[1], 1);
    output_char('\n', 1);

    gfx_canvas *imgbin = gfx_alloc_canvas(2, 2, 8);
    ((volatile uint8_t*)imgbin->pixels)[0] = 0;
    ((volatile uint8_t*)imgbin->pixels)[1] = 1;
    ((volatile uint8_t*)imgbin->pixels)[imgbin->pitch + 0] = 1;
    ((volatile uint8_t*)imgbin->pixels)[imgbin->pitch + 1] = 0;

    gfx_frame *f1 = gfx_alloc_frame(5, 10);
    gfx_attach_canvas(f1, imgbin, 0);
    gfx_attach_palette(f1, p1);
    gfx_resize_frame(f1, 90, 50);
    gfx_show_frame(f1);

    output_string("done, dumping\n", 1);
    gfx_dump_image(k++, 0, 0);

    ////////////////////////////////////////////////////

    // first a simple 4-bit square
    gfx_palette *p1x = gfx_alloc_palette(256);
    gfx_load_1bit_bw_palette(p1x);

    gfx_canvas *imgbin2 = gfx_alloc_canvas(2, 2, 8);
    gfx_draw_pixel_indexed(imgbin2, 0, 0, 1);
    gfx_draw_pixel_indexed(imgbin2, 0, 1, 0);
    gfx_draw_pixel_indexed(imgbin2, 1, 0, 0);
    gfx_draw_pixel_indexed(imgbin2, 1, 1, 1);

    gfx_frame *f1x = gfx_alloc_frame(105, 10);
    gfx_attach_canvas(f1x, imgbin2, 0);
    gfx_attach_palette(f1x, p1x);
    gfx_resize_frame(f1x, 90, 50);
    gfx_show_frame(f1x);

    output_string("done, dumping\n", 1);
    gfx_dump_image(k++, 0, 0);

    ////////////////////////////////////////////////////

    gfx_canvas *imgbin3 = gfx_alloc_canvas(2, 2, 1);
    gfx_draw_pixel_indexed(imgbin3, 0, 0, 1);
    gfx_draw_pixel_indexed(imgbin3, 0, 1, 0);
    gfx_draw_pixel_indexed(imgbin3, 1, 0, 0);
    gfx_draw_pixel_indexed(imgbin3, 1, 1, 1);

    gfx_frame *f1y = gfx_alloc_frame(205, 10);
    gfx_attach_canvas(f1y, imgbin3, 0);
    gfx_attach_palette(f1y, p1);
    gfx_resize_frame(f1y, 90, 50);
    gfx_show_frame(f1y);

    output_string("done, dumping\n", 1);
    gfx_dump_image(k++, 0, 0);

    ////////////////////////////////////////////////////

    gfx_canvas *img8 = gfx_alloc_canvas(20, 20, 8);
    for (unsigned i = 0; i < 20; ++i)
    {
        gfx_draw_pixel_indexed(img8, i, i, 255);
        gfx_draw_pixel_indexed(img8, i, 19-i, (i+1) * 255 / 20);
    }

    gfx_palette *p2 = gfx_alloc_palette(256);
    gfx_load_8bit_grayscale_palette(p2);

    gfx_frame *f3 = gfx_alloc_frame(0, 0);
    gfx_attach_canvas(f3, img8, 0);
    gfx_attach_palette(f3, p2);
    gfx_move_frame(f3, 305, 10);
    gfx_resize_frame(f3, 90, 50);
    gfx_show_frame(f3);

    output_string("done, dumping\n", 1);
    gfx_dump_image(k++, 0, 0);
    ////////////////////////////////////////////////////

    gfx_canvas *img8_2 = gfx_alloc_canvas(16, 16, 8);
    for (unsigned i = 0; i < 256; ++i)
        ((volatile uint8_t*)img8_2->pixels)[(i / 16) * img8_2->pitch + i%16] = i;

    gfx_frame *f4 = gfx_alloc_frame(405, 10);
    gfx_attach_canvas(f4, img8_2, 0);
    gfx_resize_frame(f4, 90, 50);
    gfx_show_frame(f4);

    output_string("done, dumping\n", 1);
    gfx_dump_image(k++, 0, 0);
    mg_gfx_ctl[5] = k++;
    mg_gfx_ctl[4] = 0;

    ////////////////////////////////////////////////////

    gfx_canvas *img24 = gfx_alloc_canvas(4, 2, 24);
    gfx_draw_pixel_rgb(img24, 0, 0, 0, 0, 0);
    gfx_draw_pixel_rgb(img24, 1, 0, 0, 0, 255);
    gfx_draw_pixel_rgb(img24, 2, 0, 0, 255, 0);
    gfx_draw_pixel_rgb(img24, 3, 0, 0, 255, 255);
    gfx_draw_pixel_rgb(img24, 0, 1, 255, 0, 0);
    gfx_draw_pixel_rgb(img24, 1, 1, 255, 0, 255);
    gfx_draw_pixel_rgb(img24, 2, 1, 255, 255, 0);
    gfx_draw_pixel_rgb(img24, 3, 1, 255, 255, 255);

    gfx_frame *f24 = gfx_alloc_frame(5, 330);
    gfx_attach_canvas(f24, img24, 0);
    gfx_resize_frame(f24, 90, 50);
    gfx_show_frame(f24);

    output_string("done, dumping\n", 1);
    gfx_dump_image(k++, 0, 0);

    ////////////////////////////////////////////////////

    gfx_canvas *img32 = gfx_alloc_canvas(4, 2, 32);
    gfx_draw_pixel_rgb(img32, 0, 0, 0, 0, 0);
    gfx_draw_pixel_rgb(img32, 1, 0, 0, 0, 255);
    gfx_draw_pixel_rgb(img32, 2, 0, 0, 255, 0);
    gfx_draw_pixel_rgb(img32, 3, 0, 0, 255, 255);
    gfx_draw_pixel_rgb(img32, 0, 1, 255, 0, 0);
    gfx_draw_pixel_rgb(img32, 1, 1, 255, 0, 255);
    gfx_draw_pixel_rgb(img32, 2, 1, 255, 255, 0);
    gfx_draw_pixel_rgb(img32, 3, 1, 255, 255, 255);

    gfx_frame *f32 = gfx_alloc_frame(105, 330);
    gfx_attach_canvas(f32, img32, 0);
    gfx_resize_frame(f32, 90, 50);
    gfx_show_frame(f32);

    output_string("done, dumping\n", 1);
    gfx_dump_image(k++, 0, 0);

    ////////////////////////////////////////////////////

    //// Comment out the following line to test the big images too
    return 0;
    ////

    output_string("big image...\n", 1);
    gfx_canvas *img16 = gfx_alloc_canvas(256, 256, 16);
    for (unsigned i = 0; i < 65536; ++i)
        ((volatile uint16_t*)img16->pixels)[(i / 256) * img16->pitch / 2 + i%256] = i;

    gfx_frame *f5 = gfx_alloc_frame(5, 70);
    gfx_attach_canvas(f5, img16, 0);
    gfx_resize_frame(f5, 256, 256);
    gfx_show_frame(f5);

    output_string("done, dumping\n", 1);
    gfx_dump_image(k++, 0, 0);
    ////////////////////////////////////////////////////

    output_string("big palette...\n", 1);
    gfx_palette *p16 = gfx_alloc_palette(65536);
    output_string("palette ", 1);
    output_hex(p16, 1);
    output_string(" data ", 1);
    output_hex(p16->color_data, 1);
    output_char('\n', 1);
    for (uint32_t i = 0; i < 65536; ++i)
    {
        //output_uint(i, 1); output_char('\n', 1);
        gfx_set_palette_color(p16, i, i % 256, i / 256, 0);
    }

    output_string("configuring...\n", 1);

    gfx_frame *f6 = gfx_alloc_frame(310, 70);
    gfx_attach_canvas(f6, img16, 0);
    gfx_attach_palette(f6, p16);
    gfx_resize_frame(f6, 300, 300);
    gfx_show_frame(f6);

    output_string("done, dumping\n", 1);
    gfx_dump_image(k++, 0, 0);
    ////////////////////////////////////////////////////

    output_string("finished\n", 1);

    return 0;
}
