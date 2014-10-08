#include "UART.h"
#include "sim/config.h"
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>

using namespace std;

#undef get_pty_master

#if defined(HAVE_POSIX_TERMIOS) && defined(HAVE_TCGETATTR) && defined(HAVE_TCSETATTR) && \
    defined(HAVE_UNLOCKPT) && defined(HAVE_GRANTPT) && defined(HAVE_PTSNAME)

#include <termios.h>

# if defined(HAVE_POSIX_OPENPT)
#  define get_pty_master() posix_openpt(O_RDWR|O_NOCTTY)
# else
#  if defined(HAVE_GETPT)
#   define get_pty_master() getpt(O_RDWR|O_NOCTTY)
#  else
#   if defined(HAVE_DEV_PTMX)
#     define get_pty_master() open("/dev/ptmx", O_RDWR|O_NOCTTY)
#   endif
#  endif
# endif
#endif

namespace Simulator
{
    // UART related to the NS/PC16650 -  http://www.national.com/ds/PC/PC16550D.pdf
    // Differences with 16650:
    // - FIFO mode is always enabled, cannot reset the FIFO
    // - DMA mode select is not supported/connected
    // - MODEM lines are not supported/connected
    // - transmit speed / divisor latch is not supported

    UART::UART(const string& name, Object& parent, IIOBus& iobus, IODeviceID devid)
        : Object(name, parent),

          m_iobus(iobus),
          m_devid(devid),

          InitStateVariable(hwbuf_in_full, false),
          InitStateVariable(hwbuf_in, 0),
          InitStateVariable(hwbuf_out_full, false),
          InitStateVariable(hwbuf_out, 0),

          InitStorage(m_receiveEnable, iobus.GetClock(), false),
          InitBuffer(m_fifo_in, iobus.GetClock(), "UARTInputFIFOSize"),
          InitProcess(p_Receive, DoReceive),

          InitBuffer(m_fifo_out, iobus.GetClock(), "UARTOutputFIFOSize"),
          InitProcess(p_Transmit, DoTransmit),

          InitStateVariable(write_buffer, 0),

          InitStorage(m_sendEnable, iobus.GetClock(), false),
          InitProcess(p_Send, DoSend),

          m_eof(false),
          m_error_in(0),
          m_error_out(0),

          InitStateVariable(readInterruptEnable, false),

          InitStorage(m_readInterrupt, iobus.GetClock(), false),
          InitProcess(p_ReadInterrupt, DoSendReadInterrupt),
          InitStateVariable(readInterruptChannel, 0),

          InitStateVariable(writeInterruptEnable, false),
          InitStateVariable(writeInterruptThreshold, 1),

          InitStorage(m_writeInterrupt, iobus.GetClock(), false),
          InitProcess(p_WriteInterrupt, DoSendWriteInterrupt),
          InitStateVariable(writeInterruptChannel, 0),

          InitStateVariable(loopback, false),
          InitStateVariable(scratch, 0),

          m_fin_name(),
          m_fout_name(),
          m_fd_in(-1),
          m_fd_out(-1),
          InitStateVariable(enabled, false),

