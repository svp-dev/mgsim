#include "gfx.h"
#include <cstring>
#include <sys/resource.h>

#ifdef USE_SDL
#include "gfxscaler.cpp"
#include "gfxscreen.cpp"
#else
class GfxScaler {};
class GfxScreen {};
#endif

GfxFrameBuffer::GfxFrameBuffer(void)
  : buffer(NULL), width(0), height(0)
{ }

GfxFrameBuffer::GfxFrameBuffer(size_t w, size_t h)
  : buffer(NULL), width(0), height(0)
{
  resize(w, h);
}

void
GfxFrameBuffer::resize(size_t nw, size_t nh)
{
  if (!nw || !nh)
    return;
  if (buffer)
    delete[] buffer;
  
  buffer = new uint32_t[nw * nh];
  memset(buffer, 0, nw * nh * sizeof(uint32_t));
  width = nw;
  height = nh;
}

void
GfxFrameBuffer::dump(std::ostream& f, unsigned key, const std::string& comment) const
{
  assert(buffer != NULL);
  size_t w = width, h = height;

  f << "P3" << std::endl
    << "#key: " << key << std::endl
    << "#" << comment << std::endl
    << w << ' ' << h << ' ' << 255 << std::endl;
  for (size_t y = 0; y < h; ++y) {
    for (size_t x = 0; x < w; ++x) {
      uint32_t d = buffer[y * w + x];
      f << ((d >> 16) & 0xff) << ' ' << ((d >> 8) & 0xff) << ' ' << (d & 0xff) << ' ';
    }
    f << std::endl;
  }
}

GfxFrameBuffer::~GfxFrameBuffer(void)
{
  if (buffer)
    delete[] buffer;
}


GfxDisplay::GfxDisplay(size_t rrate, size_t w, size_t h, 
		       size_t scx, size_t scy, bool enable_output)
  : scalex(scx), scaley(scy), counter(0), 
    refreshrate(rrate < 1 ? 1 : rrate), 
    fb(w, h), screen(0), scaler(0)
{
#ifdef USE_SDL
  if (enable_output)
    screen = new GfxScreen();
  scaler = new GfxScaler();
  resizeScreen(w*scalex, h*scaley);
#endif
}

GfxDisplay::~GfxDisplay(void)
{
#ifdef USE_SDL
  if (scaler)
    delete scaler;
  if (screen)
    delete screen;
#endif
}

void GfxDisplay::resizeFrameBuffer(size_t nw, size_t nh)
{
  fb.resize(nw, nh);
#ifdef USE_SDL
  if (screen) {
    screen->resetFBSurface(fb);
    resizeScreen(nw*scalex, nh*scaley);
  }
#endif
}

void GfxDisplay::resizeScreen(size_t nw, size_t nh)
{
#ifdef USE_SDL
  if (screen) {
    screen->resizeScreen(nw, nh);
    if (!screen->fbsurface)
      screen->resetFBSurface(fb);
    scaler->setSrc(screen->fbsurface);
    scaler->setDst(screen->screensurface);
    refresh();
  }
#endif
}

void GfxDisplay::refresh(void)
{
#ifdef USE_SDL
  checkEvents(true);
  if (screen) {
    scaler->blit();
    screen->update();
  }
#endif
}

void GfxDisplay::checkEvents(bool skip_refresh)
{
#ifdef USE_SDL
  SDL_Event event;
  bool dorefresh = false;
  bool doresize = false;
  bool doclose = false;
  size_t nw = 0, nh = 0;
  while (SDL_PollEvent(&event))
    switch (event.type) {
    case SDL_VIDEORESIZE:
      nw = event.resize.w;
      nh = event.resize.h;
      doresize = true;
    case SDL_VIDEOEXPOSE:
      dorefresh = true;
      break;
    case SDL_QUIT:
      doclose = true;
      break;
    }
  if (doclose)
    exit(0);
  if (doresize)
    resizeScreen(nw, nh);
  if (dorefresh && !skip_refresh)
    refresh();
#endif
}

