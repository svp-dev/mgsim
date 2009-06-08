#ifndef FPU_H
#define FPU_H

#include "buffer.h"
#include <deque>
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
    FPU_OP_SUB,         ///< Subtraction
    FPU_OP_MUL,         ///< Multiplication
    FPU_OP_DIV,         ///< Division
    FPU_OP_SQRT,        ///< Square root
    FPU_NUM_OPS         ///< Number of operations
};

/**
 * @brief Floating Point Unit
 *
 * This component accepts floating point operations, executes them asynchronously and writes them
 * back once calculated. It has several pipelines, assuming every operation of equal delay can be pipelined.
 */
class FPU : public IComponent
{
    /// Represents an FP operation
    struct Operation
    {
	    FPUOperation op;
	    int          size;
	    double       Rav, Rbv;
	    RegAddr      Rc;
    };
    
    /// Represents the result of an FP operation
	struct Result
	{
		RegAddr       address;    ///< Address of destination register of result.
	    RegisterFile* regfile;    ///< The register file this result should go to
		MultiFloat    value;      ///< Resulting value of the operation.
		int           size;       ///< Size of the resulting value.
		unsigned int  state;      ///< Progression through the pipeline.
	};

    /// Represents a pipeline for an FP operation type
	struct Unit
	{
	    std::set<FPUOperation> ops;         ///< The operation types this unit services
	    bool                   pipelined;   ///< Is it a pipeline or a single ex. unit?
	    CycleNo                latency;     ///< The latency of the unit/pipeline
	    std::deque<Result>     slots;       ///< The pipeline slots
	};
	
	typedef std::map<RegisterFile*, Buffer<Operation> > QueueMap;
	
    /**
     * Called when an operation has completed.
     * @param res the result to write back to the register file.
     * @return true if the result could be written back to the register file.
     */
	bool OnCompletion(const Result& res) const;
	
	/**
	 * Called in order to compute the result from a queued operation
	 * @param op    [in] the operation with source information
	 * @param start [in] the cycle number when the FP operation started
	 * @return the result of the source operation
	 */
    Result CalculateResult(const Operation& op) const;
	
	Simulator::Result OnCycleWritePhase(unsigned int stateIndex);

    BufferSize        m_queueSize; ///< Maximum size for FPU input buffers    
	QueueMap          m_queues;    ///< Input queues
	std::vector<Unit> m_units;     ///< The execution units in the FPU

public:
    /// Returns the number of execution units (states that can write back)
    size_t GetNumExecutionUnits() const { return m_units.size(); }

    /**
     * @brief Constructs the FPU.
     * @param parent  reference to the parent object
     * @param kernel  the kernel to manage this FPU
     * @param name    name of the FPU, irrelevant to simulation
     * @param regFile reference to the register file in which to write back results
     * @param config  reference to the configuration data
     */
    FPU(Object* parent, Kernel& kernel, const std::string& name, const Config& config);

    /**
     * @brief Queues an FP operation.
     * @details This function determines the length of the operation and queues the operation in the corresponding
     *      pipeline. When the operation has completed, the result is written back to the register file.
     * @param op      the FP operation to perform
     * @param size    size of the FP operation (4 or 8)
     * @param Rav     first (or only) operand of the operation
     * @param Rbv     second operand of the operation
	 * @param regfile reference to the register file in which to write back the result
     * @param Rc      address of the destination register(s)
     * @return true if the operation could be queued.
     */
	bool QueueOperation(FPUOperation op, int size, double Rav, double Rbv, RegisterFile& regfile, const RegAddr& Rc);
};

}
#endif

