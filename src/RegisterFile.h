#ifndef REGISTERFILE_H
#define REGISTERFILE_H

#include "ports.h"

namespace Simulator
{

class Processor;
class ICache;
class DCache;
class Allocator;

/**
 * @brief Register File with R/W ports.
 *
 * Represents a register file with R/W ports, arbitration and wake-up semantics.
 * The register file has 2 read ports dedicated for the Read stage of the pipeline,
 * one write port from the writeback stage of the pipeline, and one asynchronous
 * read and write port for other components (network, memory, etc).
 */
class RegisterFile : public Structure<RegAddr>
{
public:
    /// Structure for the configuration data
	struct Config
	{
		RegSize numIntegers;    ///< Number of integer registers in the register file
		RegSize numFloats;      ///< Number of floating pointer registers in the register file
	};

    /**
     * Constructs the Register File.
     * @param[in] parent reference to parent processor.
     * @param[in] allocator reference to allocator used to wake up threads on
     *                      writes to waiting registers.
     * @param[in] config reference to the configuration data.
     */
    RegisterFile(Processor& parent, Allocator& allocator, const Config& config);

    /**
     * Reads a register
     * @param[in]  addr the address of the register to read
     * @param[out] data the read data in the register
     * @return true if the register could be read
     */
    bool ReadRegister(const RegAddr& addr, RegValue& data) const;
    
    /**
     * Writes a register
     *
     * Writing a register may in some cases result in the data in the target register being
     * placed in the data parameter. This happens when a waiting state is written back to a
     * register which is full. The resulting state of the parameter will be full, such that
     * the caller can reschedule the thread.
     *
     * @param[in]     addr the address of the register to write
     * @param[in,out] data the data to write to the register. May receive value in register.
     * @param[in]     component the component making the write request. This is used to
     *                arbitrate further requests triggered by the write.
     * @return true if the register could be written
     */
    bool WriteRegister(const RegAddr& addr, RegValue& data, const IComponent& component);
    
    /**
     * Clears a range of registers.
     * @param[in] addr  the address of the first register to clear
     * @param[in] size  the number of consecutive registers to clear
     * @param[in] value the value to clear the registers to
     * @return true if the registers could be cleared
     */
    bool Clear(const RegAddr& addr, RegSize size, const RegValue& value);

	/**
	 * Writes a value into a register unconditionally
	 * This function performs no arbitration and has no wake-up semantics.
	 * @warning this is an admin function, do not use it from the simulation.
	 *
	 * @param[in] addr the address of the register to write
	 * @param[in] data the value to write to the register
	 * @return true if the register could be written (i.e., addr was valid)
	 */
    bool WriteRegister(const RegAddr& addr, const RegValue& data);
    
    /**
     * Returns the number of registers
     * @param[in] type the type of registers whose number should be returned
     * @return the number of registers of the specified type.
     */
    RegSize GetSize(RegType type) const;

    DedicatedReadPort            p_pipelineR1; ///< Read port #1 for the pipeline
    DedicatedReadPort            p_pipelineR2; ///< Read port #2 for the pipeline
    DedicatedWritePort<RegAddr>  p_pipelineW;  ///< Write port for the pipeline
    ArbitratedReadPort           p_asyncR;     ///< Read port for all other components
    ArbitratedWritePort<RegAddr> p_asyncW;     ///< Write port for all other components

private:
    std::vector<RegValue> m_integers; ///< Integer register file
    std::vector<RegValue> m_floats;   ///< Floating point register file
    
    Processor& m_parent;    ///< Reference to parent processor
    Allocator& m_allocator; ///< Reference to the allocator
};

}
#endif

