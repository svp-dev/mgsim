#ifndef IOBUS_H
#define IOBUS_H

#include <arch/simtypes.h>
#include <arch/dev/IODeviceDatabase.h>
#include <sim/storagetrace.h>

namespace Simulator
{

typedef size_t  IODeviceID;     ///< Number of a device on an I/O Bus
typedef size_t  IONotificationChannelID;  ///< Number of a notification/interrupt channel on an I/O bus

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
    virtual bool OnInterruptRequestReceived(IONotificationChannelID which);
    virtual bool OnNotificationReceived(IONotificationChannelID which, Integer tag);
    
    virtual StorageTraceSet GetReadRequestTraces() const { return StorageTraceSet(); }
    virtual StorageTraceSet GetWriteRequestTraces() const { return StorageTraceSet(); }
    virtual StorageTraceSet GetReadResponseTraces() const { return StorageTraceSet(); }
    virtual StorageTraceSet GetInterruptRequestTraces() const { return StorageTraceSet(); }
    virtual StorageTraceSet GetNotificationTraces() const { return StorageTraceSet(); }


    // Admin
    virtual void Initialize();
    virtual std::string GetIODeviceName() const = 0;
    virtual void GetDeviceIdentity(IODeviceIdentification& id) const = 0;

    virtual ~IIOBusClient();
};

class Clock;
class Object;

class IIOBus
{
public:
    virtual bool RegisterClient(IODeviceID id, IIOBusClient& client) = 0;

    virtual bool SendReadRequest(IODeviceID from, IODeviceID to, MemAddr address, MemSize size) = 0;
    virtual bool SendWriteRequest(IODeviceID from, IODeviceID to, MemAddr address, const IOData& data) = 0;
    virtual bool SendReadResponse(IODeviceID from, IODeviceID to, MemAddr address, const IOData& data) = 0;
    virtual bool SendInterruptRequest(IODeviceID from, IONotificationChannelID which) = 0;
    virtual bool SendNotification(IODeviceID from, IONotificationChannelID which, Integer tag) = 0;

    virtual StorageTraceSet GetReadRequestTraces(IODeviceID from) const = 0;
    virtual StorageTraceSet GetWriteRequestTraces() const = 0;
    virtual StorageTraceSet GetReadResponseTraces() const = 0;
    virtual StorageTraceSet GetInterruptRequestTraces() const = 0;
    virtual StorageTraceSet GetNotificationTraces() const = 0;

    virtual Clock& GetClock() = 0;

    // Admin
    virtual void Initialize() = 0;

    virtual IODeviceID GetLastDeviceID() const = 0;
    virtual IODeviceID GetNextAvailableDeviceID() const = 0;
    virtual IODeviceID GetDeviceIDByName(const std::string& objname) const = 0;
    virtual Object& GetDeviceByName(const std::string& objname) const = 0;
    virtual void GetDeviceIdentity(IODeviceID which, IODeviceIdentification& id) const = 0;

    virtual ~IIOBus();
};

}

#endif