          InitProcess(p_dummy, DoNothing)
    {
        int ein, eout;
        string fin, fout;

        string connectMode = GetConf("UARTConnectMode", string);

        errno = 0;

        if (connectMode == "FILE")
        {
            fin = fout = GetConf("UARTFile", string);
            m_fd_in = m_fd_out = open(fin.c_str(), O_RDWR);
            ein = eout = errno;
        }
        else if (connectMode == "FILEPAIR")
        {
            fin = GetConf("UARTInputFile", string);
            fout = GetConf("UARTOutputFile", string);

            m_fd_in = open(fin.c_str(), O_RDONLY);
            ein = errno;
            errno = 0;
            m_fd_out = open(fout.c_str(), O_WRONLY);
            eout = errno;
        }
        else if (connectMode == "STDIO")
        {
            fin = "<stdin>";
            fout = "<stdout>";
            m_fd_in = STDIN_FILENO;
            m_fd_out = STDOUT_FILENO;
            ein = eout = 0;
        }
#ifdef get_pty_master
        else if (connectMode == "PTY")
        {
            int master_fd;
            char *slave_name;

            // Open master PTY and get name of slave side
            if (-1 == (master_fd = get_pty_master()))
                throw exceptf<>("Unable to obtain pty master (%s)", strerror(errno));
            if (-1 == grantpt(master_fd))
                throw exceptf<>("grantpt: %s", strerror(errno));
            if (-1 == unlockpt(master_fd))
                throw exceptf<>("unlockpt: %s", strerror(errno));
            if (NULL == (slave_name = ptsname(master_fd)))
                throw exceptf<>("ptsname: %s", strerror(errno));

            // configure master side: disable echo, buffering, control flow etc
            struct termios tio;
            if (-1 == tcgetattr(master_fd, &tio))
                throw exceptf<>("tcgetattr: %s", strerror(errno));

            tio.c_iflag &= ~(IXON|IXOFF|ICRNL|INLCR|IGNCR|IMAXBEL|ISTRIP);
            tio.c_iflag |= IGNBRK;
            tio.c_oflag &= ~(OPOST|ONLCR|OCRNL|ONLRET);
            tio.c_lflag &= ~(IEXTEN|ICANON|ECHO|ECHOE|ECHONL|ECHOCTL|ECHOPRT|ECHOKE|ECHOCTL|ISIG);
            tio.c_cc[VMIN] = 1;
            tio.c_cc[VTIME] = 0;

            if (-1 == tcsetattr(master_fd, TCSANOW, &tio))
                throw exceptf<>("tcsetattr: %s", strerror(errno));
            tcflush(master_fd, TCIOFLUSH);

            cerr << GetName() << ": slave tty at " << slave_name << endl;

            fin = fout = "<pty master for " + std::string(slave_name) + ">";
            m_fd_in = m_fd_out = master_fd;
            ein = eout = 0;
        }
#endif
        else
        {
            throw exceptf<InvalidArgumentException>("Invalid UARTConnectMode: %s", connectMode.c_str());
        }

        if (-1 == m_fd_in)
        {
            throw exceptf<InvalidArgumentException>("Unable to open file for input: %s (%s)", fin.c_str(), strerror(ein));
        }
        if (-1 == m_fd_out)
        {
            throw exceptf<InvalidArgumentException>("Unable to open file for output: %s (%s)", fout.c_str(), strerror(eout));
        }

        m_fin_name = fin;
        m_fout_name = fout;
        m_fifo_out.Sensitive(p_Transmit);
        m_receiveEnable.Sensitive(p_Receive);
        m_sendEnable.Sensitive(p_Send);
        m_readInterrupt.Sensitive(p_ReadInterrupt);
        m_writeInterrupt.Sensitive(p_WriteInterrupt);
        m_fifo_in.Sensitive(p_dummy);

        iobus.RegisterClient(devid, *this);

        RegisterModelObject(*this, "uart");
        RegisterModelProperty(*this, "inpfifosz", m_fifo_in.GetMaxSize());
        RegisterModelProperty(*this, "outfifosz", m_fifo_out.GetMaxSize());
    }

    Result UART::DoSendReadInterrupt()
    {
        if (!m_iobus.SendInterruptRequest(m_devid, m_readInterruptChannel))
        {
            DeadlockWrite("Unable to send data ready interrupt to I/O bus");
            return FAILED;
        }
        return SUCCESS;
    }

    Result UART::DoSendWriteInterrupt()
    {
        if (!m_iobus.SendInterruptRequest(m_devid, m_writeInterruptChannel))
        {
            DeadlockWrite("Unable to send underrun interrupt to I/O bus");
            return FAILED;
        }
        return SUCCESS;
    }

