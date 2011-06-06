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

typedef size_t ARAddr;

class AncillaryRegisterFile : public Object, public Inspect::Interface<Inspect::Read>
{
    const size_t                  m_numRegisters;
    std::vector<Integer>          m_registers;

public:
    AncillaryRegisterFile(const std::string& name, Processor& parent, Clock& clock, Config& config);
    
    size_t GetNumRegisters() const { return m_numRegisters; }

    Integer ReadRegister(ARAddr addr) const;
    void WriteRegister(ARAddr addr, Integer data);
    
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

};


#endif
