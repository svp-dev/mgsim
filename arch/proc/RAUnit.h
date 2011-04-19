#ifndef RAUNIT_H
#define RAUNIT_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class RAUnit : public Object
{
    friend class RegisterFile;
    
public:
    typedef std::vector<std::pair<RegSize, LFID> > List;
    typedef RegSize  BlockSize;
    typedef RegIndex BlockIndex;

    RAUnit(const std::string& name, Processor& parent, Clock& clock, const RegisterFile& regFile, const Config& config);

    /**
     * \brief Allocates registers
     * \param size[in]     Number of registers to allocate for each register type.
     * \param fid[in]      Family that makes the request. For debugging purposes only.
     * \param reserved[in] Whether we're allocating a reserved context at least.
     * \param indices[out] Array that will receive the base indices of the allocated registers.
     * \return false if not enough register were available
     * \details A context can be reserved with ReserveContext. Allocating registers with reserved false will
     *      only allocate the registers if all reserved contexts can remain available. If reserved is true,
     *      at least one reserved context worth of register is considered for allocation as well.
     */
    bool Alloc(const RegSize size[NUM_REG_TYPES], LFID fid, ContextType context, RegIndex indices[NUM_REG_TYPES]);
    
    /**
     * \brief Frees the allocated registers
     * \param indices[in] the base address of the allocate registers, as returned from Alloc.
     */
    void Free(RegIndex indices[NUM_REG_TYPES], ContextType context);
    
    /// Returns the maximum number of contexts still available
    BlockSize GetNumFreeContexts(ContextType type) const;
    
    /// Reserves a context for future allocation
    void ReserveContext();

    /// Unreserves a reserved context
    void UnreserveContext();

    // Interaction functions
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    struct TypeInfo
    {
        List      list;                     ///< The list of blocks for administration
        RegSize   blockSize;                ///< Blocksize for this register type
        BlockSize free[NUM_CONTEXT_TYPES];  ///< Number of free blocks
    };
    TypeInfo m_types[NUM_REG_TYPES];
};

#endif

