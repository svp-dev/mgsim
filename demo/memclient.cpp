#include "demo/memclient.h"

ExampleMemClient::ExampleMemClient(const std::string& name, Simulator::Object& parent, Simulator::Clock& clock, Config& config)
    : Object(name, parent),
      memory(0), mcid(0),
      enabled("b_enabled", *this, clock, true),
      p_MemoryOutgoing(*this, "send-memory-requests", Simulator::delegate::create<ExampleMemClient, &ExampleMemClient::DoMemoryOutgoing>(*this))
{
    enabled.Sensitive(p_MemoryOutgoing);
    config.registerObject(*this, "example client");
}

void
ExampleMemClient::ConnectMemory(Simulator::IMemory* pmem)
{
    assert(memory == NULL); // can't register two times
    assert(pmem != NULL);

    memory = pmem;
    Simulator::StorageTraceSet traces; // outgoing traces prefixes
    Simulator::StorageTraceSet st; // incoming trace suffixes
    mcid = memory->RegisterClient(*this, p_MemoryOutgoing, traces, st, true);
    p_MemoryOutgoing.SetStorageTraces(opt(traces));
}

Simulator::Result
ExampleMemClient::DoMemoryOutgoing()
{
    using namespace Simulator;
    // Example behavior: load at cycle 4, store at cycle 20

    MemAddr addr = 1024;

    if (GetKernel()->GetCycleNo() == 4)
    {
        // Issue a load:
        if (!memory->Read(mcid, addr))
        {
            DeadlockWrite("Unable to send load from %#016llx to memory", (unsigned long long)addr);
            return FAILED;
        }

        COMMIT {
            // Load was issued successfully:
            std::cerr << "Load issued at cycle " << GetKernel()->GetCycleNo()
                      << " for " << addr << std::endl;
        }
    }

    if (GetKernel()->GetCycleNo() == 20)
    {
        // Issue a load:
        MemData data;
        COMMIT {
            // only populate the data if the store
            // is known to go through successfully
            for (auto &c : data.data) { c = 42; }
            for (auto &b : data.mask) { b = true; }
        }

        if (!memory->Write(mcid, addr, data, (WClientID)-1))
        {
            DeadlockWrite("Unable to send write to %#016llx to memory", (unsigned long long)addr);
            return FAILED;
        }

        COMMIT {
            // Store was issued successfully:
            std::cerr << "Store issued at cycle " << GetKernel()->GetCycleNo()
                      << " for " << addr << std::endl;
        }
    }

    return SUCCESS;
}

bool
ExampleMemClient::OnMemoryWriteCompleted(Simulator::WClientID wid)
{
    COMMIT {
        std::cerr << "Store completion at cycle " << GetKernel()->GetCycleNo()
                  << " received for entity " << wid << std::endl;
    }
    return true;
}

bool
ExampleMemClient::OnMemoryReadCompleted(Simulator::MemAddr addr, const char *)
{
    COMMIT {
        std::cerr << "Load completion at cycle " << GetKernel()->GetCycleNo()
                  << " received for " << addr << std::endl;
    }
    return true;
}

bool
ExampleMemClient::OnMemoryInvalidated(Simulator::MemAddr /*unused*/)
{
    return true;
}

bool
ExampleMemClient::OnMemorySnooped(Simulator::MemAddr /*unused*/, const char* /*data*/, const bool* /*mask*/)
{
    return true;
}

Simulator::Object&
ExampleMemClient::GetMemoryPeer() {
    return *this;
}
