#ifndef FPU_H
#define FPU_H

#include "kernel.h"
#include <queue>
#include <map>

class Config;

namespace Simulator
{

class Processor;
class RegisterFile;

/**
 * The different kinds of floating point operations that the FPU can perform
 */
enum FPUOperation
{
    FPU_OP_NONE = -1,   ///< Reserved for internal use
    FPU_OP_ADD  =  0,   ///< Addition
    FPU_OP_SUB  =  1,   ///< Subtraction
    FPU_OP_MUL  =  2,   ///< Multiplication
    FPU_OP_DIV  =  3,   ///< Division
    FPU_OP_SQRT =  4,   ///< Square root
};

/**
 * @brief Floating Point Unit
 *
 * This component accepts floating point operations, executes them asynchronously and writes them
 * back once calculated. It has several pipelines, assuming every operation of equal delay can be pipelined.
 */
class FPU : public IComponent
{
    /**
     * Represents the result of an FP operation
     */
	struct Result
	{
		RegAddr    address;    ///< Address of destination register of result.
		MultiFloat value;      ///< Resulting value of the operation.
		int        size;       ///< Size of the resulting value.
		CycleNo    completion; ///< Completion time of the operation.
	};

	std::map<CycleNo, std::deque<Result> > m_pipelines; ///< The pipelines in the FPU, one per kind of operation

public:
    /**
     * Constructs the FPU.
     * @param parent  reference to the parent processor
     * @param name    name of the FPU, irrelevant to simulation
     * @param regFile reference to the register file in which to write back results
     * @param config  reference to the configuration data
     */
    FPU(Processor& parent, const std::string& name, RegisterFile& regFile, const Config& config);

    /**
     * @brief Queues an FP operation.
     * This function determines the length of the operation and queues the operation in the corresponding
     * pipeline. When the operation has completed, the result is written back to the register file.
     * @param op   the FP operation to perform
     * @param size size of the FP operation (4 or 8)
     * @param Rav  first (or only) operand of the operation
     * @param Rbv  second operand of the operation
     * @param Rc   address of the destination register(s)
     * @return true if the operation could be queued.
     */
	bool QueueOperation(FPUOperation op, int size, double Rav, double Rbv, const RegAddr& Rc);

private:
    /**
     * Called when an operation has completed.
     * @param res the result to write back to the register file.
     * @return true if the result could be written back to the register file.
     */
	bool OnCompletion(const Result& res) const;
	
	Simulator::Result OnCycleWritePhase(unsigned int stateIndex);

	RegisterFile& m_registerFile;   ///< Reference to the register file in which to write back results

	CycleNo m_addLatency;     ///< Delay for an FP addition
	CycleNo m_subLatency;     ///< Delay for an FP subtraction
	CycleNo m_mulLatency;     ///< Delay for an FP multiplication
	CycleNo m_divLatency;     ///< Delay for an FP division
	CycleNo m_sqrtLatency;    ///< Delay for an FP square root
};

}
#endif

