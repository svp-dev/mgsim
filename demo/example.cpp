// Includes necessary for the simulation kernel and initialization:
#include "demo/tinysim.h"
#include "demo/memclient.h"

// Memory system(s) to use:
#include "arch/mem/SerialMemory.h"

// An example test program:
int main(int argc, char *argv[])
{
    const char *f = MGSIM_CONFIG_PATH;
    if (argc > 1)
	f = argv[1];
    MGSim env(f);
    // To show the configuration found so far: env.cfg->dumpConfiguration(std::cerr, argv[1]);

    // Set up a clock and top-leval object
    size_t freq = env.cfg->getValue<size_t>("MemoryFreq");
    Simulator::Clock& clock = env.k.CreateClock(freq);
    Simulator::Object root("", clock);

    // Instantiate a memory system
    Simulator::SerialMemory* mem = new Simulator::SerialMemory("memory", root, clock, *env.cfg);

    // Instantiate a memory client
    ExampleMemClient *c = new ExampleMemClient("test", root, clock, *env.cfg);

    // Connect the client to memory
    c->ConnectMemory(mem);

    // Initialize the memory -- after all clients have been registered
    mem->Initialize();

    // Global simulation loop: simulate 100 cycles
    try {
        env.DoSteps(100);
    }
    catch (const std::exception& e) {
        // Standard exception message
        std::cerr << std::endl << e.what() << std::endl;

        // For MGSim exceptions, try to fish for more:
        auto se = dynamic_cast<const Simulator::SimulationException*>(&e);
        if (se != NULL)
        {
            for (auto& p : se->GetDetails())
                std::cerr << p << std::endl;
        }
        return 1;
    }
    return 0;
}
