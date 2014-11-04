#include "arch/dev/Display.h"
#include "sim/sampling.h"

#include <cstring>
#include <fstream>
#include <iomanip>

#ifdef USE_SDL
#include <SDL.h>
#endif

using namespace std;

namespace Simulator
{
    Display * Display::m_singleton = NULL;

    Display::FrameBufferInterface::FrameBufferInterface(const string& name, Display& parent, IIOBus& iobus, IODeviceID devid)
        : Object(name, parent),
          m_iobus(iobus),
          m_devid(devid)
    {
        iobus.RegisterClient(devid, *this);
    }

    bool Display::FrameBufferInterface::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        Display& disp = GetDisplay();
        if (address + size > disp.m_video_memory.size())
        {
            throw exceptf<>(*this, "FB read out of bounds: %#016llx (%u)", (unsigned long long)address, (unsigned)size);
        }

        IOData iodata;
        COMMIT {
            memcpy(iodata.data, &disp.m_video_memory[address], size);
        }
        iodata.size = size;

        DebugIOWrite("FB read: %#016llx/%u", (unsigned long long)address, (unsigned)size);

        if (!m_iobus.SendReadResponse(m_devid, from, address, iodata))
        {
            DeadlockWrite("Cannot send FB read response to I/O bus");
            return false;
        }
        return true;
    }

    bool Display::FrameBufferInterface::OnWriteRequestReceived(IODeviceID /*from*/, MemAddr address, const IOData& iodata)
    {
        Display& disp = GetDisplay();
        if (address >= disp.m_video_memory.size() || address + iodata.size >= disp.m_video_memory.size())
        {
            throw exceptf<>(*this, "FB write out of bounds: %#016llx (%u)", (unsigned long long)address, (unsigned)iodata.size);
        }

        COMMIT {
            memcpy(&disp.m_video_memory[address], iodata.data, iodata.size);
            disp.m_data_updated = true;
        }

        DebugIOWrite("FB write: %#016llx/%u", (unsigned long long)address, (unsigned)iodata.size);

        return true;
    }

    void Display::FrameBufferInterface::GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "GfxFB", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }
    }

    const string& Display::FrameBufferInterface::GetIODeviceName() const
    {
        return GetName();
    }

    Display::ControlInterface::ControlInterface(const string& name, Display& parent, IIOBus& iobus, IODeviceID devid)
        : Object(name, parent),
          m_iobus(iobus),
          m_control(2, 0),
          m_devid(devid),
          InitStateVariable(key, 0)
    {
        RegisterStateVariable(m_control, "control");
        iobus.RegisterClient(devid, *this);
    }

    void Display::ControlInterface::GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "GfxCtl", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }
    }

    bool Display::ControlInterface::OnWriteRequestReceived(IODeviceID /*from*/, MemAddr address, const IOData& iodata)
    {
        unsigned word = address / 4;

        if (address % 4 != 0 || iodata.size != 4)
        {
            throw exceptf<>(*this, "Invalid unaligned GfxCtl write: %#016llx (%u)", (unsigned long long)address, (unsigned)iodata.size);
        }
        if (word > 5)
        {
            throw exceptf<>(*this, "Invalid write to GfxCtl word: %u", word);
        }

        uint32_t value = UnserializeRegister(RT_INTEGER, iodata.data, iodata.size);
        Display& disp = GetDisplay();

        DebugIOWrite("Ctl write to word %u: %#016lx", word, (unsigned long)value);

        if (word == 0)
        {
            uint32_t req_w = m_control[0], req_h = m_control[1];
            size_t act_w, act_h;

            if      (req_w <= 10   && req_h <= 10  ) { act_w = 10  ; act_h = 10  ; }
            else if (req_w <= 100  && req_h <= 100 ) { act_w = 100 ; act_h = 100 ; }
            else if (req_w <= 160  && req_h <= 100 ) { act_w = 160 ; act_h = 100 ; }
            else if (req_w <= 160  && req_h <= 120 ) { act_w = 160 ; act_h = 120 ; }
            else if (req_w <= 320  && req_h <= 200 ) { act_w = 320 ; act_h = 200 ; }
            else if (req_w <= 320  && req_h <= 240 ) { act_w = 320 ; act_h = 240 ; }
            else if (req_w <= 640  && req_h <= 400 ) { act_w = 640 ; act_h = 400 ; }
            else if (req_w <= 640  && req_h <= 480 ) { act_w = 640 ; act_h = 480 ; }
            else if (req_w <= 800  && req_h <= 600 ) { act_w = 800 ; act_h = 600 ; }
            else if (req_w <= 1024 && req_h <= 768 ) { act_w = 1024; act_h = 768 ; }
            else if (req_w <= 1280 && req_h <= 1024) { act_w = 1280; act_h = 1024; }
            else
            { DebugIOWrite("unsupported resolution: %ux%u", (unsigned)req_w, (unsigned)req_h); return true; }

            if (act_w != req_w || act_h != req_h)
            { DebugIOWrite("unsupported resolution: %ux%u, adjusted to %ux%u", (unsigned)req_w, (unsigned)req_h, (unsigned)act_w, (unsigned)act_h); }

            COMMIT {
                disp.Resize(act_w, act_h, !!value);
            }
            DebugIOWrite("Setting resolution to %ux%u", (unsigned)act_w, (unsigned)act_h);
        }
        else if (word < 3)
        {
            COMMIT {
                m_control[word - 1] = value;
            }
        }
        else if (word == 3)
        {
            COMMIT {
                disp.m_command_offset = value;
                disp.m_data_updated = true;
            }
        }
        else if (word == 5)
        {
            COMMIT {
                m_key = value;
            }
        }
        else if (word == 4)
        {
            COMMIT {
                disp.DumpFrameBuffer(m_key, value & 0xff, (value >> 8) & 1);
            }
            DebugIOWrite("Dumping framebuffer");
        }
        return true;
    }

    bool Display::ControlInterface::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        // the display uses 32-bit status words
        // word 0: read: display enabled; write: commit mode from words 1/2/3
        // word 1: pixel width
        // word 2: pixel height
        // word 3: command offset
        // word 4: (unused)
        // word 5: next dump key
        // word 6: max supported width
        // word 7: max supported height
        // word 8: refresh delay
        // word 9: devid of the companion fb device
        // words 0x100-0x1ff: color palette (index mode only)

        unsigned word = address / 4;
        uint32_t value = 0;

        if (address % 4 != 0 || size != 4)
        {
            throw exceptf<>(*this, "Invalid unaligned GfxCtl read: %#016llx (%u)", (unsigned long long)address, (unsigned)size);
        }
        if (word > 9)
        {
            throw exceptf<>(*this, "Read from invalid GfxCtl word: %u", word);
        }

        Display& disp = GetDisplay();

        switch(word)
        {
        case 0: value = disp.m_enabled; break;
        case 1: value = disp.m_width; break;
        case 2: value = disp.m_height; break;
        case 3: value = disp.m_command_offset; break;
        case 4: value = 0; break;
        case 5: value = m_key; break;
        case 6: value = disp.m_max_screen_w; break;
        case 7: value = disp.m_max_screen_h; break;
        case 8: value = disp.m_refreshDelay; break;
        case 9: value = disp.m_fbinterface.m_devid; break;
        }

        IOData iodata;
        SerializeRegister(RT_INTEGER, value, iodata.data, 4);
        iodata.size = 4;

        DebugIOWrite("Ctl read from word %u: %#016lx", word, (unsigned long)value);

        if (!m_iobus.SendReadResponse(m_devid, from, address, iodata))
        {
            DeadlockWrite("Cannot send GfxCtl read response to I/O bus");
            return false;
        }

        return true;
    }

    const string& Display::ControlInterface::GetIODeviceName() const
    {
        return GetName();
    }


    Display::Display(const string& name, Object& parent, IIOBus& iobus, IODeviceID ctldevid, IODeviceID fbdevid)
        : Object(name, parent),
          m_ctlinterface("ctl", *this, iobus, ctldevid),
          m_fbinterface("fb", *this, iobus, fbdevid),
          m_screen(NULL),
          m_max_screen_h(1024),
          m_max_screen_w(1280),
          m_lastUpdate(0),
          m_data_updated(true),
          m_framebuffer(),
          m_video_memory(GetConf("GfxFrameSize", size_t), 0),
          InitStateVariable(enabled, false),
          InitStateVariable(width, 640),
          InitStateVariable(height, 400),
                              InitStateVariable(command_offset, 0),
          InitStateVariable(scalex_orig, 1.0f / max(1U, GetTopConf("SDLHorizScale", unsigned int))),
          InitStateVariable(scalex, m_scalex_orig),
          InitStateVariable(scaley_orig, 1.0f / max(1U, GetTopConf("SDLVertScale", unsigned int))),
          InitStateVariable(scaley, m_scaley_orig),
          InitStateVariable(refreshDelay_orig, GetTopConf("SDLRefreshDelay", unsigned int)),
          InitStateVariable(refreshDelay, m_refreshDelay_orig)
    {
        RegisterStateVariable(m_video_memory, "video_memory");

        if (GetConf("GfxEnableSDLOutput", bool))
        {
            if (m_singleton != NULL)
                throw InvalidArgumentException(*this, "Only one Display device can output to SDL.");
            m_singleton = this;

#ifdef USE_SDL
            if (SDL_Init(SDL_INIT_VIDEO) < 0) {
                cerr << "Unable to initialize SDL: " << SDL_GetError() << endl;
            } else {
                m_enabled = true;
            }
            const SDL_VideoInfo* vf = SDL_GetVideoInfo();
            if (vf) {
                m_max_screen_h = vf->current_h;
                m_max_screen_w = vf->current_w;
                cerr << "Maximum supported output size: "
                          << m_max_screen_w << 'x' << m_max_screen_h << endl;
            }
#endif
        }


    }


    void Display::ResizeScreen(unsigned int w, unsigned int h)
    {
        if (!m_enabled)
            return ;

        float r = (float)h / (float)w;

        //cerr << "DEBUG: fb size " << m_width << " " << m_height << endl;
        //cerr << "DEBUG: resizescreen " << w << " " << h << endl;
        w = min(m_max_screen_w, w); h = w * r;
        h = min(m_max_screen_h, h); w = h / r;
        //cerr << "DEBUG: after adjust " << w << " " << h << endl;

#ifdef USE_SDL
        if ((NULL == (m_screen = SDL_SetVideoMode(w, h, 32, SDL_SWSURFACE | SDL_RESIZABLE))) &&
            (NULL == (m_screen = SDL_SetVideoMode(640, 400, 32, SDL_SWSURFACE | SDL_RESIZABLE))))
        {
            //cerr << "Setting SDL video mode failed: " << SDL_GetError() << endl;
            return;
        }
        w = m_screen->w;
        h = m_screen->h;
#endif
        //cerr << "DEBUG: new size " << m_screen->w << " " << m_screen->h << endl;
        //cerr << "DEBUG: before scale " << m_scalex << " " << m_scaley << endl;
        m_scalex = (float)m_width  / (float)w;
        m_scaley = (float)m_height / (float)h;
        //cerr << "DEBUG: after scale " << m_scalex << " " << m_scaley << endl;
        ResetCaption();
        Refresh();
    }

    Display::~Display()
    {
        m_singleton = NULL;
#ifdef USE_SDL
        if (m_enabled)
            SDL_Quit();
#endif
    }

    void Display::DumpFrameBuffer(unsigned key, int stream, bool gen_ts)
    {
        ostream * os;
        bool free_os = false;
        if (stream == 0)
        {
            string fname = "gfx." + std::to_string(key);
            if (gen_ts)
            {
                fname += '.' + std::to_string(GetKernel()->GetCycleNo());
            }
            fname += ".ppm";
            os = new ofstream(fname.c_str(), ios_base::out | ios_base::trunc);
            free_os = true;
        }
        else
        {
            os = &((stream == 2) ? cerr : cout);
        }

        Draw();

        *os << "P3" << endl
            << dec
            << "#key: " << key << endl
            << "#" << endl
            << m_width << ' ' << m_height << ' ' << 255 << endl;
        for (unsigned y = 0; y < m_height; ++y)
        {
            for (unsigned x = 0; x < m_width; ++x)
            {
                uint32_t d = ((uint32_t*)(void*)&m_framebuffer[0])[y * m_width + x];
                *os << ((d >> 16) & 0xff) << ' '
                    << ((d >>  8) & 0xff) << ' '
                    << ((d >>  0) & 0xff) << ' ';
            }
            *os << endl;
        }


        if (free_os)
            delete os;
    }

    void Display::Draw()
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
            // cerr << "at offset " << i << " command " << hex << v32[i] << dec << endl;
            auto command = v32[i] >> 8;
            if (command == 0)
                break;
            else if (command == 3)
                continue;
            else if (command == 1) {
                if (v32[i+2] >= vsz32)
                    DebugIOWrite("Invalid palette offset: %u", (unsigned)v32[i+2]);
                else if (v32[i+2] + v32[i+1] > vsz32)
                    DebugIOWrite("Palette at 4x%u, size 4x%u bytes lies beyond video memory",
                                 (unsigned)v32[i+2], (unsigned)v32[i+1]);
                else {
                    palette_ncolors = v32[i+1];
                    palette_offset = v32[i+2];
                    //cerr << "New palette " << v32[i+1] << " data " << hex << v32[i+2] << dec << endl;
                    //for (unsigned w = 0; w < palette_ncolors; ++w)
                    //    cerr << "idx " << w << " color " << hex << v32[palette_offset + w] << dec << endl;
                }
            }
            else if (command == 2) {
                auto indexed = v32[i+1] >> 16;
                auto bpp = v32[i+1] & 0xffff;
                auto src_start = 4 * v32[i+2];
                auto src_pitch = v32[i+3];
                auto src_w = v32[i+4] >> 16;
                auto src_h = v32[i+4] & 0xffff;
                int32_t dst_x = v32[i+5] >> 16;
                int32_t dst_y = v32[i+5] & 0xffff;
                if (dst_x & 0x8000) dst_x = - (int32_t)((~dst_x + 1) & 0xffff);
                if (dst_y & 0x8000) dst_y = - (int32_t)((~dst_y + 1) & 0xffff);
                auto dst_w = v32[i+6] >> 16;
                auto dst_h = v32[i+6] & 0xffff;

                /*
                cerr << "Blit indexed = " << indexed
                     << " bpp = " << bpp
                     << " src_start = " << hex << src_start << dec
                     << " src_pitch = " << src_pitch
                     << " src_w = " << src_w
                     << " src_h = " << src_h
                     << " dst_x = " << dst_x
                     << " dst_y = " << dst_y
                     << " dst_w = " << dst_w
                     << " dst_h = " << dst_h
                     << endl;
                */

                auto size_bytes = (bpp * src_pitch / 8) * src_h;

                if (src_start + size_bytes > vsz)
                    DebugIOWrite("Frame data at %u, size %lu bytes lies beyond video memory (%zu)",
                                 (unsigned)src_start, (unsigned long)size_bytes, (size_t)vsz);
                else {
                    uint32_t* dst_base = &m_framebuffer[0];
                    unsigned dst_pitch = m_width;
                    // the frame may have a different size from the output region. So
                    // we need to scale.
                    double scale_x = (double)src_w / (double)dst_w;
                    double scale_y = (double)src_h / (double)dst_h;

                    // the actual output pane may be smaller than what the command
                    // request if (part of) the frame lies outside of the screen. So
                    // we need to reframe.
                    unsigned src_x = 0, src_y = 0;
                    if (dst_x < 0) {
                        src_x = -dst_x * scale_x;
                        dst_w += dst_x;
                        dst_x = 0;
                    }
                    if (dst_y < 0) {
                        src_y = -dst_y * scale_y;
                        dst_h += dst_y;
                        dst_y = 0;
                    }
                    if (dst_x + dst_w > m_width)
                        dst_w = m_width - dst_x;
                    if (dst_y + dst_h > m_height)
                        dst_h = m_height - dst_y;

                    if (bpp == 32 && !indexed)
                    {
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
                                //cerr << "pix " << dx << " " << dy << "  " << sx << " " << sy << " " << hex <<  dst[dx] << dec << endl;
                            }
                        }
                    }
                    else if (bpp == 24 && !indexed)
                    {
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
                        static const float Rf = 0xff / (float)0xf8;
                        static const float Gf = 0xff / (float)0xfc;
                        static const float Bf = Rf;
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
                                //cerr << "pix " << dx << " " << dy << "  " << sx << " " << sy << " " << hex << color << " " << dst[dx] << dec << endl;
                            }
                        }
                    }
                    else if (bpp == 16 && indexed && palette_offset != 0 && palette_ncolors >= 65536)
                    {
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
                        static const float Rf = 0xff / (float)0xe0;
                        static const float Gf = Rf;
                        static const float Bf = 0xff / (float)0xc0;
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
                                //cerr << "pix " << dx << " " << dy << "  " << sx << " " << sy << " " << index << endl;
                                dst[dx] = v32[palette_offset + index];
                            }
                        }
                    }
                    else if (bpp == 1 && indexed && palette_offset != 0 && palette_ncolors >= 2)
                    {
                        const uint8_t* src_base = (const uint8_t*)(const void*)&m_video_memory[src_start];
                        for (unsigned dy = 0; dy < dst_h; ++dy)
                        {
                            unsigned sy = dy * scale_y;
                            const uint8_t* __restrict__ src = src_base + (src_y + sy) * src_pitch/8 + src_x/8;
                            uint32_t* __restrict__ dst = dst_base + (dst_y + dy) * dst_pitch + dst_x;
                            for (unsigned dx = 0; dx < dst_w; ++dx)
                            {
                                unsigned sx = dx * scale_x;
                                uint8_t index = (src[sx/8] >> (sx%8)) & 1;
                                //cerr << "pix "
                                //     << dy << " " << dx << " "
                                //     << sy << " " << sx << " - "
                                //     << (int)index << " " << hex << palette_data[index] << dec << endl;
                                dst[dx] = v32[palette_offset + index];
                            }
                        }
                    }
                    else
                        DebugIOWrite("Unsupported pixel mode %x", (unsigned)v32[i+1]);
                }

            }
            else
                DebugIOWrite("Unsupported command %x", (unsigned)v32[i]);
        }

    }

    void Display::Refresh()
    {
        if (m_screen == NULL || !m_data_updated)
            return;
        m_data_updated = false;

#ifdef USE_SDL
        if (m_width == 0 || m_height == 0)
        {
            // No source to copy, just clear the surface
            SDL_FillRect(m_screen, NULL, 0);
            return ;
        }

        Draw();
        if (SDL_MUSTLOCK(m_screen))
            if (SDL_LockSurface(m_screen) < 0)
                return;
        assert(m_screen->format->BytesPerPixel == 4);

        // Copy the buffer into the video surface
        unsigned dx, dy;
        float scaley = this->m_scaley,
            scalex = this->m_scalex;
        unsigned width = this->m_width;
        unsigned screen_h = m_screen->h,
            screen_w = m_screen->w;
        unsigned screen_pitch = m_screen->pitch;
        char* pixels = (char*)m_screen->pixels;
        Uint8 Rshift = m_screen->format->Rshift,
            Gshift = m_screen->format->Gshift,
            Bshift = m_screen->format->Bshift;

        const uint32_t * __restrict__ src = (const uint32_t*)&m_framebuffer[0];
        for (dy = 0; dy < screen_h; ++dy)
        {
            Uint32 * __restrict__ dest = (Uint32*)(pixels + dy * screen_pitch);
            unsigned int    sy   = dy * scaley;

            for (dx = 0; dx < screen_w; ++dx)
            {
                unsigned int sx  = dx * scalex;
                Uint32 color = src[sy * width + sx];
                dest[dx] = (((color & 0xff0000) >> 16) << Rshift)
                    | (((color & 0x00ff00) >> 8) << Gshift)
                    | (((color & 0x0000ff)     ) << Bshift);
            }
        }

        if (SDL_MUSTLOCK(m_screen))
            SDL_UnlockSurface(m_screen);
        SDL_Flip(m_screen);
#endif
    }

    void Display::ResetCaption() const
    {
#ifdef USE_SDL
        auto caption = "MGSim display: "
            + to_string(m_width) + "x" + to_string(m_height)
            + ", " + to_string(m_refreshDelay) + " kernel cycles / frame";
        SDL_WM_SetCaption(caption.c_str(), NULL);
#endif
    }

