#include <fstream>
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <cstring>
#include <cctype>
#include <cassert>
#include "LCD.h"
#include "config.h"
#include "except.h"

namespace Simulator
{

// size_t LCD::GetSize() const { return m_width * m_height + 1; }

LCD::LCD(const std::string& name, Object& parent, IIOBus& iobus, IODeviceID devid, Config& config)
    : Object(name, parent, parent.GetClock()),
      m_devid(devid), m_iobus(iobus),
      m_buffer(0),
      m_width(config.getValue<size_t>(*this, "LCDDisplayWidth")), 
      m_height(config.getValue<size_t>(*this, "LCDDisplayHeight")),
      m_startrow(config.getValue<size_t>(*this, "LCDOutputRow")), 
      m_startcolumn(config.getValue<size_t>(*this, "LCDOutputColumn")),
      m_bgcolor(config.getValue<size_t>(*this, "LCDBackgroundColor") % 10), 
      m_fgcolor(config.getValue<size_t>(*this, "LCDForegroundColor") % 10),
      m_curx(0), 
      m_cury(0),
      m_tracefile(NULL)
{
    if (m_width * m_height == 0)
    {
        throw exceptf<InvalidArgumentException>(*this, "Invalid size specification: %zux%zu", m_width, m_height);
    }
    m_buffer = new char[m_width * m_height];
    memset(m_buffer, ' ', m_width * m_height);

    std::string tfname = config.getValue<std::string>(*this, "LCDTraceFile", "");
    if (!tfname.empty())
    {
        m_tracefile = new std::ofstream;
        m_tracefile->open(tfname.c_str(), std::ios::out | std::ios::app | std::ios::binary);
        if (!m_tracefile->is_open())
        {
            throw exceptf<InvalidArgumentException>(*this, "Cannot open trace file: %s", tfname.c_str());
        }
    }

    iobus.RegisterClient(devid, *this);
}

LCD::~LCD()
{
    delete[] m_buffer;
    if (m_tracefile != NULL)
    {
        m_tracefile->close();
        delete m_tracefile;
    }
}

void LCD::GetDeviceIdentity(IODeviceIdentification& id) const
{
    if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "LCD", id))
    {
        throw InvalidArgumentException(*this, "Device identity not registered");
    }    
}

bool LCD::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
{
    if (address != 0 || size != 4)
    {
        throw exceptf<SimulationException>(*this, "Invalid I/O read to %#016llx/%u", (unsigned long long)address, (unsigned)size);
    }

    uint32_t value = (uint32_t)m_width << 16 | (uint32_t)m_height;

    IOData iodata;
    SerializeRegister(RT_INTEGER, value, iodata.data, 4);
    iodata.size = 4;

    if (!m_iobus.SendReadResponse(m_devid, from, address, iodata))
    {
        DeadlockWrite("Cannot send LCD read response to I/O bus");
        return false;
    }
    return true;
}

bool LCD::OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data)
{
    if (data.size < 1)
        return true;
    
    COMMIT{
        if (address == m_width * m_height)
        {
            /* print with autoscroll */
            char thebyte = ((const char*)data.data)[0];

            if (m_tracefile)
                (*m_tracefile) << thebyte << std::flush;

            if (std::isprint(thebyte))
            {
                m_buffer[m_cury * m_width + m_curx++] = thebyte;
                Refresh(m_cury, m_cury + 1);
            }
            else if (thebyte == '\t')
            {
                for (; m_curx < m_width && (m_curx % 8) != 0; ++m_curx)
                    m_buffer[m_cury * m_width + m_curx] = ' ';
                Refresh(m_cury, m_cury + 1);
            }
            else if (thebyte == '\r')
            {
                m_curx = 0;
            }
            else if (thebyte == '\n')
            {
                memset(m_buffer + m_cury * m_width + m_curx, ' ', m_width - m_curx);
                m_curx = 0;
                ++m_cury;
                if (m_cury < m_height)
                {
                    memset(m_buffer + m_cury * m_width, ' ', m_width);
                    Refresh(m_cury - 1, m_cury + 1);
                }
            }
            else if (thebyte == '\f')
            {
                memset(m_buffer, ' ', m_width * m_height);
                m_curx = m_cury = 0;
                Refresh(0, m_height);
            }
            else if (thebyte == '\b')
            {
                if (m_curx > 0)
                    --m_curx;
            }
            /* if necessary, force newline */
            if (m_curx >= m_width)
            {
                m_curx = 0;
                ++m_cury;
                if (m_cury < m_height)
                {
                    memset(m_buffer + m_cury * m_width, ' ', m_width);
                    Refresh(m_cury - 1, m_cury + 1);
                }
            }
            /* if necessary, scroll up */
            if (m_cury >= m_height)
            {
                memmove(m_buffer, m_buffer + m_width, (m_height - 1) * m_width);
                memset(m_buffer + (m_height - 1) * m_width, ' ', m_width);
                m_cury = m_height - 1;
                Refresh(0, m_height);
            }
            
        }
        else
        {
            if (address + data.size >= (m_width * m_height))
            {
                throw exceptf<SimulationException>(*this, "Invalid I/O write to %#016llx/%u", (unsigned long long)address, (unsigned)data.size);
            }
            for (size_t i = 0; i < data.size; ++i)
            {
                char thebyte = ((const char*)data.data)[i];

                if (m_tracefile)
                    (*m_tracefile) << thebyte << std::flush;

                m_buffer[address + i] = std::isprint(thebyte) ? thebyte : ' ';
                
                DebugIOWrite("LCD output by device %u: %u -> %ux%u",
                             (unsigned)from, (unsigned)thebyte, 
                             (unsigned)(i % m_width + 1), (unsigned)(i / m_width + 1));
            }
            Refresh(address / m_width, (address + data.size) / m_width + 1);
        }
    }
    return true;
}



#define ESC ((char)27)

void LCD::Refresh(unsigned first, unsigned last) const
{
    std::stringstream color;
    color    << ESC << '[' << (m_bgcolor + 40) << ';' << (m_fgcolor + 30) << 'm';
    char save[] = { ESC, '7' };
    char restore[] = { ESC, '8' };

    write(0, save, sizeof(save));
    for (size_t j = first; j < last && j < m_height; ++j)
    {
        std::stringstream pos;
        pos << ESC << '[' << (m_startrow + j) << ';' << m_startcolumn << 'H';
        write(0, pos.str().c_str(), pos.str().size());
        write(0, color.str().c_str(), color.str().size());
        write(0, m_buffer + j * m_width, m_width);
    }
    write(0, restore, sizeof(restore));
}    

}
