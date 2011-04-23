#ifndef LCD_H
#define LCD_H

#include "arch/IOBus.h"
#include "sim/kernel.h"

namespace Simulator
{

class LCD : public IIOBusClientCallback, public Object
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

    void Refresh(unsigned firstrow, unsigned lastrow) const;

public:
    LCD(const std::string& name, Object& parent,
        size_t width, size_t height,
        size_t startrow, size_t startcolumn,
        unsigned bgcolor, unsigned fgcolor);

    bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size) { return false; }
    bool OnReadResponseReceived(IODeviceID from, const IOData& data) { return false; }

    bool OnInterruptRequestReceived(IODeviceID from) { return false; }
    bool OnInterruptAckReceived(IODeviceID from) { return false; }

    bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);

    std::string GetIODeviceName() const { return GetFQN(); }

    ~LCD() {}
};


}

#endif
