#include "display.h"
#include "config.h"
#include <stdexcept>
#include <cassert>

class ScreenOutput {
protected:
  friend class Display;
#ifdef USE_SDL

  bool                  m_sdl_enabled;
  SDL_Surface*          m_screen;
  float                 m_scalex, m_scaley;

  ScreenOutput(const Config& config) : 
    m_sdl_enabled(false), 
    m_screen(NULL), 
    m_scalex(std::max(1U, config.getInteger<unsigned>("GfxHorizScale", 2))),
    m_scaley(std::max(1U, config.getInteger<unsigned>("GfxVertScale", 2)))
  {
    if (!config.getBoolean("GfxEnableOutput", false))
      return;
    
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
      {
	std::cerr << "Unable to initialize SDL: " << SDL_GetError() << std::endl
		  << "Graphics disabled." << std::endl;
	return;
      }
    m_sdl_enabled = true;
  }

  void ResizeScreen(unsigned nw, unsigned nh) {
    if (!m_sdl_enabled)
      return;

    nw = std::min(1024U, std::max(100U, nw));
    nh = std::min(768U, std::max(100U, nh));
    m_screen = SDL_SetVideoMode(nw, nh, 32, SDL_SWSURFACE|SDL_RESIZABLE);
    if (!m_screen) {
      std::cerr << "Set SDL video mode failed: " << SDL_GetError() << std::endl
		<< "Trying default mode..." << std::endl;
      m_screen = SDL_SetVideoMode(640, 480, 32, SDL_SWSURFACE|SDL_RESIZABLE);
      if (!m_screen)
	{
	  std::cerr << "Failed again: " << SDL_GetError() << std::endl
		    << "Output disabled until next program resize." << std::endl;
	}
    }
#ifndef NDEBUG
    if (m_screen) 
      std::cerr << "Display resized to " 
		<< m_screen->w << " x " << m_screen->h << std::endl;
#endif
  }

  ~ScreenOutput(void) {
    if (m_sdl_enabled) 
      SDL_Quit();
  }

#else
  ScreenOutput(const Config&) {}
#endif
};

void Display::ResizeOutput(unsigned w, unsigned h)
{
#ifdef USE_SDL
  m_output->ResizeScreen(w, h);
  if (m_output->m_screen) {
    // Recompute scale factors
    m_output->m_scalex = 
      (float) m_output->m_screen->w / (float) m_fb_width;
    m_output->m_scaley = 
      (float) m_output->m_screen->h / (float) m_fb_height;

    // Set window caption
    std::ostringstream caption;
    caption << "Framebuffer: " << m_fb_width << " x " << m_fb_height;
    SDL_WM_SetCaption(caption.str().c_str(), 0);
  }
#endif
}

Display::Display(const Config& config)
  : m_fb_width(config.getInteger<unsigned>("GfxDisplayWidth", 100)), 
    m_fb_height(config.getInteger<unsigned>("GfxDisplayHeight", 100)),
    m_framebuffer(m_fb_width * m_fb_height, 0),
    m_refreshrate(std::max(1U, config.getInteger<unsigned>("GfxRefreshRate", 100))),
    m_counter(0),
    m_output(new ScreenOutput(config))
{
#ifdef USE_SDL
  // Framebuffer was just initially sized; 
  // therefore attempt to resize the screen
  ResizeOutput(m_fb_width * m_output->m_scalex, 
	       m_fb_height * m_output->m_scaley);
#endif
}

Display::~Display(void)
{
  delete m_output;
}

void Display::ResizeFramebuffer(unsigned int w, unsigned int h)
{
    m_fb_width  = std::max(1U, w);
    m_fb_height = std::max(1U, h);
    m_framebuffer.resize(m_fb_width * m_fb_height);
    std::fill(m_framebuffer.begin(), m_framebuffer.end(), 0);

#ifdef USE_SDL
    // Try to resize the screen as well
    ResizeOutput(m_fb_width * m_output->m_scalex, 
		 m_fb_height * m_output->m_scaley);
    Refresh();
#endif
}