    Result UART::DoSend()
    {
        if (!m_fifo_out.Push(m_write_buffer))
        {
            DebugIOWrite("Unable to queue byte from transmit hold register to output FIFO: %#02x", (unsigned)m_write_buffer);
            return FAILED;
        }

        if (m_fifo_out.size() + 1 >= m_writeInterruptThreshold)
            m_writeInterrupt.Clear();

        m_sendEnable.Clear();

        DebugIOWrite("Moved transmit hold register to output FIFO: %#02x", (unsigned)m_write_buffer);

        return SUCCESS;
    }

    Result UART::DoTransmit()
    {
        // here some byte is ready to send, try to
        // push it to outside.

        assert(!m_fifo_out.Empty());

        if (m_loopback)
        {
            if (!m_fifo_in.Push(m_fifo_out.Front()))
            {
                DebugIOWrite("Unable to loop back from output FIFO back to input FIFO");
                return FAILED;
            }
            DebugIOWrite("Loop back of one byte from output FIFO to input FIFO: %#02x", (unsigned)m_fifo_out.Front());
            if (m_readInterruptEnable)
            {
                m_readInterrupt.Set();
            }
        }
        else
        {
            if (m_hwbuf_out_full)
            {
                DebugIOWrite("Output latch busy, unable to send byte from output FIFO to external stream");
                return FAILED;
            }
            m_hwbuf_out = m_fifo_out.Front();
            COMMIT { m_hwbuf_out_full = true; }
            DebugIOWrite("Latching one byte to external stream: %#02x", (unsigned)m_hwbuf_out);
        }

        if (m_writeInterruptEnable && m_fifo_out.size() < m_writeInterruptThreshold + 1)
            m_writeInterrupt.Set();

        m_fifo_out.Pop();

        return SUCCESS;
    }

    Result UART::DoReceive()
    {
        assert(m_hwbuf_in_full == true);

        // here some byte was received from outside, try to queue it
        // to the buffer.

        if (m_loopback)
        {
            // when in loop back mode, input from the external line is
            // blocked.
            return SUCCESS;
        }

        if (!m_fifo_in.Push(m_hwbuf_in))
        {
            DebugIOWrite("Cannot queue incoming byte (%#02x) from input latch to input FIFO", (unsigned)m_hwbuf_in);
            return FAILED;
        }

        if (m_readInterruptEnable)
        {
            m_readInterrupt.Set();
        }

        m_receiveEnable.Clear();
        COMMIT { m_hwbuf_in_full = false; }

        DebugIOWrite("Pushed one byte from input latch to input FIFO: %#02x", (unsigned)m_hwbuf_in);

        return SUCCESS;
    }