#ifdef USE_SDL
    static unsigned currentDelayScale(unsigned x)
    {
        for (unsigned i = 10000000; i > 0; i /= 10)
            if (x > i) return i;
        return 1;
    }
#endif

    void Display::CheckEvents()
    {
        if (!m_enabled)
            return ;

#ifdef USE_SDL
        bool do_resize = false;
        bool do_close = false;
        unsigned nh = 0, nw = 0;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                do_close = true;
                break;

            case SDL_KEYUP:
                switch (event.key.keysym.sym)
                {
                case SDLK_ESCAPE:
                    do_close = true;
                    break;
                case SDLK_PAGEDOWN:
                    m_scalex /= 2.0; m_scaley /= 2.0; do_resize = true;
                    break;
                case SDLK_PAGEUP:
                    m_scalex *= 2.0; m_scaley *= 2.0; do_resize = true;
                    break;
                case SDLK_END:
                    m_scalex *= .9; m_scaley *= .9; do_resize = true;
                    break;
                case SDLK_HOME:
                    m_scalex *= 1.1; m_scaley *= 1.1; do_resize = true;
                    break;
                case SDLK_TAB:
                    m_scalex = m_scaley; do_resize = true;
                    break;
                case SDLK_DOWN:
                    m_refreshDelay += currentDelayScale(m_refreshDelay);
                    ResetCaption();
                    break;
                case SDLK_UP:
                    if (m_refreshDelay)
                        m_refreshDelay -= currentDelayScale(m_refreshDelay);
                    ResetCaption();
                    break;
                case SDLK_r:
                    m_refreshDelay = m_refreshDelay_orig;
                    m_scalex = m_scalex_orig;
                    m_scaley = m_scaley_orig;
                    do_resize = true;
                    break;
                default:
                    // do nothing (yet)
                    break;
                }
                if (do_resize)
                {
                    nw = m_width / m_scalex;
                    nh = m_height / m_scaley;
                }
                break;

            case SDL_VIDEORESIZE:
                do_resize = true;
                nw = event.resize.w;
                nh = event.resize.h;
                break;
            }
        }

        if (do_close)
        {
            // cerr << "Graphics output closed by user." << endl;
            m_enabled = false;
            m_screen  = NULL;
            SDL_Quit();
        }
        if (do_resize)
        {
            ResizeScreen(nw, nh);
            m_data_updated = true;
        }

        Refresh();
#endif
    }


    void Display::Resize(unsigned int w, unsigned int h, bool erase)
    {
        m_width  = w;
        m_height = h;

        m_framebuffer.resize(w * h);

        if (erase)
            memset(&m_framebuffer[0], 0, w * h * sizeof(m_framebuffer[0]));
        m_data_updated = true;

#ifdef USE_SDL
        // Try to resize the screen as well
        ResizeScreen(m_width / m_scalex, m_height / m_scaley);
        Refresh();
#endif
    }


}
