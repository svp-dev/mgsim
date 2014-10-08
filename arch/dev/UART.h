// -*- c++ -*-
#ifndef UART_H
#define UART_H

#include "Selector.h"

#include "arch/IOBus.h"
#include "sim/kernel.h"
#include "sim/flag.h"
#include "sim/buffer.h"
#include "sim/inspect.h"

class Config;

namespace Simulator
{

    class UART : public IIOBusClient, public ISelectorClient, public Object, public Inspect::Interface<Inspect::Info|Inspect::Read>
    {
        IIOBus& m_iobus;
        IODeviceID m_devid;

        DefineStateVariable(bool, hwbuf_in_full);
        DefineStateVariable(unsigned char, hwbuf_in);

        DefineStateVariable(bool, hwbuf_out_full);
        DefineStateVariable(unsigned char, hwbuf_out);

        Flag    m_receiveEnable;
        Buffer<unsigned char> m_fifo_in;
        Process p_Receive;
        Result DoReceive();

        Buffer<unsigned char> m_fifo_out;
        Process p_Transmit;
        Result DoTransmit();

        DefineStateVariable(unsigned char, write_buffer);

        Flag    m_sendEnable;
        Process p_Send;
        Result DoSend();

        bool m_eof;
        int m_error_in;
        int m_error_out;

        DefineStateVariable(bool, readInterruptEnable);

        Flag    m_readInterrupt;
        Process p_ReadInterrupt;
        DefineStateVariable(IONotificationChannelID, readInterruptChannel);
        Result DoSendReadInterrupt();

        DefineStateVariable(bool, writeInterruptEnable);
        DefineStateVariable(size_t, writeInterruptThreshold);

        Flag    m_writeInterrupt;
        Process p_WriteInterrupt;
        DefineStateVariable(IONotificationChannelID, writeInterruptChannel);
        Result DoSendWriteInterrupt();

        DefineStateVariable(bool, loopback);
        DefineStateVariable(unsigned char, scratch);

        std::string m_fin_name;
        std::string m_fout_name;
        int m_fd_in;
        int m_fd_out;
        DefineStateVariable(bool, enabled);

        Process p_dummy;
        Result DoNothing() { COMMIT{ p_dummy.Deactivate(); }; return SUCCESS; }

    public:
        UART(const std::string& name, Object& parent, IIOBus& iobus, IODeviceID devid);


        // from IIOBusClient
        bool OnReadRequestReceived(IODeviceID from, MemAddr addr, MemSize size);
        bool OnWriteRequestReceived(IODeviceID from, MemAddr addr, const IOData& data);
        void GetDeviceIdentity(IODeviceIdentification& id) const;
        const std::string& GetIODeviceName() const;

        // From ISelectorClient
        bool OnStreamReady(int fd, Selector::StreamState state);
        std::string GetSelectorClientName() const;

        /* debug */
        void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
        void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;

    };

}

#endif
