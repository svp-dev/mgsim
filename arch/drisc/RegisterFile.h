#ifndef REGISTERFILE_H
#define REGISTERFILE_H

#ifndef PROCESSOR_H
#error This file should be included in DRISC.h
#endif

/*
 * @brief Register File with R/W ports.
 *
 * Represents a register file with R/W ports, arbitration and wake-up semantics.
 * The register file has 2 read ports dedicated for the Read stage of the pipeline,
 * one write port from the writeback stage of the pipeline, and one asynchronous
 * read and write port for other components (memory, etc).
 */
class RegisterFile : public virtual Structure<RegAddr>, public virtual Storage, public Inspect::Interface<Inspect::Read>
{
public:
    /**
     * Constructs the Register File.
     * @param[in] name name of this register file.
     * @param[in] parent reference to parent processor.
     * @param[in] clock reference to the clock used to control updates.
     * @param[in] allocator reference to allocator used to wake up threads on
     *                      writes to waiting registers.
     * @param[in] config reference to the configuration data.
     */
    RegisterFile(const std::string& name, DRISC& parent, Clock& clock, Allocator& allocator, Config& config);

    /**
     * Reads a register
     * @param[in]  addr the address of the register to read
     * @param[out] data the read data in the register
     * @param[in]  quiet whether to hide debugging messages during the read
     * @return true if the register could be read
     */
    bool ReadRegister(const RegAddr& addr, RegValue& data, bool quiet = false) const;

    /**
     * Writes a register
     *
     * Writing a register may in some cases result in the data in the target register being
     * placed in the data parameter. This happens when a waiting state is written back to a
     * register which is full. The resulting state of the parameter will be full, such that
     * the caller can reschedule the thread.
     *
     * @param[in] addr the address of the register to write
     * @param[in] data the data to write to the register.
     * @param[in] from_memory indicates if the write comes from memory.
     * @param[in] source the source of the write. This is used for arbitration in certain cases.
     * @return true if the register could be written
     */
    bool WriteRegister(const RegAddr& addr, const RegValue& data, bool from_memory);

    /**
     * Clears a range of registers.
     * @param[in] addr  the address of the first register to clear
     * @param[in] size  the number of consecutive registers to clear
     * @param[in] value the value to clear the registers to
     * @return true if the registers could be cleared
     */
    bool Clear(const RegAddr& addr, RegSize size);

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


    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

    Object* GetParent() const { return Structure<RegAddr>::GetParent(); }

    DedicatedReadPort            p_pipelineR1; ///< Read port #1 for the pipeline
    DedicatedReadPort            p_pipelineR2; ///< Read port #2 for the pipeline
    DedicatedWritePort<RegAddr>  p_pipelineW;  ///< Write port for the pipeline
    ArbitratedReadPort           p_asyncR;     ///< Read port for all other components
    ArbitratedWritePort<RegAddr> p_asyncW;     ///< Write port for all other components

private:
    // Applies the queued updates
    void Update();

    std::vector<RegValue>& PickFile(RegType t)
    {
        switch(t)
        {
        case RT_INTEGER: return m_integers;
        case RT_FLOAT:   return m_floats;
        default: UNREACHABLE;
        }
    }
    const std::vector<RegValue>& PickFile(RegType t) const
    {
        switch(t)
        {
        case RT_INTEGER: return m_integers;
        case RT_FLOAT:   return m_floats;
        default: UNREACHABLE;
        }
    }

    std::vector<RegValue> m_integers; ///< Integer register file
    std::vector<RegValue> m_floats;   ///< Floating point register file

    Allocator& m_allocator; ///< Reference to the allocator

    // We can have at most this many number of updates per cycle.
    // This should be equal to the number of write ports.
    static const unsigned int MAX_UPDATES = 2;

    // The queued updates
    std::pair<RegAddr, RegValue> m_updates[MAX_UPDATES];
    unsigned int                 m_nUpdates;

    // Administrative
    std::vector<std::string> m_integer_local_aliases;
    std::vector<std::string> m_float_local_aliases;
    
};

#endif

