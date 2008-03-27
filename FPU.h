#ifndef FPU_H
#define FPU_H

#include "kernel.h"
#include "ISA.h"
#include <queue>
#include <map>

namespace Simulator
{

class Processor;
class RegisterFile;

class FPU : public IComponent
{
	struct Result
	{
		RegAddr address;
		Float   value;
		CycleNo completion;
	};

	std::map<CycleNo, std::deque<Result> > m_pipelines;

public:
	struct Config
	{
		CycleNo addLatency;
		CycleNo subLatency;
		CycleNo mulLatency;
		CycleNo divLatency;
		CycleNo sqrtLatency;
	};

    FPU(Processor& parent, const std::string& name, RegisterFile& regFile, const Config& config);

	bool queueOperation(int opcode, int func, const Float& Rav, const Float& Rbv, const RegAddr& Rc);

    bool idle()   const;

private:
	bool onCompletion(const Result& res) const;
	Simulator::Result onCycleWritePhase(int stateIndex);

	RegisterFile& m_registerFile;
	Config        m_config;
};

}
#endif

