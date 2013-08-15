#ifndef DRISC_FORWARD_H
#define DRISC_FORWARD_H

// Forward declarations
class Config;
namespace Simulator
{
    class DRISC;
    class IBankSelector;

    namespace counters {};

    namespace drisc
    {
        class FamilyTable;
        struct Family;
        class ThreadTable;
        struct Thread;
        class RegisterFile;
        class RAUnit;
        class ICache;
        class DCache;
        class Network;
        class Pipeline;
        class Allocator;
        class MMIOComponent;
        class IOMatchUnit;
        class IOBusInterface;
        class IOResponseMultiplexer;
        class IONotificationMultiplexer;
        class IODirectCacheAccess;
        class IOInterface;
        struct RemoteMessage;
        struct LinkMessage;
        struct AllocResponse;

        const std::vector<std::string>& GetDefaultLocalRegisterAliases(RegType type);
// ISA-specific function to map virtual registers to register classes
        unsigned char GetRegisterClass(unsigned char addr, const RegsNo& regs, RegClass* rc, RegType type);
    }
}

#endif
