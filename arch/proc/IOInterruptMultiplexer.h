#ifndef IOINTMUX_H
#define IOINTMUX_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class IOBusInterface;

class IOInterruptMultiplexer : public Object
{
private:
    RegisterFile&                   m_regFile;

    std::vector<Register<RegAddr>*> m_writebacks;
    std::vector<SingleFlag*>        m_interrupts;

    size_t                          m_lastNotified;

public:
    IOInterruptMultiplexer(const std::string& name, Object& parent, Clock& clock, RegisterFile& rf, size_t numInterrupts);

    // sent by device select upon an I/O read from the processor
    bool SetWriteBackAddress(IOInterruptID which, const RegAddr& addr);

    // triggered by the IOBusInterface
    bool OnInterruptRequestReceived(IOInterruptID which);

    Process p_IncomingInterrupts;
    
    // upon interrupt received
    Result DoReceivedInterrupts();
};


#endif
