#ifndef IOBUS_H
#define IOBUS_H

#include "simtypes.h"
#include "arch/dev/IODeviceDatabase.h"

namespace Simulator
{

typedef size_t  IODeviceID;     ///< Number of a device on an I/O Bus
typedef size_t  IOInterruptID;  ///< Number of an interrupt channel on an I/O bus

/* maximum size of the data in an I/O request. */
static const size_t MAX_IO_OPERATION_SIZE = 64;

/* the data for an I/O request. */
struct IOData
{
    char    data[MAX_IO_OPERATION_SIZE];
    MemSize size;
};

class IIOBusClient
{
public:
    virtual bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
    virtual bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
    virtual bool OnReadResponseReceived(IODeviceID from, MemAddr address, const IOData& data);
    virtual bool OnInterruptRequestReceived(IOInterruptID which);
    virtual bool OnNotificationReceived(IOInterruptID which, Integer tag);

    virtual void GetDeviceIdentity(IODeviceIdentification& id) const = 0;

    /* for debugging */
    virtual std::string GetIODeviceName() const = 0;

    virtual ~IIOBusClient();
};

class Clock;

class IIOBus
{
public:
    virtual bool RegisterClient(IODeviceID id, IIOBusClient& client) = 0;

    virtual bool SendReadRequest(IODeviceID from, IODeviceID to, MemAddr address, MemSize size) = 0;
    virtual bool SendWriteRequest(IODeviceID from, IODeviceID to, MemAddr address, const IOData& data) = 0;
    virtual bool SendReadResponse(IODeviceID from, IODeviceID to, MemAddr address, const IOData& data) = 0;
    virtual bool SendInterruptRequest(IODeviceID from, IOInterruptID which) = 0;
    virtual bool SendNotification(IODeviceID from, IOInterruptID which, Integer tag) = 0;

    virtual IODeviceID GetLastDeviceID() const = 0;
    virtual void GetDeviceIdentity(IODeviceID which, IODeviceIdentification& id) const = 0;

    virtual Clock& GetClock() = 0;

    virtual ~IIOBus();
};

}

#endif
