#ifndef IODEVICEDATABASE_H
#define IODEVICEDATABASE_H

#include <map>

namespace Simulator
{

struct IODeviceIdentification
{
    uint16_t  provider;
    uint16_t  model;
    uint16_t  revision;
};

class DeviceDatabase
{
    struct ProviderEntry {
        uint64_t                    id;
        const char                  *name;
    };
    static const ProviderEntry      provider_db[];

    struct DeviceEntry {
        IODeviceIdentification      id;
        const char                  *name;
    };
    static const DeviceEntry        device_db[];


    std::map<uint16_t, std::string> m_providers;

    DeviceDatabase();

    static const DeviceDatabase     m_singleton;


public:
    static
    const DeviceDatabase& GetDatabase() { return m_singleton; }

    void Print(std::ostream& out) const;

    bool FindDeviceByName(const std::string& provider, const std::string& model, IODeviceIdentification& id) const;
};


}


#endif
