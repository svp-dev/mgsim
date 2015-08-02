// -*- c++ -*-
#ifndef LCD_H
#define LCD_H

#include <arch/IOMessageInterface.h>
#include <sim/kernel.h>
#include <sim/sampling.h>

#include <fstream>

class Config;

namespace Simulator
{

class LCD : public IIOMessageClient, public Object
{
    IOMessageInterface& m_ioif;
    IODeviceID m_devid;
    char*      m_buffer;

    size_t     m_width;
    size_t     m_height;

    size_t     m_startrow;
    size_t     m_startcolumn;

    unsigned   m_bgcolor;
    unsigned   m_fgcolor;

    DefineStateVariable(size_t, curx);
    DefineStateVariable(size_t, cury);

    std::ofstream *m_tracefile;

    void Refresh(unsigned firstrow, unsigned lastrow) const;

public:
    LCD(const std::string& name, Object& parent, IOMessageInterface& iobus, IODeviceID devid);
    LCD(const LCD&) = delete;
    LCD& operator=(const LCD&) = delete;
    ~LCD();

    bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size) override;
    StorageTraceSet GetReadRequestTraces() const override;
    bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data) override;
    StorageTraceSet GetWriteRequestTraces() const override;

    void GetDeviceIdentity(IODeviceIdentification& id) const override;

    const std::string& GetIODeviceName() const override;
};

}

#endif
