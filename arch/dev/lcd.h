#ifndef LCD_H
#define LCD_H

#include "arch/MMIO.h"

namespace Simulator
{

class LCD : public MMIOComponent
{
    char*     m_buffer;

    size_t    m_width;
    size_t    m_height;

    size_t    m_startrow;
    size_t    m_startcolumn;

    unsigned  m_bgcolor;
    unsigned  m_fgcolor;

    size_t    m_curx;
    size_t    m_cury;

public:
    LCD(const std::string& name, MMIOInterface& parent,
        size_t width, size_t height,
        size_t startrow, size_t startcolumn,
        unsigned bgcolor, unsigned fgcolor);

    size_t GetSize() const;
    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid) { return FAILED; }
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);
    
    void Refresh(unsigned firstrow, unsigned lastrow) const;

    ~LCD() {}
};


}

#endif
