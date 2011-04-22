#ifndef IOINTERFACE_H
#define IOINTERFACE_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

#include "IOResponseMultiplexer.h"
#include "IOInterruptMultiplexer.h"
#include "IOBusInterface.h"

class IOInterface : public Object
{
private:
    IOResponseMultiplexer   m_rrmux;
    IOInterruptMultiplexer  m_intmux;
    IOBusInterface          m_iobus_if;

public:
    IOInterface(const std::string& name, Object& parent, Clock& clock, RegisterFile& rf, IIOBus& iobus, const Config& config);

    bool Read(IODeviceID dev, MemAddr address, MemSize size, const RegAddr& writeback);
    bool Write(IODeviceID dev, MemAddr address, const IOData& data);
    bool WaitForNotification(IODeviceID dev, const RegAddr& writeback);

};


#endif
