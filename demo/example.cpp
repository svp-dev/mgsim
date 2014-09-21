#include "demo/tinysim.h"
#include "demo/memclient.h"
#include "demo/prodcons.h"
#include "demo/prodcons2.h"

#include "arch/mem/SerialMemory.h"

#include <cstdlib>


// An example test program:
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
	std::cerr << "usage: " << argv[0] << " <path-to-ini-file> <demoname>" << std::endl
                  << std::endl
                  << "Supported demos:" << std::endl
                  << "   memory          Demo of the memory subsystem with a serial memory." << std::endl
                  << "   prodcons N M S  Demo a producer-consumer with a buffer of size S" << std::endl
                  << "                   and frequency ratio N/M." << std::endl;
	return 0;
    }
    MGSim env(argv[1]);
    std::string demo = argv[2];

    // To show the configuration found so far: env.cfg->dumpConfiguration(std::cerr, argv[1]);

    std::cout << "Initializing the '" << demo << "' demo..." << std::endl;
    if (demo == "prodcons")
    {
	size_t f1 = 1, f2 = 1, sz = 1;
	if (argc > 3)
            f1 = atoi(argv[3]);
	if (argc > 4)
            f2 = atoi(argv[4]);
	if (argc > 5)
            sz = atoi(argv[5]);

	auto& c1 = env.k->CreateClock(f1);
	auto root = new Simulator::Object("", *env.k);
	auto& c2 = env.k->CreateClock(f2);

	auto cons = new ExampleConsumer("cons", *root, c1, sz);
	auto prod = new ExampleProducer("prod", *root, c2, *cons);
    }
    if (demo == "prodcons2")
    {
	size_t f1 = 1, f2 = 1, sz = 0;
	if (argc > 3)
            f1 = atoi(argv[3]);
	if (argc > 4)
            f2 = atoi(argv[4]);
	if (argc > 5)
            sz = atoi(argv[5]);

	auto& c1 = env.k->CreateClock(f1);
	auto root = new Simulator::Object("", *env.k);
	auto& c2 = env.k->CreateClock(f2);

	auto cons = new ExampleConsumer2("cons", *root);
	auto prod = new ExampleProducer2("prod", *root, c2);
	Simulator::Connector *c;
	if (sz > 0)
            c = new Simulator::BufferedConnector<int>("buf", *root, c1, sz);
	else
            c = new Simulator::DirectConnector();
	c->Connect(*cons);
	c->Connect(*prod);
	c->Initialize();
    }
    else if (demo == "memory")
    {
	// Set up a clock and top-leval object
	size_t freq = env.cfg->getValue<size_t>("MemoryFreq");
	auto& clock = env.k->CreateClock(freq);
	auto root = new Simulator::Object("", *env.k);

	// Instantiate a memory system
	auto mem = new Simulator::SerialMemory("memory", *root, clock, *env.cfg);

	// Instantiate a memory client
	auto c = new ExampleMemClient("test", *root, clock, *env.cfg);

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
        env.DoSteps(100000);
	std::cout << "Simulation completed, " << env.k->GetCycleNo() << " cycles elapsed." << std::endl;
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
