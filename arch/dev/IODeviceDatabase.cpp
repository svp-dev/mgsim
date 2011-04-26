#include "arch/IOBus.h"
#include <iostream>
#include <iomanip>

using namespace std;

namespace Simulator
{

    const DeviceDatabase::ProviderEntry
    DeviceDatabase::provider_db[] =
    {
        {   1, "MGSim" },

        {   0, NULL }
    };

    const DeviceDatabase::DeviceEntry
    DeviceDatabase::device_db[] =
    {
        { {   1,  0,  1 }, "CPU" },
        { {   1,  1,  1 }, "LCD" },
        { {   1,  2,  1 }, "UART" },
        { {   1,  3,  1 }, "Framebuffer" },
        { {   1,  4,  1 }, "RTC" },
        { {   1,  5,  1 }, "Timer" },

        { {   0,  0,  0 }, NULL }
    };

    
    DeviceDatabase::DeviceDatabase()
    {
       for (size_t i = 0; provider_db[i].name != NULL; ++i)
           m_providers[provider_db[i].id] = provider_db[i].name;
    }
                                   
    const DeviceDatabase
    DeviceDatabase::m_singleton;

    
    void DeviceDatabase::Print(std::ostream& out) const
    {
        out << "Prov. | Mod. | Rev. | Description" << endl
            << "------+------+------+---------------" << endl;
        for (size_t i = 0; device_db[i].name != NULL; ++i)
        {
            out << setw(4) << setfill('0') << hex << device_db[i].id.provider
                << "  | "
                << setw(4) << setfill('0') << hex << device_db[i].id.model
                << " | "
                << setw(4) << setfill('0') << hex << device_db[i].id.revision
                << " | "
                << m_providers.find(device_db[i].id.provider)->second
                << " "
                << device_db[i].name
                << endl;
        }
    }
    
    bool DeviceDatabase::FindDeviceByName(const string& provider, const string& model, IODeviceIdentification& id) const
    {
        for (size_t i = 0; device_db[i].name != NULL; ++i)
        {
            if (provider == m_providers.find(device_db[i].id.provider)->second
                && model == device_db[i].name)
            {
                id = device_db[i].id;
                return true;
            }
        }
        return false;
    }

}

