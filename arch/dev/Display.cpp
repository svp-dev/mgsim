#include <arch/dev/Display.h>
#include <arch/dev/sdl_wrappers.h>
#include <sim/sampling.h>

#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cstdlib>

using namespace std;

namespace Simulator
{
    struct SDLContext
    {
        SDL_Window* window;
        uint32_t windowID;
        SDL_Renderer* renderer;
        SDL_Texture* texture;
    };

    Display::FrameBufferInterface
    ::FrameBufferInterface(const string& name, Display& parent,
                           IIOBus& iobus, IODeviceID devid)
        : Object(name, parent),
          m_iobus(iobus),
          m_devid(devid)
    {
        iobus.RegisterClient(devid, *this);
    }

    bool Display::FrameBufferInterface::
    OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
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

    bool Display::FrameBufferInterface::
    OnWriteRequestReceived(IODeviceID /*from*/,
                           MemAddr address, const IOData& iodata)
    {
        Display& disp = GetDisplay();
        if (address >= disp.m_video_memory.size() || address + iodata.size >= disp.m_video_memory.size())
        {
            throw exceptf<>(*this, "FB write out of bounds: %#016llx (%u)", (unsigned long long)address, (unsigned)iodata.size);
        }

        COMMIT {
            memcpy(&disp.m_video_memory[address], iodata.data, iodata.size);
            disp.m_video_memory_updated = true;
        }

        DebugIOWrite("FB write: %#016llx/%u", (unsigned long long)address, (unsigned)iodata.size);

        return true;
    }