void Display::Refresh(void)
{
#ifdef USE_SDL
  if (!m_output->m_sdl_enabled)
    return;

  CheckEvents(true);

  SDL_Surface *m_screen = m_output->m_screen;

  if (m_screen != NULL 
      && (!SDL_MUSTLOCK(m_screen) || SDL_LockSurface(m_screen) == 0))
    {
      assert(m_screen->format->BytesPerPixel == 4);

      float scalex = (float) m_screen->w / (float) m_fb_width;
      float scaley = (float) m_screen->h / (float) m_fb_height;
      unsigned fb_w = m_fb_width;
      unsigned screen_w = m_screen->w;
      unsigned screen_h = m_screen->h;
      unsigned screen_vscan = m_screen->pitch / 4;

      Uint32* pixels = (Uint32*)m_screen->pixels;

#ifndef NDEBUG
      std::cerr << "Display refresh:"
		<< " scalex = " << scalex
		<< " scaley = " << scaley
		<< " screen w = " << screen_w
		<< " screen h = " << screen_h
		<< " screen vscan = " << screen_vscan
		<< " fb w = " << fb_w
		<< std::endl;
#endif

      // Copy the frame buffer into the video surface
      for (unsigned screen_y = 0; screen_y < screen_h; ++screen_y) 
	{
	  unsigned fb_y = screen_y / scaley;
	  unsigned screen_line_base = screen_y * screen_vscan;
	  unsigned fb_line_base = fb_y * fb_w;

#ifndef NDEBUG
	  std::cerr << "Display line:" 
		    << " screen base = " << screen_line_base
		    << " fb base = " << fb_line_base
		    << std::endl;
#endif
	  
	  for (unsigned screen_x = 0; screen_x < screen_w; ++screen_x)
	    {
	      unsigned fb_x = screen_x / scalex;

	      unsigned screen_offset = screen_line_base + screen_x;
	      unsigned fb_offset = fb_line_base + fb_x;
	      pixels[screen_offset] = 
		m_framebuffer[fb_offset];
	    }
	}
      if (SDL_MUSTLOCK(m_screen))
	SDL_UnlockSurface(m_screen);
	
      SDL_Flip(m_screen);
    }
#endif
}

void Display::CheckEvents(bool skip_refresh)
{
#ifdef USE_SDL
  if (!m_output->m_sdl_enabled)
    return;
  SDL_Event event;
  bool dorefresh = false, doresize = false, doclose = false;
  unsigned nw = 0, nh = 0;
  while (SDL_PollEvent(&event))
    {
      switch (event.type)
        {
	case SDL_QUIT:
	  doclose = true;
	  break;
        case SDL_VIDEOEXPOSE:
	  dorefresh = true;
	  break;
	case SDL_VIDEORESIZE:
	  nw = event.resize.w;
	  nh = event.resize.h;
	  doresize = true;
	  break;
        }
    }
  if (doclose) {
    m_output->m_sdl_enabled = false;
    m_output->m_screen = NULL;
    SDL_Quit();
    std::cerr << "Hangup from graphics output, display disabled." << std::endl;
  }
  if (doresize)
    ResizeOutput(nw, nh);
  if (dorefresh && !skip_refresh)
    Refresh();
#endif
}

void Display::Dump(std::ostream& f, unsigned key, 
		   const std::string& comment) const
{
    // Dump the frame buffer
    f << "P3" << std::endl
      << std::dec
      << "#key: " << key << std::endl
      << "#" << comment << std::endl
      << m_fb_width << ' ' << m_fb_height << ' ' << 255 << std::endl;
    for (unsigned y = 0; y < m_fb_height; ++y)
    {
        for (unsigned x = 0; x < m_fb_width; ++x)
        {
            uint32_t d = m_framebuffer[y * m_fb_width + x];
            f << ((d >> 16) & 0xff) << ' '
              << ((d >>  8) & 0xff) << ' '
              << ((d >>  0) & 0xff) << ' ';
        }
        f << std::endl;
    }
}
