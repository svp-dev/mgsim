// -*- c++ -*-
#ifndef UART_H
#define UART_H

#include "Selector.h"

#include "arch/IOMessageInterface.h"
#include "sim/kernel.h"
#include "sim/flag.h"
#include "sim/buffer.h"
#include "sim/inspect.h"

class Config;

namespace Simulator
{

    class UART : public IIOMessageClient, public ISelectorClient,
                 public Object, public Inspect::Interface<Inspect::Info|Inspect::Read>
    {
        IOMessageInterface& m_ioif;
        IODeviceID m_devid;
        Clock& m_clock;

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
        UART(const std::string& name, Object& parent,
             IOMessageInterface& iobus, IODeviceID devid);


        // from IIOBusClient
        bool OnReadRequestReceived(IODeviceID from, MemAddr addr, MemSize size) override;
        bool OnWriteRequestReceived(IODeviceID from, MemAddr addr, const IOData& data) override;
        void GetDeviceIdentity(IODeviceIdentification& id) const override;
        StorageTraceSet GetReadRequestTraces() const override;
        StorageTraceSet GetWriteRequestTraces() const override;
        const std::string& GetIODeviceName() const override;

        // From ISelectorClient
        bool OnStreamReady(int fd, Selector::StreamState state) override;
        std::string GetSelectorClientName() const override;

        /* debug */
        void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const override;
        void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;

    };

}

#endif
