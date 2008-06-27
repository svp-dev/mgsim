#ifndef REGISTERFILE_H
#define REGISTERFILE_H

#include "ports.h"

namespace Simulator
{

class Processor;
class ICache;
class DCache;
class Allocator;

//
// RegisterFile class
//
class RegisterFile : public Structure<RegAddr>
{
    // The register file has 2 read ports dedicated for the Read stage of the pipeline,
    // one write port from the writeback stage of the pipeline, and one asynchronous
    // read and write port for other components (network, memory, etc)
public:
	struct Config
	{
		RegSize numIntegers;
		RegSize numFloats;
	};

    RegisterFile(Processor& parent, ICache& icache, DCache &dcache, Allocator& allocator, const Config& config);

    bool readRegister(const RegAddr& addr, RegValue& data) const;
    bool writeRegister(const RegAddr& addr, RegValue& data, const IComponent& component);
    bool clear(const RegAddr& addr, RegSize size, const RegValue& value);

	// Admin interface, do not use from simulation
    bool writeRegister(const RegAddr& addr, const RegValue& data);
    
    RegSize getSize(RegType type) const;

    DedicatedReadPort            p_pipelineR1;
    DedicatedReadPort            p_pipelineR2;
    DedicatedWritePort<RegAddr>  p_pipelineW;
    ArbitratedReadPort           p_asyncR;
    ArbitratedWritePort<RegAddr> p_asyncW;

private:
    //void onArbitrateWritePhase();

    std::vector<RegValue> m_integers;
    std::vector<RegValue> m_floats;
    Processor& m_parent;
    Allocator& m_allocator;
    ICache&    m_icache;
	DCache&    m_dcache;
};

}
#endif

