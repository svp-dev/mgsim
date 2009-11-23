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
    m_scalex(1.0f / std::max(1U, config.getInteger<unsigned int>("GfxHorizScale", 2))),
    m_scaley(1.0f / std::max(1U, config.getInteger<unsigned int>("GfxVertScale",  2))),
    m_refreshDelay(1000 / std::max(1U, config.getInteger<unsigned>("GfxRefreshRate", 30))),
    m_screen(NULL)
{
#ifdef USE_SDL
    if (config.getBoolean("GfxEnableOutput", false))
    {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "Unable to initialize SDL: " << SDL_GetError() << std::endl;
        } else {
            m_enabled = true;
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
        m_scalex = (float)m_width  / (float)w;
        m_scaley = (float)m_height / (float)h;
        m_screen = SDL_SetVideoMode(w, h, 32, SDL_SWSURFACE | SDL_RESIZABLE);
        if (m_screen == NULL) {
            std::cerr << "Setting SDL video mode failed: " << SDL_GetError() << std::endl;
        } else {
            std::stringstream caption;
            caption << "Microgrid Simulator Display (" << m_width << "x" << m_height << ")";
            SDL_WM_SetCaption(caption.str().c_str(), NULL);
            Refresh();
        }
    }
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
        else if (!SDL_MUSTLOCK(m_screen) || SDL_LockSurface(m_screen) == 0)
        {
            assert(m_screen->format->BytesPerPixel == 4);

            // Copy the buffer into the video surface
            for (int dy = 0; dy < m_screen->h; ++dy) 
            {
                unsigned int    sy   = (unsigned int)(dy * m_scaley);
                Uint32*         dest = (Uint32*)((char*)m_screen->pixels + dy * m_screen->pitch);
                const uint32_t* src  = &m_framebuffer[sy * m_width];
	            for (int dx = 0; dx < m_screen->w; ++dx)
	            {
                    dest[dx] = src[ (unsigned int)(dx * m_scalex) ];
	            }
	        }
	        
            if (SDL_MUSTLOCK(m_screen))
            {
    	        SDL_UnlockSurface(m_screen);
	        }
            SDL_Flip(m_screen);
            m_lastRefresh = SDL_GetTicks();
        }
    }
#endif
}

void Display::CheckEvents(bool skip_refresh)
{
#ifdef USE_SDL
    if (m_enabled)
    {
        bool refresh = (SDL_GetTicks() - m_lastRefresh > m_refreshDelay);
        
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
	        case SDL_QUIT:
                m_enabled = false;
                m_screen  = NULL;
                SDL_Quit();
	            break;
	        
            case SDL_VIDEOEXPOSE:
                refresh = true;
	            break;

	        case SDL_VIDEORESIZE:
	            ResizeScreen(event.resize.w, event.resize.h);
	            break;
            }
        }
        
        if (refresh)
        {
            Refresh();
        }
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
