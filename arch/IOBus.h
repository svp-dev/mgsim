#ifndef IOBUS_H
#define IOBUS_H

#include "simtypes.h"

namespace Simulator
{

typedef size_t  IODeviceID;     ///< Number of a device on an I/O Bus

/* maximum size of the data in an I/O request. */
static const size_t MAX_IO_OPERATION_SIZE = 64;

/* the data for an I/O request. */
struct IOData
{
    char    data[MAX_IO_OPERATION_SIZE];
    MemSize size;
};

class IIOBusClientCallback
{
public:
    virtual bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size) = 0;
    virtual bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data) = 0;
    virtual bool OnReadResponseReceived(IODeviceID from, const IOData& data) = 0;
    virtual bool OnInterruptRequestReceived(IODeviceID from) = 0;
    virtual bool OnInterruptAckReceived(IODeviceID from) = 0;

    /* for debugging */
    virtual std::string GetIODeviceName() = 0;

    virtual ~IIOBusClientCallback() {}
};

class IIOBus
{
public:
    virtual bool RegisterClient(IODeviceID id, IIOBusClientCallback& client) = 0;

    virtual bool SendReadRequest(IODeviceID from, IODeviceID to, MemAddr address, MemSize size) = 0;
    virtual bool SendWriteRequest(IODeviceID from, IODeviceID to, MemAddr address, const IOData& data) = 0;
    virtual bool SendReadResponse(IODeviceID from, IODeviceID to, const IOData& data) = 0;
    virtual bool SendInterruptRequest(IODeviceID from, IODeviceID to) = 0;
    virtual bool SendInterruptAck(IODeviceID from, IODeviceID to) = 0;

    virtual ~IIOBus() {}
};

}

#endif
