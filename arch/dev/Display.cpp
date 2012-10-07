#include "Display.h"

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
        : Object(name, parent, iobus.GetClock()),
          m_devid(devid),
          m_iobus(iobus)
    {
        iobus.RegisterClient(devid, *this);
    }

    bool Display::FrameBufferInterface::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        Display& disp = GetDisplay();
        if (address + size > disp.m_framebuffer.size())
        {
            throw exceptf<SimulationException>(*this, "FB read out of bounds: %#016llx (%u)", (unsigned long long)address, (unsigned)size);
        }

        IOData iodata;
        COMMIT {
            memcpy(iodata.data, &disp.m_framebuffer[address], size);
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
        if (address >= disp.m_framebuffer.size() || address + iodata.size >= disp.m_framebuffer.size())
        {
            throw exceptf<SimulationException>(*this, "FB write out of bounds: %#016llx (%u)", (unsigned long long)address, (unsigned)iodata.size);
        }

        COMMIT {
            memcpy(&disp.m_framebuffer[address], iodata.data, iodata.size);
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
    
    Display::ControlInterface::ControlInterface(const string& name, Display& parent, IIOBus& iobus, IODeviceID devid)
        : Object(name, parent, iobus.GetClock()),
          m_devid(devid),
          m_iobus(iobus),
          m_control(3, 0)
    {
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
            throw exceptf<SimulationException>(*this, "Invalid unaligned GfxCtl write: %#016llx (%u)", (unsigned long long)address, (unsigned)iodata.size); 
        }
        if ((word > 5 && word < 0x100) || word > 0x1ff)
        {
            throw exceptf<SimulationException>(*this, "Invalid write to GfxCtl word: %u", word);
        }
        
        uint32_t value = UnserializeRegister(RT_INTEGER, iodata.data, iodata.size);
        Display& disp = GetDisplay();

        DebugIOWrite("Ctl write to word %u: %#016lx", word, (unsigned long)value);

        if (word == 0)
        {
            uint32_t req_w = m_control[0], req_h = m_control[1], req_bpp = m_control[2] & 0xffff;
            size_t act_w, act_h;
            bool req_indexed = m_control[2] >> 16;
            
            if (req_bpp != 32 && req_bpp != 24 && req_bpp != 16 && req_bpp != 8)
            { DebugIOWrite("unsupported bits per pixel: %u", (unsigned)req_bpp); return true; }
            if (req_indexed && req_bpp > 8)
            { DebugIOWrite("unsupported use of indexed mode with bpp > 8: %u", (unsigned)req_bpp); return true; }

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

            if (act_w * act_h * req_bpp / 8 > disp.m_framebuffer.size())
            { DebugIOWrite("resolution too large for framebuffer: %ux%ux%u", (unsigned)act_w, (unsigned)act_h, (unsigned)req_bpp); return true; }
            
            COMMIT {
                disp.m_indexed = req_indexed;
                disp.m_bpp = req_bpp;
                disp.Resize(act_w, act_h, !!value);
            }
            DebugIOWrite("Setting resolution to %ux%ux%u", (unsigned)act_w, (unsigned)act_h, (unsigned)req_bpp);
        }
        else if (word <= 3)
        {
            COMMIT {
                m_control[word - 1] = value;
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
        else // word > 0x100
        {
            COMMIT {
                disp.m_palette[word - 0x100] = value;
            }
        }
        return true;
    }

    bool Display::ControlInterface::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        // the display uses 32-bit status words
        // word 0: read: display enabled; write: commit mode from words 1/2/3
        // word 1: pixel width
        // word 2: pixel height
        // word 3: low 16 = current bpp; high 16 = indexed (zero: not indexed; 1: indexed)
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
            throw exceptf<SimulationException>(*this, "Invalid unaligned GfxCtl read: %#016llx (%u)", (unsigned long long)address, (unsigned)size); 
        }
        if ((word > 9 && word < 0x100) || word > 0x1ff)
        {
            throw exceptf<SimulationException>(*this, "Read from invalid GfxCtl word: %u", word);
        }
        
        Display& disp = GetDisplay();

        if (word <= 9)
        {
            switch(word)
            {
            case 0: value = disp.m_enabled; break;
            case 1: value = disp.m_width; break;
            case 2: value = disp.m_height; break;
            case 3: value = disp.m_bpp | ((int)disp.m_indexed << 16); break;
            case 4: value = 0; break;
            case 5: value = m_key; break;
            case 6: value = disp.m_max_screen_w; break;
            case 7: value = disp.m_max_screen_h; break;
            case 8: value = disp.m_refreshDelay; break;
            case 9: value = disp.m_fbinterface.m_devid; break;
            }
        }
        else
        {
            // palette access;
            word -= 0x100;
            value = disp.m_palette[word];
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


    Display::Display(const string& name, Object& parent, IIOBus& iobus, IODeviceID ctldevid, IODeviceID fbdevid, Config& config)
        : Object(name, parent),
          m_framebuffer(config.getValue<size_t>(*this, "GfxFrameSize"), 0),
          m_palette(256, 0),
          m_indexed(false),
          m_bpp(8),
          m_width(640), m_height(400),
          m_scalex_orig(1.0f / max(1U, config.getValue<unsigned int>("SDLHorizScale"))),
          m_scalex(m_scalex_orig),
          m_scaley_orig(1.0f / max(1U, config.getValue<unsigned int>("SDLVertScale"))),
          m_scaley(m_scaley_orig),
          m_refreshDelay_orig(config.getValue<unsigned int>("SDLRefreshDelay")),
          m_refreshDelay(m_refreshDelay_orig),
          m_lastUpdate(0),
          m_screen(NULL),
          m_max_screen_h(1024), m_max_screen_w(1280),
          m_enabled(false),
          m_ctlinterface("ctl", *this, iobus, ctldevid),
          m_fbinterface("fb", *this, iobus, fbdevid)
    {
        if (m_framebuffer.size() < 640*400)
            throw exceptf<InvalidArgumentException>(*this, "FrameBufferSize not set or too small for minimum resolution 640x400: %zu", m_framebuffer.size());

        RegisterSampleVariableInObject(m_width,  SVC_LEVEL);
        RegisterSampleVariableInObject(m_height, SVC_LEVEL);
        RegisterSampleVariableInObject(m_scalex, SVC_LEVEL);
        RegisterSampleVariableInObject(m_scaley, SVC_LEVEL);
        RegisterSampleVariableInObject(m_refreshDelay, SVC_LEVEL);
        RegisterSampleVariableInObject(m_lastUpdate, SVC_CUMULATIVE);

#ifdef USE_SDL
        if (config.getValue<bool>(*this, "GfxEnableSDLOutput"))
        {
            if (m_singleton != NULL)
                throw InvalidArgumentException(*this, "Only one Display device can output to SDL.");
            m_singleton = this;

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
        }
#endif
        

    }


    void Display::ResizeScreen(unsigned int w, unsigned int h)
    {
#ifdef USE_SDL
        if (!m_enabled)
            return ;

        float r = (float)h / (float)w;

        // cerr << "DEBUG: fb size " << m_width << " " << m_height << endl;
        // cerr << "DEBUG: resizescreen " << w << " " << h << endl;
        w = min(m_max_screen_w, w); h = w * r; 
        h = min(m_max_screen_h, h); w = h / r;
        // cerr << "DEBUG: after adjust " << w << " " << h << endl;

//    m_screen = SDL_SetVideoMode(w, h, 32, SDL_SWSURFACE | SDL_RESIZABLE);
        
        if ((NULL == (m_screen = SDL_SetVideoMode(w, h, 32, SDL_SWSURFACE | SDL_RESIZABLE))) &&
            (NULL == (m_screen = SDL_SetVideoMode(640, 400, 32, SDL_SWSURFACE | SDL_RESIZABLE))))
        {
            cerr << "Setting SDL video mode failed: " << SDL_GetError() << endl;
        } 
        else 
        {
            // cerr << "DEBUG: new size " << m_screen->w << " " << m_screen->h << endl;
            // cerr << "DEBUG: before scale " << m_scalex << " " << m_scaley << endl;
            m_scalex = (float)m_width  / (float)m_screen->w;
            m_scaley = (float)m_height / (float)m_screen->h;
            // cerr << "DEBUG: after scale " << m_scalex << " " << m_scaley << endl;
            ResetCaption();
            Refresh();
        }
#endif
    }

    Display::~Display()
    {
        m_singleton = NULL;
#ifdef USE_SDL
        if (m_enabled)
            SDL_Quit();
#endif
    }

    void Display::DumpFrameBuffer(unsigned key, int stream, bool gen_ts) const
    {
        if (m_bpp != 32)
        {
            throw exceptf<SimulationException>(*this, "Unable to dump the framebuffer when bpp != 32 (currently %u)", m_bpp);
        }

        ostream * os;
        bool free_os = false;
        if (stream == 0)
        {
            ostringstream fname;
            fname << "gfx." << key;
            if (gen_ts)
            {
                fname << '.' << GetKernel()->GetCycleNo();
            }
            fname << ".ppm";
            os = new ofstream(fname.str().c_str(), ios_base::out | ios_base::trunc);
            free_os = true;
        }
        else
        {
            os = &((stream == 2) ? cerr : cout);
        }

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

    void Display::Refresh() const
    {
#ifdef USE_SDL
        if (m_screen != NULL)
        {
            if (m_width == 0 || m_height == 0)
            {
                // No source to copy, just clear the surface
                SDL_FillRect(m_screen, NULL, 0);
            }
            else 
            {
                if (SDL_MUSTLOCK(m_screen))
                    if (SDL_LockSurface(m_screen) < 0)
                        return;
                assert(m_screen->format->BytesPerPixel == 4);

                // Copy the buffer into the video surface
                unsigned dx, dy;
                float m_scaley = this->m_scaley, 
                    m_scalex = this->m_scalex;
                unsigned m_width = this->m_width;
                unsigned m_screen_h = m_screen->h, 
                    m_screen_w = m_screen->w;
                unsigned m_screen_pitch = m_screen->pitch;
                char* pixels = (char*)m_screen->pixels;
                Uint8 Rshift = m_screen->format->Rshift,
                    Gshift = m_screen->format->Gshift,
                    Bshift = m_screen->format->Bshift;

                if (m_indexed)
                {
                    assert(m_bpp == 8);
                    /*** 1 byte per pixel, palette lookup ***/
                    const uint8_t *src = &m_framebuffer[0];
                    for (dy = 0; dy < m_screen_h; ++dy) 
                    {
                        Uint32*         dest = (Uint32*)(pixels + dy * m_screen_pitch);
                        unsigned int    sy   = dy * m_scaley;
                        for (dx = 0; dx < m_screen_w; ++dx)
                        {
                            unsigned int sx  = dx * m_scalex;
                            Uint32 color = m_palette[src[sy * m_width + sx]];
                            dest[dx] = (((color & 0xff0000) >> 16) << Rshift) 
                                | (((color & 0x00ff00) >> 8) << Gshift)
                                | (((color & 0x0000ff)     ) << Bshift);
                        }
                    }                
                }
                else
                    switch(m_bpp)
                    {
                    case 8:
                    {
                        /*** 1 bytes per pixel, 3-3-2 RGB ***/
                        const uint8_t *src = &m_framebuffer[0];
                        static const float Rf = 0xff / (float)0xe0;
                        static const float Gf = Rf;
                        static const float Bf = 0xff / (float)0xc0;
                        for (dy = 0; dy < m_screen_h; ++dy) 
                        {
                            Uint32*         dest = (Uint32*)(pixels + dy * m_screen_pitch);
                            unsigned int    sy   = dy * m_scaley;
                        
                            for (dx = 0; dx < m_screen_w; ++dx)
                            {
                                unsigned int sx  = dx * m_scalex;
                                Uint8 color = src[sy * m_width + sx];
                                dest[dx] = 
                                    (((uint32_t)((color & 0xe0) * Rf)) << Rshift) 
                                    | (((uint32_t)(((color & 0x1c) << 3) * Gf)) << Gshift) 
                                    | (((uint32_t)(((color & 0x03) << 6) * Bf)) << Bshift);
                            }
                        }
                    }
                    break;
                    case 16:
                    {
                        /*** 2 bytes per pixel, 5-6-5 RGB ***/
                        const uint16_t *src = (const uint16_t*)(void*)&m_framebuffer[0];
                        static const float Rf = 0xff / (float)0xf8;
                        static const float Gf = 0xff / (float)0xfc;
                        static const float Bf = Rf;
                        for (dy = 0; dy < m_screen_h; ++dy) 
                        {
                            Uint32*         dest = (Uint32*)(pixels + dy * m_screen_pitch);
                            unsigned int    sy   = dy * m_scaley;
                        
                            for (dx = 0; dx < m_screen_w; ++dx)
                            {
                                unsigned int sx  = dx * m_scalex;
                                Uint16 color = src[sy * m_width + sx];
                                dest[dx] = 
                                    (((uint32_t)(((color & 0xf800) >> 8) * Rf)) << Rshift) 
                                    | (((uint32_t)(((color & 0x07e0) >> 3) * Gf)) << Gshift) 
                                    | (((uint32_t)(((color & 0x001f) << 3) * Bf)) << Bshift);
                            }
                        }
                    }
                    break;
                    case 24:
                    {
                        /*** 3 bytes per pixel, 8-8-8 RGB ***/
                        const uint8_t *src = (const uint8_t*)(void*)&m_framebuffer[0];
                        for (dy = 0; dy < m_screen_h; ++dy) 
                        {
                            Uint32*         dest = (Uint32*)(pixels + dy * m_screen_pitch);
                            unsigned int    sy   = dy * m_scaley;
                        
                            for (dx = 0; dx < m_screen_w; ++dx)
                            {
                                unsigned int sx  = dx * m_scalex;
                                const Uint8 * base = &src[sy * m_width * 3 + sx * 3];
                                dest[dx] = (base[0] << Rshift) 
                                    | (base[1] << Gshift)
                                    | (base[2] << Bshift);
                            }
                        }
                    }
                    break;
                    case 32:
                    {
                        /*** 4 bytes per pixel, 8-8-8 RGB ***/
                        const uint32_t *src = (const uint32_t*)(void*)&m_framebuffer[0];
                        for (dy = 0; dy < m_screen_h; ++dy) 
                        {
                            Uint32*         dest = (Uint32*)(pixels + dy * m_screen_pitch);
                            unsigned int    sy   = dy * m_scaley;
                        
                            for (dx = 0; dx < m_screen_w; ++dx)
                            {
                                unsigned int sx  = dx * m_scalex;
                                Uint32 color = src[sy * m_width + sx];
                                dest[dx] = (((color & 0xff0000) >> 16) << Rshift) 
                                    | (((color & 0x00ff00) >> 8) << Gshift)
                                    | (((color & 0x0000ff)     ) << Bshift);
                            }
                        }
                    }
                    break;
                    default:
                        /* no known bpp */
                        break;
                    }

                if (SDL_MUSTLOCK(m_screen))
                    SDL_UnlockSurface(m_screen);
                SDL_Flip(m_screen);
            }
        }
#endif
    }

    void Display::ResetCaption() const
    {
#ifdef USE_SDL
        stringstream caption;
        caption << "MGSim display: " 
                << m_width << "x" << m_height 
                << ", " << m_refreshDelay << " kernel cycles / frame";
        SDL_WM_SetCaption(caption.str().c_str(), NULL);
#endif
    }

    static unsigned currentDelayScale(unsigned x)
    {
        for (unsigned i = 10000000; i > 0; i /= 10)
            if (x > i) return i;
        return 1;
    }

    void Display::CheckEvents()
    {
#ifdef USE_SDL
        if (!m_enabled)
            return ;
        
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
            ResizeScreen(nw, nh);
        
        Refresh();
#endif
    }


    void Display::Resize(unsigned int w, unsigned int h, bool erase)
    {
        m_width  = w;
        m_height = h;

        if (erase)
            memset(&m_framebuffer[0], 0, w * h * m_bpp / 8);
    
#ifdef USE_SDL
        // Try to resize the screen as well
        ResizeScreen(m_width / m_scalex, m_height / m_scaley);
        Refresh();
#endif
    }


}