    bool UART::OnWriteRequestReceived(IODeviceID from, MemAddr addr, const IOData& iodata)
    {
        if (iodata.size != 1 || addr > 10)
        {
            throw exceptf<>(*this, "Invalid write from device %u to %#016llx/%u", (unsigned)from, (unsigned long long)addr, (unsigned)iodata.size);
        }

        unsigned char data = *(unsigned char*)iodata.data;
        switch(addr)
        {
        case 0: // TX
            COMMIT { m_write_buffer = data; }
            m_sendEnable.Set();
            DebugIOWrite("Copied one byte from device %u to transmit hold register: %#02x", (unsigned)from, (unsigned)data);
            break;

        case 1: // IER
            // bit 0: enable received data available
            // bit 1: enable transmitter holding register empty / FIFO not full
            if (data & 1)
            {
                COMMIT {
                    m_readInterruptEnable = true;
                }
                if (!m_fifo_in.Empty())
                    m_readInterrupt.Set();
            }
            if (data & 2)
            {
                COMMIT {
                    m_writeInterruptEnable = true;
                }
                if (m_fifo_out.size() < m_writeInterruptThreshold + 1)
                    m_writeInterrupt.Set();
            }
            break;

        case 2: // FCR
            // bit 0: FIFO enable
            // bit 1: RCVR FIFO reset
            // bit 2: XMIT FIFO reset
            // bit 3: DMA mode select
            // bit 4: reserved
            // bit 5: reserved
            // bit 6: RCVR trigger (LSB)
            // bit 7: RCVR trigger (MSB)
        {
            unsigned trigger_level = data >> 6;
            COMMIT
            {
                switch(trigger_level)
                {
                case 0: m_writeInterruptThreshold = 1; break;
                case 1: m_writeInterruptThreshold = 4; break;
                case 2: m_writeInterruptThreshold = 8; break;
                case 3: m_writeInterruptThreshold = 14; break;
                }
                DebugIOWrite("Changing write interrupt threshold to %u bytes", (unsigned)m_writeInterruptThreshold);
            }
        }
        break;

        case 4: // MCR
            COMMIT {
                m_loopback = data & 0x10;
            }
            DebugIOWrite("Setting loopback mode to %s", m_loopback ? "enabled" : "disabled");
            break;

        case 7: // scratch
            COMMIT { m_scratch = data; }
            break;

        case 8: // extension
            COMMIT { m_readInterruptChannel = data; }
            break;

        case 9: // extension
            COMMIT { m_writeInterruptChannel = data; }
            break;

        case 10: // extension
            if (!data && m_enabled)
            {
                DebugIOWrite("De-activating the UART");
                m_writeInterrupt.Clear();
                m_readInterrupt.Clear();
                COMMIT {
                    Selector::GetSelector().UnregisterStream(m_fd_in);
                    if (m_fd_in != m_fd_out)
                        Selector::GetSelector().UnregisterStream(m_fd_out);
                    m_enabled = false;
                }
            }
            else if (data && !m_enabled)
            {
                DebugIOWrite("Activating the UART");
                COMMIT {
                    Selector::GetSelector().RegisterStream(m_fd_in, *this);
                    if (m_fd_in != m_fd_out)
                        Selector::GetSelector().RegisterStream(m_fd_out, *this);
                    m_enabled = true;
                }
            }
            break;


        case 3: // LCR
        case 5: // LSR
        case 6: // MSR
            DebugIOWrite("Ignoring unsupported write to UART register %u", (unsigned)addr);
            break;

        }

        return true;
    }


