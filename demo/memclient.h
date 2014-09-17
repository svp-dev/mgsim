#ifndef MEMCLIENT_H
#define MEMCLIENT_H

#include "arch/Memory.h"
#include "sim/kernel.h"
#include "sim/config.h"

class ExampleMemClient : public Simulator::Object, public Simulator::IMemoryCallback {

public:
    // Constructor
    ExampleMemClient(const std::string& name, Simulator::Object& parent, Simulator::Clock& clock, Config& config);

    // Connect memory
    void ConnectMemory(Simulator::IMemory* memory);

    // Interface: component -> memory

    Simulator::IMemory *memory;
    Simulator::MCID mcid;
    Simulator::SingleFlag enabled;
    Simulator::Process p_MemoryOutgoing;
    Simulator::Result DoMemoryOutgoing();

    // Interface: memory -> component
    bool OnMemoryReadCompleted(Simulator::MemAddr addr, const char* data) override;
    bool OnMemoryWriteCompleted(Simulator::WClientID wid) override;
    bool OnMemorySnooped(Simulator::MemAddr /*unused*/, const char* /*data*/, const bool* /*mask*/) override;
    bool OnMemoryInvalidated(Simulator::MemAddr /*unused*/) override;
    virtual Simulator::Object& GetMemoryPeer() override;

private:
    ExampleMemClient(const ExampleMemClient&) = delete;
    ExampleMemClient& operator=(const ExampleMemClient&) = delete;
};


#endif
