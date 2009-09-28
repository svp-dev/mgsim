#ifndef GFX_H
#define GFX_H

#include "types.h"
#include <cassert>
#include <cstddef>
#include <string>
#include <iostream>

class GfxFrameBuffer {
 public:
  GfxFrameBuffer(void);
  GfxFrameBuffer(size_t w, size_t h);
  ~GfxFrameBuffer(void);

  uint32_t* getBuffer(void) const { return buffer; }

  size_t getWidth(void) const { return width; }
  size_t getHeight(void) const { return height; }

  void putPixel(size_t x, size_t y, uint32_t data) const
  {
    if (x >= width || y >= height)
      return;
    buffer[x + y * width] = data;
  }

  void putPixel(size_t offset, uint32_t data) const
  {
    if (offset >= (width * height))
      return;
    buffer[offset] = data;
  }

  void resize(size_t nw, size_t nh);

  void dump(std::ostream&, unsigned key, const std::string& comment = std::string()) const;

protected:
  GfxFrameBuffer(const GfxFrameBuffer&);
  GfxFrameBuffer& operator=(const GfxFrameBuffer&);

  uint32_t *__restrict__ buffer;
  size_t width;
  size_t height;
};

class GfxScaler;
class GfxScreen;

class GfxDisplay
{
 public:
  GfxDisplay(size_t rrate, size_t w, size_t h, 
	     size_t scx, size_t scy, bool enable_output = true);
  ~GfxDisplay(void);

  const GfxFrameBuffer& getFrameBuffer(void) const { return fb; }

  void resizeFrameBuffer(size_t nw, size_t nh);
  void resizeScreen(size_t nw, size_t nh);
  
  void cycle(void) {
    if (counter++ > refreshrate) {
      counter = 0;
      refresh();
    }
  }
  
  void refresh(void);
  void checkEvents(bool skip_refresh = false);
  
 protected:
  size_t scalex;
  size_t scaley;
  size_t counter;
  size_t refreshrate;
  GfxFrameBuffer fb;
  GfxScreen *screen;
  GfxScaler *scaler;

  GfxDisplay(const GfxDisplay&);
  GfxDisplay& operator=(const GfxDisplay&);
};

#endif
