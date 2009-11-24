#include "display.h"
#include "config.h"
#include <stdexcept>
#include <cassert>
#ifdef USE_SDL
#include <SDL.h>
#endif

Display::Display(const Config& config)
    : m_width(0), m_height(0),
      m_enabled(false),
      m_scalex_orig(1.0f / std::max(1U, config.getInteger<unsigned int>("GfxHorizScale", 2))),
      m_scalex(m_scalex_orig),
      m_scaley_orig(1.0f / std::max(1U, config.getInteger<unsigned int>("GfxVertScale",  2))),
      m_scaley(m_scaley_orig),
      m_refreshDelay_orig(config.getInteger<unsigned int>("GfxRefreshDelay", 30)),
      m_refreshDelay(m_refreshDelay_orig),
      m_screen(NULL),
      m_max_screen_h(768), m_max_screen_w(1024)
{
#ifdef USE_SDL
    if (config.getBoolean("GfxEnableOutput", false))
    {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "Unable to initialize SDL: " << SDL_GetError() << std::endl;
        } else {
            m_enabled = true;
        }
        const SDL_VideoInfo* vf = SDL_GetVideoInfo();
        if (vf) {
            m_max_screen_h = vf->current_h;
            m_max_screen_w = vf->current_w;
            std::cerr << "Maximum supported output size: " 
                      << m_max_screen_w << 'x' << m_max_screen_h << std::endl;
        }
    }
#endif
}

Display::~Display()
{
#ifdef USE_SDL
    if (m_enabled)
    {
        SDL_Quit();
    }
#endif
}

void Display::Resize(unsigned int w, unsigned int h)
{
    m_width  = w;
    m_height = h;
    m_framebuffer.resize(m_width * m_height);
    std::fill(m_framebuffer.begin(), m_framebuffer.end(), 0);
    
#ifdef USE_SDL
    // Try to resize the screen as well
    ResizeScreen(m_width / m_scalex, m_height / m_scaley);
    Refresh();
#endif
}

void Display::ResizeScreen(unsigned int w, unsigned int h)
{
#ifdef USE_SDL
    if (m_enabled)
    {
        float r = (float)h / (float)w;

        // std::cerr << "DEBUG: fb size " << m_width << " " << m_height << std::endl;
        // std::cerr << "DEBUG: resizescreen " << w << " " << h << std::endl;
        w = std::min(m_max_screen_w, w); h = w * r; 
        h = std::min(m_max_screen_h, h); w = h / r;
        // std::cerr << "DEBUG: after adjust " << w << " " << h << std::endl;

        m_screen = SDL_SetVideoMode(w, h, 32, SDL_SWSURFACE | SDL_RESIZABLE);
        
        if ((NULL == (m_screen = SDL_SetVideoMode(w, h, 32, SDL_SWSURFACE | SDL_RESIZABLE))) &&
            (NULL == (m_screen = SDL_SetVideoMode(640, 480, 32, SDL_SWSURFACE | SDL_RESIZABLE))))
        {
            std::cerr << "Setting SDL video mode failed: " << SDL_GetError() << std::endl;
        } 
        else 
        {
            // std::cerr << "DEBUG: new size " << m_screen->w << " " << m_screen->h << std::endl;
            // std::cerr << "DEBUG: before scale " << m_scalex << " " << m_scaley << std::endl;
            m_scalex = (float)m_width  / (float)m_screen->w;
            m_scaley = (float)m_height / (float)m_screen->h;
            // std::cerr << "DEBUG: after scale " << m_scalex << " " << m_scaley << std::endl;
            ResetCaption();
            Refresh();
        }
    }
#endif
}

void Display::ResetCaption()
{
#ifdef USE_SDL
    std::stringstream caption;
    caption << "Microgrid Simulator Display (" 
            << m_width << "x" << m_height 
            << "), " << m_refreshDelay << "cpf";
    SDL_WM_SetCaption(caption.str().c_str(), NULL);
#endif
}

void Display::Refresh()
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
            float m_scaley = this->m_scaley, m_scalex = this->m_scalex;
            unsigned m_width = this->m_width;
            unsigned m_screen_h = m_screen->h, m_screen_w = m_screen->w;
            unsigned m_screen_pitch = m_screen->pitch;
            char* pixels = (char*)m_screen->pixels;
            Uint8 Rshift = m_screen->format->Rshift,
                Gshift = m_screen->format->Gshift,
                Bshift = m_screen->format->Bshift;
            const uint32_t *src = &m_framebuffer[0];

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
                
            if (SDL_MUSTLOCK(m_screen))
                SDL_UnlockSurface(m_screen);
            SDL_Flip(m_screen);
        }
    }
#endif
}

void Display::CheckEvents_()
{
#ifdef USE_SDL
    if (m_enabled)
    {
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
                    m_refreshDelay += (m_refreshDelay < 1000) ? ((m_refreshDelay < 100) ? 10 : 100) : 1000; 
                    ResetCaption();
                    break;
                case SDLK_UP:
                    if (m_refreshDelay)
                        m_refreshDelay -= (m_refreshDelay <= 1000) ? ((m_refreshDelay <= 100) ? ((m_refreshDelay <= 10) ? 1 : 10) : 100) : 1000;
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
            // std::cerr << "Graphics output closed by user." << std::endl;
            m_enabled = false;
            m_screen  = NULL;
            SDL_Quit();
        }
        if (do_resize)
            ResizeScreen(nw, nh);
        Refresh();
    }
#endif
}

void Display::Dump(std::ostream& f, unsigned key, const std::string& comment) const
{
    // Dump the frame buffer
    f << "P3" << std::endl
      << std::dec
      << "#key: " << key << std::endl
      << "#" << comment << std::endl
      << m_width << ' ' << m_height << ' ' << 255 << std::endl;
    for (unsigned y = 0; y < m_height; ++y)
    {
        for (unsigned x = 0; x < m_width; ++x)
        {
            uint32_t d = m_framebuffer[y * m_width + x];
            f << ((d >> 16) & 0xff) << ' '
              << ((d >>  8) & 0xff) << ' '
              << ((d >>  0) & 0xff) << ' ';
        }
        f << std::endl;
    }
}
