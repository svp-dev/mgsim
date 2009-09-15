#include "gfx.h"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <SDL.h>

class GfxScreen 
{
public:
  GfxScreen(void)
    : fbsurface(0), screensurface(0),
      sdl_init_done(false) 
  { }
  ~GfxScreen(void);

  void resetFBSurface(const GfxFrameBuffer&);
  void resizeScreen(size_t nw, size_t nh);
  void update(void);

  SDL_Surface *fbsurface;
  SDL_Surface *screensurface;
protected:

  void check_sdl_init(void);
  bool sdl_init_done;

  GfxScreen(const GfxScreen&);
  GfxScreen& operator=(const GfxScreen&);
};

GfxScreen::~GfxScreen(void)
{
  if (fbsurface)
    SDL_FreeSurface(fbsurface);
}

void GfxScreen::resetFBSurface(const GfxFrameBuffer& fb)
{
  if (fbsurface) {
    SDL_FreeSurface(fbsurface);
    fbsurface = 0;
  }
  if (screensurface && fb.getBuffer())
    {
      fbsurface = SDL_CreateRGBSurfaceFrom(fb.getBuffer(),
					   fb.getWidth(),
					   fb.getHeight(),
					   32,
					   fb.getWidth()*4,
					   screensurface->format->Rmask,
					   screensurface->format->Gmask,
					   screensurface->format->Bmask,
					   screensurface->format->Amask);
      if (!fbsurface)
	std::cerr << "Failed to create SDL surface for frame buffer: "
		  << SDL_GetError() << std::endl;
      else {
	std::ostringstream caption;
	caption << "Framebuffer: " << fb.getWidth() << " x " << fb.getHeight();
	SDL_WM_SetCaption(caption.str().c_str(), 0);
      }
    }
  
  
}

void GfxScreen::check_sdl_init(void)
{
  if (!sdl_init_done)
    {
      if (SDL_Init(SDL_INIT_VIDEO) < 0)
	std::cerr << "Unable to initialize SDL: " << SDL_GetError() << std::endl;
      else
	{
	  atexit(SDL_Quit);
	  sdl_init_done = true;
	}
    }
}

void GfxScreen::resizeScreen(size_t nw, size_t nh)
{
  check_sdl_init();
  if (nw < 100) nw = 100;
  if (nh < 100) nh = 100;
  if (nw > 1024) nw = 1024;
  if (nh > 768) nh = 768;
  screensurface = SDL_SetVideoMode(nw, nh, 32, SDL_SWSURFACE|SDL_RESIZABLE);
  if (!screensurface)
    {
      std::cerr << "Set video mode failed: " << SDL_GetError() << std::endl;
      std::cerr << "Trying default mode..." << std::endl;
      screensurface = SDL_SetVideoMode(640, 480, 32, SDL_SWSURFACE|SDL_RESIZABLE);
      if (!screensurface)
	{
	  std::cerr << "Failed again: " << SDL_GetError() << std::endl;
	  std::cerr << "Display disabled." << std::endl;
	}
    }
}

void GfxScreen::update(void)
{
  if (screensurface)
    SDL_UpdateRect(screensurface,
		   0, 0, 
		   screensurface->w, screensurface->h);
}