    void Display::FrameBufferInterface::
    GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "GfxFB", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }
    }

    const string& Display::FrameBufferInterface::
    GetIODeviceName() const
    {
        return GetName();
    }

    Display::ControlInterface::
    ControlInterface(const string& name, Display& parent,
                     IIOBus& iobus, IODeviceID devid)
        : Object(name, parent),
          m_iobus(iobus),
          m_control(2, 0),
          m_devid(devid),
          InitStateVariable(key, 0)
    {
        RegisterStateVariable(m_control, "control");
        iobus.RegisterClient(devid, *this);
    }

    void Display::ControlInterface::
    GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "GfxCtl", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }
    }

    bool Display::ControlInterface::
    OnWriteRequestReceived(IODeviceID /*from*/, MemAddr address, const IOData& iodata)
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
                disp.ResizeLogicalScreen(act_w, act_h, !!value);
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
                disp.m_video_memory_updated = true;
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
                disp.PrepareLogicalScreen();
                disp.DumpLogicalScreen(m_key, value & 0xff, (value >> 8) & 1);
            }
            DebugIOWrite("Dumping framebuffer");
        }
        return true;
    }

    bool Display::ControlInterface::
    OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        // the display uses 32-bit status words
        // word 0: read: display enabled; write: commit mode from words 1/2/3
        // word 1: logical screen width
        // word 2: logical screen height
        // word 3: command offset
        // word 4: (unused)
        // word 5: next dump key
        // word 6: max supported screen width
        // word 7: max supported screen height
        // word 8: (unused)
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
        case 0: value = disp.m_sdl_enabled; break;
        case 1: value = disp.m_logical_width; break;
        case 2: value = disp.m_logical_height; break;
        case 3: value = disp.m_command_offset; break;
        case 4: value = 0; break;
        case 5: value = m_key; break;
        case 6: value = disp.m_max_screen_w; break;
        case 7: value = disp.m_max_screen_h; break;
        case 8: value = 0; break;
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

    const string& Display::ControlInterface::
    GetIODeviceName() const
    {
        return GetName();
    }


    Display::Display(const string& name, Object& parent, IIOBus& iobus, IODeviceID ctldevid, IODeviceID fbdevid)
        : Object(name, parent),
          m_ctlinterface("ctl", *this, iobus, ctldevid),
          m_fbinterface("fb", *this, iobus, fbdevid),
          m_max_screen_h(1024),
          m_max_screen_w(1280),
          m_video_memory_updated(false),
          m_logical_screen_resized(false),
          m_logical_screen_updated(false),
          m_window_resized(false),
          m_video_memory(GetConf("GfxFrameSize", size_t), 0),
          m_logical_screen_pixels(),
          m_sdl_enabled(false),
          m_sdl_context(new SDLContext),
          InitStateVariable(logical_width, 640),
          InitStateVariable(logical_height, 400),
          InitStateVariable(command_offset, 0),
          InitStateVariable(scalex, 1.0),
          InitStateVariable(scaley, 1.0)
    {
        RegisterStateVariable(m_video_memory, "video_memory");

        if (GetConf("GfxEnableSDLOutput", bool))
        {
            DisplayManager::CreateManagerIfNotExists(*GetKernel()->GetConfig());
            auto dm = DisplayManager::GetManager();
            if (dm == NULL || !dm->IsSDLInitialized())
                cerr << "# " << GetName() << ": unable to use SDL, output to screen disabled" << endl;
            else
            {
                m_sdl_enabled = true;
                dm->RegisterDisplay(this);
                dm->GetMaxWindowSize(m_max_screen_w, m_max_screen_h);
                cerr << "# " << GetName()
                     << ": Maximum supported output size: "
                     << m_max_screen_w << 'x' << m_max_screen_h << endl;
            }
        }

    }

    Display::~Display()
    {
        auto dm = DisplayManager::GetManager();
        if (dm != NULL)
            dm->UnregisterDisplay(this);
        CloseWindow();
        delete m_sdl_context;
        m_sdl_context = 0;
    }


    void Display::ResizeLogicalScreen(unsigned int w, unsigned int h, bool erase)
    {
        m_logical_width  = w;
        m_logical_height = h;

        m_logical_screen_pixels.resize(w * h);

        if (erase)
            memset(&m_logical_screen_pixels[0], 0, w * h * sizeof(m_logical_screen_pixels[0]));

        m_logical_screen_resized = true;
    }

    void Display::DumpLogicalScreen(unsigned key, int stream, bool gen_ts)
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

        *os << "P3" << endl
            << dec
            << "#key: " << key << endl
            << "#" << endl
            << m_logical_width << ' ' << m_logical_height << ' ' << 255 << endl;
        for (unsigned y = 0; y < m_logical_height; ++y)
        {
            for (unsigned x = 0; x < m_logical_width; ++x)
            {
                uint32_t d = ((uint32_t*)(void*)&m_logical_screen_pixels[0])[y * m_logical_width + x];
                *os << ((d >> 16) & 0xff) << ' '
                    << ((d >>  8) & 0xff) << ' '
                    << ((d >>  0) & 0xff) << ' ';
            }
            *os << endl;
        }


        if (free_os)
            delete os;
    }



    void Display::EqualizeWindowScale()
    {
        SetWindowScale(m_scalex, m_scalex, true);
    }

    void Display::SetWindowScale(double scalex, double scaley, bool set)
    {
        if (set)
        {
            m_scalex = scalex;
            m_scaley = scaley;
        }
        else
        {
            m_scalex *= scalex;
            m_scaley *= scaley;
        }
        SetWindowSize(m_logical_width / m_scalex, m_logical_height / m_scaley);
    }

    void Display::SetWindowSize(unsigned int w, unsigned int h)
    {
        if (!m_sdl_enabled)
            return ;

        float r = (float)h / (float)w;

        w = min(m_max_screen_w, w); h = w * r;
        h = min(m_max_screen_h, h); w = h / r;

        if (m_sdl_context->window == NULL)
        {
            m_sdl_context->texture = 0;
            m_sdl_context->renderer = 0;
            int res = SDL_CreateWindowAndRenderer(w, h, SDL_WINDOW_RESIZABLE,
                                                  &m_sdl_context->window, &m_sdl_context->renderer);
            if (res < 0)
                cerr << "# " << GetName() << ": error creating SDL window: " << SDL_GetError() << endl;
            else
                m_sdl_context->windowID = SDL_GetWindowID(m_sdl_context->window);
        }
        else
            SDL_SetWindowSize(m_sdl_context->window, w, h);

        if (m_sdl_context->window)
        {
            int ww, wh;
            SDL_GetWindowSize(m_sdl_context->window, &ww, &wh);
            w = (unsigned)ww;
            h = (unsigned)wh;
            ResetWindowCaption();
            m_window_resized = true;
        }

        m_scalex = (float)m_logical_width  / (float)w;
        m_scaley = (float)m_logical_height / (float)h;
    }

    void Display::Show()
    {
        bool need_redraw = false, need_reshow = false;
        if (m_logical_screen_resized)
        {
            m_logical_screen_resized = false;
            need_redraw = true;
            SetWindowSize(m_logical_width / m_scalex, m_logical_height / m_scaley);

            if (m_sdl_context->renderer)
            {
                if (m_sdl_context->texture)
                    SDL_DestroyTexture(m_sdl_context->texture);

                m_sdl_context->texture = SDL_CreateTexture(m_sdl_context->renderer,
                                                           SDL_PIXELFORMAT_RGB888,
                                                           SDL_TEXTUREACCESS_STREAMING,
                                                           m_logical_width, m_logical_height);
                if (!m_sdl_context->texture)
                    cerr << "# " << GetName() << ": error creating SDL texture: " << SDL_GetError() << endl;
            }
        }

        if (m_video_memory_updated)
        {
            m_video_memory_updated = false;
            need_redraw = true;
        }

        if (need_redraw)
            PrepareLogicalScreen();

        if (!m_sdl_context->window || !(SDL_GetWindowFlags(m_sdl_context->window) & SDL_WINDOW_SHOWN))
            // nothing visible, stop here
            return;

        if (m_logical_screen_updated)
        {
            m_logical_screen_updated = false;
            need_reshow = true;
            if (m_sdl_context->texture)
                SDL_UpdateTexture(m_sdl_context->texture, 0, &m_logical_screen_pixels[0], m_logical_width*4);
        }

        if (m_window_resized)
        {
            m_window_resized = false;
            need_reshow = true;
        }

        if (need_reshow && m_sdl_enabled && m_sdl_context->renderer && m_sdl_context->texture)
        {
            SDL_RenderSetScale(m_sdl_context->renderer, m_scalex, m_scaley);
            //SDL_SetLogicalSize(m_sdl_context->renderer, m_logical_width, m_logical_height);
            //SDL_RenderClear(m_sdl_context->renderer);
            SDL_RenderCopy(m_sdl_context->renderer, m_sdl_context->texture, 0, 0);
            SDL_RenderPresent(m_sdl_context->renderer);
        }
    }

    void Display::ResetWindowCaption() const
    {
        if (m_sdl_context->window)
        {
            auto dm = DisplayManager::GetManager();
            assert(dm != NULL);

            auto caption = "MGSim display: "
                + to_string(m_logical_width) + "x" + to_string(m_logical_height) + ", "
                + to_string(dm->GetRefreshDelay()) + " kernel cycles / frame";
            SDL_SetWindowTitle(m_sdl_context->window, caption.c_str());
        }
    }

    void Display::ResetDisplay()
    {
        CloseWindow();
        m_logical_screen_resized = true;
        m_video_memory_updated = true;
        Show();
    }

    void Display::CloseWindow()
    {
        if (m_sdl_context->window)
        {
            if (m_sdl_context->texture)
                SDL_DestroyTexture(m_sdl_context->texture);
            if (m_sdl_context->renderer)
                SDL_DestroyRenderer(m_sdl_context->renderer);
            SDL_DestroyWindow(m_sdl_context->window);
            m_sdl_context->window = 0;
            m_sdl_context->renderer = 0;
            m_sdl_context->texture = 0;
        }
    }

    static unsigned currentDelayScale(unsigned x)
    {
        for (unsigned i = 10000000; i > 0; i /= 10)
            if (x > i) return i;
        return 1;
    }

    void DisplayManager::RegisterDisplay(Display *disp)
    {
        for (auto p : m_displays)
            if (p == disp)
                return;
        m_displays.push_back(disp);
    }

    void DisplayManager::UnregisterDisplay(Display *disp)
    {
        remove(m_displays.begin(), m_displays.end(), disp);
    }

    void DisplayManager::GetMaxWindowSize(unsigned& w, unsigned& h)
    {
        if (!m_sdl_initialized)
            return;

        w = h = 0;
        int numDisplays = SDL_GetNumVideoDisplays();
        for (int i = 0; i < numDisplays; ++i)
        {
            SDL_Rect r;
            if (SDL_GetDisplayBounds(i, &r) == 0)
            {
                if (r.w > (int)w || r.h > (int)h)
                {  w = r.w; h = r.h; }
            }
        }
    }

    void DisplayManager::ResetDisplays() const
    {
        for (auto d : m_displays)
            d->ResetDisplay();
    }

    DisplayManager* DisplayManager::g_singleton = 0;

    void DisplayManager::CreateManagerIfNotExists(Config& cfg)
    {
        if (g_singleton == 0)
            g_singleton = new DisplayManager(cfg.getValue<unsigned>("SDLRefreshDelay"));
    }

    DisplayManager::DisplayManager(unsigned refreshDelay)
        : m_sdl_initialized(false),
          m_refreshDelay_orig(refreshDelay),
          m_refreshDelay(refreshDelay),
          m_lastUpdate(0),
          m_displays()
    {
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            cerr << "# unable to set up SDL: " << SDL_GetError() << endl;
            return;
        }
        m_sdl_initialized = true;
        if (SDL_HasQuit)
            atexit(SDL_Quit);
    }

    void DisplayManager::CheckEvents()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            uint32_t et = SDL_GetEventType(event);
            Display *selected = 0;
            switch (et)
            {
            case SDL_WINDOWEVENT:
                for (auto d : m_displays)
                    if (d->m_sdl_context && d->m_sdl_context->windowID == SDL_GetEventWinWinID(event))
                    { selected = d; break; }
                break;
            case SDL_KEYUP:
                for (auto d : m_displays)
                    if (d->m_sdl_context && d->m_sdl_context->windowID == SDL_GetEventKeyWinID(event))
                    { selected = d; break; }
                break;
            }
            if (selected == 0)
                // stray event, ignore
                continue;

            switch(et)
            {
            case SDL_KEYUP:
                switch (SDL_GetEventKeySym(event))
                {
                case SDLK_ESCAPE:
                    selected->CloseWindow();
                    break;
                case SDLK_PAGEDOWN:
                    selected->SetWindowScale(0.5, 0.5, false);
                    break;
                case SDLK_PAGEUP:
                    selected->SetWindowScale(2.0, 2.0, false);
                    break;
                case SDLK_END:
                    selected->SetWindowScale(0.9, 0.9, false);
                    break;
                case SDLK_HOME:
                    selected->SetWindowScale(1.1, 1.1, false);
                    break;
                case SDLK_TAB:
                    selected->EqualizeWindowScale();
                    break;
                case SDLK_SPACE:
                    selected->SetWindowScale(1.0, 1.0, true);
                    break;
                case SDLK_DOWN:
                    m_refreshDelay += currentDelayScale(m_refreshDelay);
                    for (auto d : m_displays)
                        d->ResetWindowCaption();
                    break;
                case SDLK_UP:
                    if (m_refreshDelay)
                        m_refreshDelay -= currentDelayScale(m_refreshDelay);
                    for (auto d : m_displays)
                        d->ResetWindowCaption();
                    break;
                case SDLK_r:
                    m_refreshDelay = m_refreshDelay_orig;
                    for (auto d : m_displays)
                        d->SetWindowScale(1.0, 1.0, true);
                    break;
                default:
                    // do nothing (yet)
                    break;
                }
                break;

            case SDL_WINDOWEVENT:
                switch (SDL_GetEventWinType(event))
                {
                case SDL_WINDOWEVENT_CLOSE:
                    selected->CloseWindow();
                    break;
                case SDL_WINDOWEVENT_RESIZED:
                    selected->SetWindowSize(SDL_GetEventWinSizeW(event),
                                            SDL_GetEventWinSizeH(event));
                    break;
                }
                break;
            }
        }

        for (auto d : m_displays)
            d->Show();
    }



}
