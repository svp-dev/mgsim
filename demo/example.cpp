// Includes necessary for the simulation kernel and initialization:
#include "demo/tinysim.h"
#include "demo/memclient.h"

// Memory system(s) to use:
#include "arch/mem/SerialMemory.h"

// An example test program:
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
	std::cerr << "usage: " << argv[0] << " <path-to-ini-file> <demoname>" << std::endl
		  << std::endl
		  << "Supported demos:" << std::endl
		  << "   memory      Demonstrate the memory subsystem with a serial memory." << std::endl;
	return 0;
    }
    MGSim env(argv[1]);
    std::string demo = argv[2];

    // To show the configuration found so far: env.cfg->dumpConfiguration(std::cerr, argv[1]);

    std::cout << "Initializing the '" << demo << "' demo..." << std::endl;
    if (demo == "memory")
    {
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
    }
    else
    {
	std::cerr << "Unknown demo mode, using empty simulation." << std::endl;
    }

    std::cout << "Initialization done, starting simulation..." << std::endl;

    // Global simulation loop: simulate 100 cycles
    try {
        env.DoSteps(100);
	std::cout << "Simulation completed, " << env.k.GetCycleNo() << " cycles elapsed." << std::endl;
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
