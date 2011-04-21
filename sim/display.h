#ifndef DISPLAY_H
#define DISPLAY_H

#include "types.h"
#include <string>
#include <iostream>
#include <vector>

#ifdef USE_SDL
#include <SDL.h>
#else
struct SDL_Surface;
#endif

class Config;

namespace Simulator {

class Display
{
    unsigned int          m_width, m_height;
    std::vector<uint32_t> m_framebuffer;
    bool                  m_enabled;
    float                 m_scalex_orig, m_scalex;
    float                 m_scaley_orig, m_scaley;
    unsigned int          m_refreshDelay_orig, m_refreshDelay;
    unsigned long         m_lastRefresh;
    SDL_Surface*          m_screen;
    unsigned int          m_max_screen_h, m_max_screen_w;
    unsigned long         m_nGfxOps;
    
    void ResizeScreen(unsigned int w, unsigned int h);

    Display(const Display&);
    Display& operator=(const Display&);
public:
    Display(const Config& config);

    unsigned int GetRefreshDelay(void) const { return m_refreshDelay; }

    void PutPixel(unsigned int x, unsigned int y, uint32_t data)
    {
        if (x < m_width && y < m_height)
        {
            m_framebuffer[y * m_width + x] = data;
        }
        ++m_nGfxOps;
    }

    void PutPixel(unsigned int offset, uint32_t data)
    {
        if (offset < m_width * m_height) 
        {
            m_framebuffer[offset] = data;
        }
        ++m_nGfxOps;
    }

    void Resize(unsigned w, unsigned h); 
    void Dump(std::ostream&, unsigned key, const std::string& comment = std::string()) const;

#ifdef USE_SDL
    void Refresh();
    void OnCycle(unsigned long long cycle) 
    {
        if (cycle - m_lastRefresh > m_refreshDelay) 
        {
            CheckEvents();
            m_lastRefresh = cycle;
        }
    }           
    void CheckEvents();
    ~Display();
#define CHECK_DISPLAY_EVENTS    
#endif

protected:
    void ResetCaption();
};

}

#endif
