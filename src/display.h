#ifndef DISPLAY_H
#define DISPLAY_H

#include "types.h"
#include <string>
#include <iostream>
#include <vector>
#ifdef USE_SDL
#include <SDL.h>
#endif

class Config;

class Display
{
    unsigned int          m_width,  m_height;
    unsigned int          m_scalex, m_scaley;
    unsigned int          m_refresh;
    std::vector<uint32_t> m_frame;
#ifdef USE_SDL
    SDL_Surface*          m_screen;
    SDL_TimerID           m_timer;

    static Uint32 TimerCallback(Uint32, void*);
#endif

    Display(const Display&);
    Display& operator=(const Display&);
public:
    Display(const Config& config);
    ~Display();

    void PutPixel(unsigned int x, unsigned int y, uint32_t data)
    {
        if (x < m_width && y < m_height) {
            m_frame[y * m_width + x] = data;
        }
    }

    void PutPixel(unsigned int offset, uint32_t data)
    {
        if (offset < m_width * m_height) {
            m_frame[offset] = data;
        }
    }
    
    void Resize(unsigned int w, unsigned int h); 
    void Dump(std::ostream&, unsigned key, const std::string& comment = std::string()) const;
    void Refresh();
    bool CheckEvents();
};

#endif
