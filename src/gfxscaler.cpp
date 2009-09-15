#include <SDL.h>
#include <cassert>
#include <cstdlib>

class GfxScaler {
public:
  GfxScaler(void) 
    : src(0), dst(0), _scalex(0.0), _scaley(0.0), scaled(false) 
  {}

  GfxScaler(SDL_Surface *asrc, SDL_Surface *adst)
    : src(asrc), dst(adst)
  {
    resize();
  }

  void setSrc(SDL_Surface *src) {
    this->src = src;
    resize();
  }
  void setDst(SDL_Surface *dst) {
    this->dst = dst;
    resize();
  }
  SDL_Surface* getSrc(void) const { return src; }
  SDL_Surface* getDst(void) const { return dst; }

  void resize(void);

  void blit(void) const;

protected:
  SDL_Surface *src;
  SDL_Surface *dst;
  float _scalex;
  float _scaley;  
  bool scaled;
};

void GfxScaler::resize(void)
{
  if (!src || !dst || !dst->w || !dst->h)
    {
      scaled = false;
      return;
    }
  this->_scalex = (float)src->w / (float)dst->w;
  this->_scaley = (float)src->h / (float)dst->h;
  scaled = true;
}

void GfxScaler::blit(void) const
{
  assert(src->format->BytesPerPixel == 4);
  assert(dst->format->BytesPerPixel == 4);
  if (!scaled)
    return;

  if (SDL_MUSTLOCK(src))
    if (SDL_LockSurface(src) < 0)
      return;
  if (SDL_MUSTLOCK(dst))
    if (SDL_LockSurface(dst) < 0) {
      SDL_UnlockSurface(src);
      return;
    }

  Uint32 * __restrict__ srcpixels = (Uint32*)src->pixels;
  int srcpitch = src->pitch/4;
  Uint32 * __restrict__ dstpixels = (Uint32*)dst->pixels;
  int dstpitch = dst->pitch/4;
  int h = dst->h;
  int w = dst->w;
  float scalex = _scalex;
  float scaley = _scaley;

  //  fprintf(stderr, "h = %d, w = %d\n", h, w);
  for (long y = 0; y < h; ++y)
    for (long x = 0; x < w; ++x)
      {
	int srcx = x * scalex;
	int srcy = y * scaley;
	Uint32 *srcp = srcpixels + (ptrdiff_t)(srcy * srcpitch + srcx);
	Uint32 *dstp = dstpixels + (ptrdiff_t)(y * dstpitch + x);
	//	fprintf(stderr, "Pixel src(%d, %d) -> dst(%d, %d) = %lx\n", srcx, srcy, x, y, *srcp);
	*dstp = *srcp;
      }

  if (SDL_MUSTLOCK(dst))
    SDL_UnlockSurface(dst);
  if (SDL_MUSTLOCK(src))
    SDL_UnlockSurface(src);
}
