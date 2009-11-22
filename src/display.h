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

class ScreenOutput;

class Display
{
  unsigned int          m_fb_width;
  unsigned int	        m_fb_height;
  std::vector<uint32_t> m_framebuffer;
  
  unsigned              m_refreshrate;
  unsigned              m_counter;

  ScreenOutput*         m_output; 

  Display(const Display&);
  Display& operator=(const Display&);
 public:
  Display(const Config& config);
  ~Display();

  void PutPixel(unsigned int x, unsigned int y, uint32_t data)
  {
    if (x < m_fb_width && y < m_fb_height)
      m_framebuffer[y * m_fb_width + x] = data;
  }

  void PutPixel(unsigned int offset, uint32_t data)
  {
    if (offset < m_fb_width * m_fb_height) 
      m_framebuffer[offset] = data;
  }

  void ResizeFramebuffer(unsigned w, unsigned h); 

  void Dump(std::ostream&, unsigned key, 
	    const std::string& comment = std::string()) const;
  void Refresh();

  void CheckEvents(bool skip_refresh = false);

  void OnCycle(void) {
    if (m_counter++ > m_refreshrate) {
      m_counter = 0;
      Refresh();
    }
  }

 protected:
  void ResizeOutput(unsigned w, unsigned h);
};

#endif
