#include <arch/dev/Display.h>

namespace Simulator
{
    void Display::PrepareLogicalScreen()
    {
        auto vsz = m_video_memory.size();
        auto vsz32 = vsz / 4;
        auto v32 = (const uint32_t*)&m_video_memory[0];

        uint32_t palette_ncolors = 0;
        uint32_t palette_offset = 0;

        for (uint32_t i = m_command_offset;
             (i < vsz32) && (i + (v32[i] & 0xff)) <= vsz32;
             i += 1 + (v32[i] & 0xff))
        {
            auto command = v32[i] >> 8;

            if (command == 0)
                // end-of-commands: stop
                break;

            else if (command == 3)
                // No-op; do nothing
                continue;

            else if (command == 1)
            {
                // command 1 = set palette data

                if (v32[i+2] >= vsz32)
                    DebugIOWrite("Invalid palette offset: %u", (unsigned)v32[i+2]);
                else if (v32[i+2] + v32[i+1] > vsz32)
                    DebugIOWrite("Palette at 4x%u, size 4x%u bytes lies beyond video memory",
                                 (unsigned)v32[i+2], (unsigned)v32[i+1]);
                else
                {
                    palette_ncolors = v32[i+1];
                    palette_offset = v32[i+2];
                }
            }

            else if (command == 2)
            {
                // command 2 = draw texture

                // indexed: whether a palette is used
                auto indexed = v32[i+1] >> 16;
                // bit depth in bits
                auto bpp = v32[i+1] & 0xffff;
                // start of texture from start of video memory, measured in bytes
                auto src_start = 4 * v32[i+2];
                // number of *pixels* in video memory between subsequent lines in the texture
                // NB: this is a different definition for pitch than is used in SDL (SDL uses bytes)
                auto src_pitch = v32[i+3];
                // texture size
                auto src_w = v32[i+4] >> 16;
                auto src_h = v32[i+4] & 0xffff;

                // destination position on screen
                int32_t dst_x = v32[i+5] >> 16;
                int32_t dst_y = v32[i+5] & 0xffff;
                // the position is 16-bit two's complement, so adjust sign as needed
                if (dst_x & 0x8000) dst_x = - (int32_t)((~dst_x + 1) & 0xffff);
                if (dst_y & 0x8000) dst_y = - (int32_t)((~dst_y + 1) & 0xffff);
                // destination size on screen
                auto dst_w = v32[i+6] >> 16;
                auto dst_h = v32[i+6] & 0xffff;

                // if rendering entire out of screen, do not go further
                if (dst_x + dst_w <= 0 || dst_y + dst_h <= 0 ||
                    dst_x >= (int)m_logical_width || dst_y >= (int)m_logical_height)
                    continue;

                auto size_bytes = (bpp * src_pitch / 8) * src_h;

                if (src_start + size_bytes > vsz)
                    DebugIOWrite("Frame data at %u, size %lu bytes lies beyond video memory (%zu)",
                                 (unsigned)src_start, (unsigned long)size_bytes, (size_t)vsz);
                else
                {
                    uint32_t* dst_base = &m_logical_screen_pixels[0];
                    unsigned dst_pitch = m_logical_width;

                    // the texture may have a different size from the
                    // output pane. So we need to scale.  NB: this is
                    // a different scaling from m_scalex/m_scaley,
                    // which indicate the scaling between logical
                    // screen and output SDL window size.
                    double scale_x = (double)src_w / (double)dst_w;
                    double scale_y = (double)src_h / (double)dst_h;

                    // The input and output panes may need to be
                    // clipped, if (part of) the specified output pane
                    // lies outside of the logical screen. Do it.
                    unsigned src_x = 0, src_y = 0;
                    if (dst_x < 0)
                    {
                        src_x = -dst_x * scale_x;
                        dst_w += dst_x;
                        dst_x = 0;
                    }
                    if (dst_y < 0)
                    {
                        src_y = -dst_y * scale_y;
                        dst_h += dst_y;
                        dst_y = 0;
                    }
                    if (dst_x + dst_w > m_logical_width)
                        dst_w = m_logical_width - dst_x;
                    if (dst_y + dst_h > m_logical_height)
                        dst_h = m_logical_height - dst_y;

                    // Now the rendering per se
                    if (bpp == 32 && !indexed)
                    {
                        // 32-bit XRGB, "truecolor", akin to SDL_PIXELFORMAT_RGB888, space inefficient but fast
                        const uint32_t* src_base = (const uint32_t*)(const void*)&m_video_memory[src_start];
                        for (unsigned dy = 0; dy < dst_h; ++dy)
                        {
                            unsigned sy = dy * scale_y;
                            const uint32_t* __restrict__ src = src_base + (src_y + sy) * src_pitch + src_x;
                            uint32_t* __restrict__ dst = dst_base + (dst_y + dy) * dst_pitch + dst_x;
                            for (unsigned dx = 0; dx < dst_w; ++dx)
                            {
                                unsigned sx = dx * scale_x;
                                dst[dx] = src[sx];
                            }
                        }
                    }
                    else if (bpp == 24 && !indexed)
                    {
                        // 24-bit RGB, "truecolor", akin to SDL_PIXELFORMAT_RGB24, inefficient memory accesses!
                        const uint8_t* src_base = (const uint8_t*)(const void*)&m_video_memory[src_start];
                        for (unsigned dy = 0; dy < dst_h; ++dy)
                        {
                            unsigned sy = dy * scale_y;
                            const uint8_t* __restrict__ src = src_base + ((src_y + sy) * src_pitch + src_x) * 3;
                            uint32_t* __restrict__ dst = dst_base + (dst_y + dy) * dst_pitch + dst_x;
                            for (unsigned dx = 0; dx < dst_w; ++dx)
                            {
                                unsigned sx = dx * scale_x;
                                dst[dx] = (((uint32_t)src[sx * 3]) << 16)
                                    | (((uint32_t)src[sx * 3 + 1]) << 8)
                                    | (((uint32_t)src[sx * 3 + 2]));
                            }
                        }
                    }
                    else if (bpp == 16 && !indexed)
                    {
                        // 16-bit RGB, "64K colors", akin to SDL_PIXELFORMAT_RGB565
                        static constexpr float Rf = 0xff / (float)0xf8;
                        static constexpr float Gf = 0xff / (float)0xfc;
                        static constexpr float Bf = Rf;
                        const uint16_t* src_base = (const uint16_t*)(const void*)&m_video_memory[src_start];
                        for (unsigned dy = 0; dy < dst_h; ++dy)
                        {
                            unsigned sy = dy * scale_y;
                            const uint16_t* __restrict__ src = src_base + (src_y + sy) * src_pitch + src_x;
                            uint32_t* __restrict__ dst = dst_base + (dst_y + dy) * dst_pitch + dst_x;
                            for (unsigned dx = 0; dx < dst_w; ++dx)
                            {
                                unsigned sx = dx * scale_x;
                                uint16_t color = src[sx];
                                dst[dx] = (((uint32_t)(((color & 0xf800) >> 8) * Rf)) << 16)
                                    | (((uint32_t)(((color & 0x07e0) >> 3) * Gf)) << 8)
                                    | (((uint32_t)(((color & 0x001f) << 3) * Bf)));
                            }
                        }
                    }
                    else if (bpp == 16 && indexed && palette_offset != 0 && palette_ncolors >= 65536)
                    {
                        // 16-bit indexed (palette with 64K entries, uncommon)
                        const uint16_t* src_base = (const uint16_t*)(const void*)&m_video_memory[src_start];
                        for (unsigned dy = 0; dy < dst_h; ++dy)
                        {
                            unsigned sy = dy * scale_y;
                            const uint16_t* __restrict__ src = src_base + (src_y + sy) * src_pitch + src_x;
                            uint32_t* __restrict__ dst = dst_base + (dst_y + dy) * dst_pitch + dst_x;
                            for (unsigned dx = 0; dx < dst_w; ++dx)
                            {
                                unsigned sx = dx * scale_x;
                                uint16_t index = src[sx];
                                dst[dx] = v32[palette_offset + index];
                            }
                        }
                    }
                    else if (bpp == 8 && !indexed)
                    {
                        // 8-bit RGB "lowcolor", akin to SDL_PIXELFORMAT_RGB332, compact but uncommon
                        static constexpr float Rf = 0xff / (float)0xe0;
                        static constexpr float Gf = Rf;
                        static constexpr float Bf = 0xff / (float)0xc0;
                        const uint8_t* src_base = (const uint8_t*)(const void*)&m_video_memory[src_start];
                        for (unsigned dy = 0; dy < dst_h; ++dy)
                        {
                            unsigned sy = dy * scale_y;
                            const uint8_t* __restrict__ src = src_base + (src_y + sy) * src_pitch + src_x;
                            uint32_t* __restrict__ dst = dst_base + (dst_y + dy) * dst_pitch + dst_x;
                            for (unsigned dx = 0; dx < dst_w; ++dx)
                            {
                                unsigned sx = dx * scale_x;
                                uint8_t color = src[sx];
                                dst[dx] = ((uint32_t)(((color & 0xe0) * Rf)) << 16)
                                    | (((uint32_t)(((color & 0x1c) << 3) * Gf)) << 8)
                                    | (((uint32_t)(((color & 0x03) << 6) * Bf)));
                            }
                        }
                    }
                    else if (bpp == 8 && indexed && palette_offset != 0 && palette_ncolors >= 256)
                    {
                        // 8-bit indexed (palette with 256 entries), compact & common
                        const uint8_t* src_base = (const uint8_t*)(const void*)&m_video_memory[src_start];
                        for (unsigned dy = 0; dy < dst_h; ++dy)
                        {
                            unsigned sy = dy * scale_y;
                            const uint8_t* __restrict__ src = src_base + (src_y + sy) * src_pitch + src_x;
                            uint32_t* __restrict__ dst = dst_base + (dst_y + dy) * dst_pitch + dst_x;
                            for (unsigned dx = 0; dx < dst_w; ++dx)
                            {
                                unsigned sx = dx * scale_x;
                                uint8_t index = src[sx];
                                dst[dx] = v32[palette_offset + index];
                            }
                        }
                    }
                    else if (bpp == 4 && indexed && palette_offset != 0 && palette_ncolors >= 16)
                    {
                        // 4-bit indexed (palette with 16 entries), akin to VGA/EGA "16 color mode"
                        const uint8_t* src_base = (const uint8_t*)(const void*)&m_video_memory[src_start];
                        for (unsigned dy = 0; dy < dst_h; ++dy)
                        {
                            unsigned sy = dy * scale_y;
                            const uint8_t* __restrict__ src = src_base + (src_y + sy) * src_pitch / 2 + src_x / 2;
                            uint32_t* __restrict__ dst = dst_base + (dst_y + dy) * dst_pitch + dst_x;
                            for (unsigned dx = 0; dx < dst_w; ++dx)
                            {
                                unsigned sx = dx * scale_x;
                                uint8_t index = (src[sx / 2] >> (4 * (sx % 2))) & 0xf;
                                dst[dx] = v32[palette_offset + index];
                            }
                        }
                    }
                    else if (bpp == 1 && indexed && palette_offset != 0 && palette_ncolors >= 2)
                    {
                        // 1-bit indexed (palette with 2 entries) "mononochrome"
                        const uint8_t* src_base = (const uint8_t*)(const void*)&m_video_memory[src_start];
                        for (unsigned dy = 0; dy < dst_h; ++dy)
                        {
                            unsigned sy = dy * scale_y;
                            const uint8_t* __restrict__ src = src_base + (src_y + sy) * src_pitch / 8 + src_x / 8;
                            uint32_t* __restrict__ dst = dst_base + (dst_y + dy) * dst_pitch + dst_x;
                            for (unsigned dx = 0; dx < dst_w; ++dx)
                            {
                                unsigned sx = dx * scale_x;
                                uint8_t index = (src[sx / 8] >> (sx % 8)) & 1;
                                dst[dx] = v32[palette_offset + index];
                            }
                        }
                    }
                    else
                        DebugIOWrite("Unsupported pixel mode %x / indexed %d", (unsigned)v32[i+1], (int)indexed);
                }

            }
            else
                DebugIOWrite("Unsupported command %x", (unsigned)v32[i]);
        }

        m_logical_screen_updated = true;
    }

}
