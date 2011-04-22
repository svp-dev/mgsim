#ifndef ANCILLARYREGISTERFILE_H
#define ANCILLARYREGISTERFILE_H


#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

/*
 * Access interface to ancillary registers. Ancillary registers
 * differ from regular (computational) registers in that
 * they are few and do not have state bits. 
 * FIXME:
 * This should really be a register file with ports and arbitration etc.
 * Accessed R/W from the memory stage, W from delegation (remote configure)
 */


/* The interface is shared between ancillary core registers and
   performance counters. */
class AncillaryRegisterInterface : public Object
{
public:
    AncillaryRegisterInterface(const std::string& name, Object& parent, Clock& clock)
        : Object(name, parent, clock) {}

    virtual size_t GetNumRegisters() const = 0;
    virtual bool ReadRegister(size_t addr, Integer& data) const = 0;
    virtual bool WriteRegister(size_t addr, Integer data) = 0;
    
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
    

    virtual ~AncillaryRegisterInterface() {}
};

class AncillaryRegisterFile : public AncillaryRegisterInterface
{
    const size_t                  m_numRegisters;
    std::vector<Integer>          m_registers;

public:
    AncillaryRegisterFile(const std::string& name, Processor& parent, Clock& clock, const Config& config);
    
    size_t GetNumRegisters() const { return m_numRegisters; }

    bool ReadRegister(size_t addr, Integer& data) const;
    bool WriteRegister(size_t addr, Integer data);

};


#endif