    bool UART::OnReadRequestReceived(IODeviceID from, MemAddr addr, MemSize size)
    {
        if (size != 1 || addr > 10)
        {
            throw exceptf<>(*this, "Invalid read from device %u to %#016llx/%u", (unsigned)from, (unsigned long long)addr, (unsigned)size);
        }

        unsigned char data = 0;

        switch(addr)
        {
        case 0: // RX

            // is there a byte available?
            if (!m_fifo_in.Empty())
            {
                // are we extracting the last byte available?
                if (m_fifo_in.size() == 1)
                {
                    // yes: acknowledge interrupt.
                    m_readInterrupt.Clear();
                }

                // get the byte from the FIFO
                data = m_fifo_in.Front();
                m_fifo_in.Pop();

                DebugIOWrite("Extracted one byte from input FIFO for device %u", (unsigned)from);
            }
            break;

        case 1: // IER
            // bit 0: enable received data available
            // bit 1: enable transmitter holding register empty / FIFO not full
            data = m_readInterruptEnable | m_writeInterruptEnable << 1;
            break;

        case 2: // IIR
            // bit 0: interrupt pending (inverted)
            // bit 1-3: interrupt ID
            // bit 67: FIFO enabled
        {
            bool data_available = m_readInterrupt.IsSet();
            bool write_underrun = m_writeInterrupt.IsSet();

            data = !(data_available | write_underrun);
            if (data_available)
            {
                // DR has priority over THRE
                data |= 1<<2;
            }
            else
                if (write_underrun)
                {
                    data |= 1<<1;

                    // reading the IIR also stops the THRE interrupts
                    // if it was the interrupt source
                    m_writeInterrupt.Clear();
                }

            data |= (3 << 6); // FIFO enabled
        }
        break;

        case 3: // LCR
            DebugIOWrite("Faking read to LCR (unsupported)");
            // bit 0-1: word length (1 1 = 8 bits)
            // bit 2: number of stop bits
            // bit 3: parity enable
            // bit 4: even parity select
            // bit 5: stick parity
            // bit 6: set break
            // bit 7: divisor latch acess bit
            data = 3; // word length 8 bits, all other unset
            break;

        case 4: // MCR
            // bit 0: DTR
            // bit 1: RTS
            // bit 2: out1
            // bit 3: out2
            // bit 4: loopback
            data = m_loopback << 4;
            break;

        case 5: // LSR
            // bit 0: data ready
            // bit 1: overrun error
            // bit 2: parity error
            // bit 3: framing error
            // bit 4: break interrupt
            // bit 5: transmitter holding register empty (THRE)
            // bit 6: transmitter empty (TEMT)
            // bit 7: FIFO RECV error
            data = (!m_fifo_in.Empty()) | (!m_sendEnable.IsSet() << 5) | (m_fifo_out.Empty() << 6);
            break;

        case 6: // MSR
            DebugIOWrite("Faking read to MSR (unsupported)");
            // bit 0: DCTS
            // bit 1: DDSR
            // bit 2: TERI
            // bit 3: DDCD
            // bit 4: CTS
            // bit 5: DSR
            // bit 6: RI
            // bit 7: DCD
            break;

        case 7: // scratch
            data = m_scratch;
            break;

        case 8: // extension
            data = m_readInterruptChannel;
            break;

        case 9: // extension
            data = m_writeInterruptChannel;
            break;

        case 10: // extension
            data = m_enabled;
            break;
        }

        IOData iodata;
        iodata.size = 1;
        iodata.data[0] = (char)data;

        if (!m_iobus.SendReadResponse(m_devid, from, addr, iodata))
        {
            DeadlockWrite("Cannot send UART read response to I/O bus");
            return false;
        }
        return true;
    }


    bool UART::OnStreamReady(int fd, Selector::StreamState state)
    {
        // fprintf(stderr, "External fd %d is ready for I/O (state %d) in %d out %d\n", fd, (int)state, m_fd_in, m_fd_out);

        if (fd == m_fd_in && (state & Selector::READABLE))
        {
            // fprintf(stderr, "External fd %d is readable\n", fd);
            if (m_hwbuf_in_full)
            {
                DeadlockWrite("Cannot acquire byte, input latch busy");
            }
            else
            {
                ssize_t res = read(fd, &m_hwbuf_in, 1);
                if (res == 0)
                {
                    m_eof = true;
                    DebugIOWrite("Detected EOF condition on fd %d", fd);
                }
                else if (res < 0)
                {
                    // we might get spurious availability events. Only
                    // catch an error if this is one.
                    if (errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK)
                    {
                        m_error_in = errno;
                        DebugIOWrite("error in read(): %s", strerror(errno));
                    }
                }
                else
                {
                    assert(res == 1);
                    m_hwbuf_in_full = true;
                    m_receiveEnable.Set();
                    DebugIOWrite("Acquired one byte from fd %d to input latch: %#02x", fd, (unsigned)m_hwbuf_in);
                }
            }
        }

        if (fd == m_fd_out && (state & Selector::WRITABLE))
        {
            if (m_hwbuf_out_full)
            {
                ssize_t res = write(fd, &m_hwbuf_out, 1);
                if (res < 0)
                {
                    // we might get spurious availability events. Only
                    // catch an error if this is one.
                    if (errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK)
                    {
                        m_error_out = errno;
                        DebugIOWrite("error in write(): %s", strerror(errno));
                    }
                }
                else
                {
                    assert(res == 1);
                    DebugIOWrite("Sent one byte from output latch to fd %d: %#02x", fd, (unsigned)m_hwbuf_out);
                    m_hwbuf_out_full = false;
                }
            }
            else
            {
                /* nothing to do */
                /* DebugIOWrite("Output latch empty, nothing to send"); */
            }
        }
        return true;
    }

