#ifndef DISPLAY_H
#define DISPLAY_H

#include "types.h"
#include <string>
#include <iostream>
#include <vector>

class Config;
struct SDL_Surface;

class Display
{
    unsigned int          m_width, m_height;
    std::vector<uint32_t> m_framebuffer;
    bool                  m_enabled;
    float                 m_scalex, m_scaley;
    unsigned int          m_refreshDelay;
    unsigned long         m_lastRefresh;
    SDL_Surface*          m_screen;
    
    void ResizeScreen(unsigned int w, unsigned int h);

    Display(const Display&);
    Display& operator=(const Display&);
public:
    Display(const Config& config);
    ~Display();

    void PutPixel(unsigned int x, unsigned int y, uint32_t data)
    {
        if (x < m_width && y < m_height)
        {
            m_framebuffer[y * m_width + x] = data;
        }
    }

    void PutPixel(unsigned int offset, uint32_t data)
    {
        if (offset < m_width * m_height) 
        {
            m_framebuffer[offset] = data;
        }
    }

    void Resize(unsigned w, unsigned h); 
    void Dump(std::ostream&, unsigned key, const std::string& comment = std::string()) const;
    void Refresh();
    void CheckEvents(bool skip_refresh = false);
};

#endif
