#include "display.h"
#include "config.h"
#include <stdexcept>

Display::Display(const Config& config)
    : m_width(1), m_height(1)
{
    m_frame.resize(m_width * m_height);
    std::fill(m_frame.begin(), m_frame.end(), 0);
    
#ifdef USE_SDL
    m_screen = NULL;
    if (config.getBoolean("GfxEnableOutput", false))
    {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
        {
            throw std::runtime_error(std::string("Unable to initialize SDL: ") + SDL_GetError());
        }

        SDL_WM_SetCaption("Microgrid Simulator Display", NULL);
        
        m_refresh = std::max(1U, config.getInteger<unsigned int>("GfxRefreshRate", 30));
        m_scalex  = std::max(1U, config.getInteger<unsigned int>("GfxHorzScale",  2));
        m_scaley  = std::max(1U, config.getInteger<unsigned int>("GfxVertScale",  2));

        // Resize the screen as well
        m_screen = SDL_SetVideoMode(m_width * m_scalex, m_height * m_scaley, 32, SDL_SWSURFACE | SDL_RESIZABLE);
        if (m_screen == NULL)
        {
            SDL_Quit();
            throw std::runtime_error(std::string("Unable to set SDL video mode: ") + SDL_GetError());
        }
        Refresh();
        m_timer = SDL_AddTimer(1000 / m_refresh, TimerCallback, this);        
    }
#endif
}

Display::~Display(void)
{
#ifdef USE_SDL
    if (m_screen != NULL)
    {
        SDL_RemoveTimer(m_timer);
        SDL_Quit();
    }
#endif
}

#ifdef USE_SDL
/*static*/ Uint32 Display::TimerCallback(Uint32 interval, void* param)
{
    static_cast<Display*>(param)->Refresh();
    return interval;
}
#endif

void Display::Resize(unsigned int w, unsigned int h)
{
    m_width  = std::max(1U, w);
    m_height = std::max(1U, h);
    m_frame.resize(m_width * m_height);
    std::fill(m_frame.begin(), m_frame.end(), 0);

#ifdef USE_SDL
    if (m_screen != NULL)
    {
        // Resize the screen as well
        m_screen = SDL_SetVideoMode(m_width * m_scalex, m_height * m_scaley, 32, SDL_SWSURFACE);
        if (m_screen == NULL)
        {
            throw std::runtime_error(std::string("Unable to set SDL video mode: ") + SDL_GetError());
        }
        Refresh();
    }
#endif
}

void Display::Refresh(void)
{
#ifdef USE_SDL
    if (m_screen != NULL && (!SDL_MUSTLOCK(m_screen) || SDL_LockSurface(m_screen) == 0))
    {
        // Copy the frame buffer into the video surface
        for (int dy = 0; dy < m_screen->h; ++dy)
        for (int dx = 0; dx < m_screen->w; ++dx)
        {
            unsigned int sx = dx / m_scalex;
            unsigned int sy = dy / m_scaley;
            Uint32* line = (Uint32*)((char*)m_screen->pixels + dy * m_screen->pitch);
            line[dx] = m_frame[sy * m_width + sx];
        }

        if (SDL_MUSTLOCK(m_screen)) {
            SDL_UnlockSurface(m_screen);
        }
        SDL_Flip(m_screen);
    }
#endif
}

bool Display::CheckEvents()
{
#ifdef USE_SDL
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_VIDEOEXPOSE:
            Refresh();
            break;
        }
    }
#endif
    return true;
}

void Display::Dump(std::ostream& f, unsigned key, const std::string& comment) const
{
    // Dump the frame buffer
    f << "P3" << std::endl
      << "#key: " << key << std::endl
      << "#" << comment << std::endl
      << m_width << ' ' << m_height << ' ' << 255 << std::endl;
    for (unsigned int y = 0; y < m_height; ++y)
    {
        for (unsigned int x = 0; x < m_width; ++x)
        {
            uint32_t d = m_frame[y * m_width + x];
            f << ((d >> 16) & 0xff) << ' '
              << ((d >>  8) & 0xff) << ' '
              << ((d >>  0) & 0xff) << ' ';
        }
        f << std::endl;
    }
}