    string UART::GetSelectorClientName() const
    {
        return GetName();
    }


    void UART::Cmd_Info(ostream& out, const vector<string>& /*args*/) const
    {
        out << "The Universal Asynchronous Receiveir-Transmitter is a serial device." << endl
            << endl
            << "Enabled: " << (m_enabled ? "yes" : "no") << endl
            << "Input file: " << m_fin_name << endl
            << "Output file: " << m_fout_name << endl
            << "Transmit hold register: " << (m_sendEnable.IsSet() ? "full" : "empty") << endl
            << "Read interrupt enable: " << (m_readInterruptEnable ? "yes" : "no") << endl
            << "Read interrupt active: " << (m_readInterrupt.IsSet() ? "yes" : "no") << endl
            << "Read interrupt channel: " << m_readInterruptChannel << endl
            << "Write interrupt enable: " << (m_writeInterruptEnable ? "yes" : "no") << endl
            << "Write interrupt FIFO threshold: " << m_writeInterruptThreshold << " bytes" << endl
            << "Write interrupt active: " << (m_writeInterrupt.IsSet() ? "yes" : "no") << endl
            << "Write interrupt channel: " << m_writeInterruptChannel << endl
            << "Loopback mode: " << (m_loopback ? "enabled" : "disabled") << endl
            << endl
            << "Stream output latch: " << (m_hwbuf_out_full ? "full" : "empty") << endl
            << "Stream input latch: " << (m_hwbuf_in_full ? "full" : "empty") << endl
            << "Input error condition: " << (m_error_in ? strerror(m_error_in) : "(no error)") << endl
            << "Output error condition: " << (m_error_out ? strerror(m_error_out) : "(no error)")<< endl
            << "End-of-file reached: " << (m_eof ? "yes" : "no") << endl;
    }

    static
    ostream& obyte(ostream& out, unsigned char byte)
    {
        out << "0x" << setw(2) << setfill('0') << right << hex << (unsigned)byte << left << ' ';
        if (isprint(byte) && byte < 128)
            out << '\'' << (char)byte << '\'';
        else
            out << '?';
        return out;
    }

    void UART::Cmd_Read(ostream& out, const vector<string>& /*args*/) const
    {
        out << "Scratch register: ";
        obyte(out, m_scratch);
        out << endl
            << "Transmit hold register: ";
        if (m_sendEnable.IsSet()) obyte(out, m_write_buffer);
        else out << "Empty";
        out << endl;

        out << "Stream input latch: ";
        if (m_hwbuf_in_full) obyte(out, m_hwbuf_in);
        else out << "Empty";
        out << endl;

        out << "Stream output latch: ";
        if (m_hwbuf_out_full) obyte(out, m_hwbuf_out);
        else out << "Empty";
        out << endl;

        out << "XMIT FIFO: " << endl;
        if (m_fifo_out.Empty())
        {
            out << "Empty" << endl;
        }
        else
        {
            for (auto p = m_fifo_out.begin(); p != m_fifo_out.end(); )
            {
                obyte(out, *p);
                if (++p != m_fifo_out.end())
                {
                    out << "; ";
                }
            }
            out << endl;
        }

        out << "RCVD FIFO: " << endl;
        if (m_fifo_in.Empty())
        {
            out << "Empty" << endl;
        }
        else
        {
            for (auto p = m_fifo_in.begin(); p != m_fifo_in.end(); )
            {
                obyte(out, *p);
                if (++p != m_fifo_in.end())
                {
                    out << "; ";
                }
            }
            out << endl;
        }

    }

    void UART::GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "UART", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }
    }

    const string& UART::GetIODeviceName() const
    {
        return GetName();
    }



}
