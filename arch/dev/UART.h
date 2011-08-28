#ifndef UART_H
#define UART_H

#include "arch/IOBus.h"
#include "sim/kernel.h"
#include "sim/storage.h"
#include "sim/inspect.h"
#include "Selector.h"

class Config;

namespace Simulator
{
    
    class UART : public IIOBusClient, public ISelectorClient, public Object, public Inspect::Interface<Inspect::Info|Inspect::Read>
    {
        IIOBus& m_iobus;
        IODeviceID m_devid;

        bool m_hwbuf_in_full;
        unsigned char m_hwbuf_in;

        bool m_hwbuf_out_full;
        unsigned char m_hwbuf_out;

        SingleFlag m_receiveEnable;
        Buffer<unsigned char> m_fifo_in;
        Process p_Receive;
        Result DoReceive();

        Buffer<unsigned char> m_fifo_out;
        Process p_Transmit;
        Result DoTransmit();
    
        unsigned char m_write_buffer;

        SingleFlag m_sendEnable;
        Process p_Send;
        Result DoSend();

        bool m_eof;
        int m_error_in;
        int m_error_out;

        bool m_readInterruptEnable;

        SingleFlag m_readInterrupt;
        Process p_ReadInterrupt;
        IONotificationChannelID m_readInterruptChannel;
        Result DoSendReadInterrupt();

        bool m_writeInterruptEnable;
        size_t m_writeInterruptThreshold;

        SingleFlag m_writeInterrupt;
        Process p_WriteInterrupt;
        IONotificationChannelID m_writeInterruptChannel;
        Result DoSendWriteInterrupt();

        bool m_loopback;
        unsigned char m_scratch;

        std::string m_fin_name;
        std::string m_fout_name;
        int m_fd_in;
        int m_fd_out;
        bool m_enabled;

        Process p_dummy;
        Result DoNothing() { p_dummy.Deactivate(); return SUCCESS; }

    public:
        UART(const std::string& name, Object& parent, IIOBus& iobus, IODeviceID devid, Config& config);


        // from IIOBusClient
        bool OnReadRequestReceived(IODeviceID from, MemAddr addr, MemSize size);
        bool OnWriteRequestReceived(IODeviceID from, MemAddr addr, const IOData& data);
        void GetDeviceIdentity(IODeviceIdentification& id) const;
        std::string GetIODeviceName() const;

        // From ISelectorClient
        bool OnStreamReady(int fd, Selector::StreamState state);
        std::string GetSelectorClientName() const;

        /* debug */
        void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
        void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;

    };

}

#endif
