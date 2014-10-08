// -*- c++ -*-
#ifndef DISPLAY_H
#define DISPLAY_H

#include <arch/IOBus.h>
#include <sim/kernel.h>
#include <sim/storage.h>
#include <sim/config.h>
#include <sim/sampling.h>

struct SDL_Surface;


namespace Simulator
{
    class Display : public Object
    {
        class ControlInterface;

        class FrameBufferInterface : public IIOBusClient, public Object
        {
            IIOBus&         m_iobus;
            IODeviceID      m_devid;

            friend class ControlInterface;

            Display&        GetDisplay() { return *dynamic_cast<Display*>(GetParent()); }

        public:
            FrameBufferInterface(const std::string& name, Display& parent, IIOBus& iobus, IODeviceID devid);

            friend class Display;

            bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
            bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
            void GetDeviceIdentity(IODeviceIdentification& id) const;

            const std::string& GetIODeviceName() const;
        };

        class ControlInterface : public IIOBusClient, public Object
        {
            IIOBus&         m_iobus;
            std::vector<uint32_t> m_control;
            IODeviceID      m_devid;
            unsigned        m_key;

            Display&        GetDisplay() { return *dynamic_cast<Display*>(GetParent()); }

        public:
            ControlInterface(const std::string& name, Display& parent, IIOBus& iobus, IODeviceID devid);

            friend class Display;

            bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
            bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
            void GetDeviceIdentity(IODeviceIdentification& id) const;

            const std::string& GetIODeviceName() const;
        };

        ControlInterface      m_ctlinterface;
        FrameBufferInterface  m_fbinterface;
        static Display*       m_singleton;
        SDL_Surface*          m_screen;
        unsigned int          m_max_screen_h;
        unsigned int          m_max_screen_w;
        CycleNo               m_lastUpdate;

        std::vector<uint8_t>  m_framebuffer;
        std::vector<uint32_t> m_palette;

        DefineStateVariable(bool, enabled);
        DefineStateVariable(bool, indexed);
        DefineStateVariable(unsigned int, bpp); /* 8, 16, 24, 32 */
        DefineStateVariable(unsigned int, width);
        DefineStateVariable(unsigned int, height);
        DefineStateVariable(float, scalex_orig);
        DefineStateVariable(float, scalex);
        DefineStateVariable(float, scaley_orig);
        DefineStateVariable(float, scaley);
        DefineStateVariable(unsigned int, refreshDelay_orig);
        DefineStateVariable(unsigned int, refreshDelay);



        friend class ControlInterface;
        friend class FrameBufferInterface;

    public:
        Display(const std::string& name, Object& parent, IIOBus& iobus, IODeviceID ctldevid, IODeviceID fbdevid);

        Display(const Display&) = delete;
        Display& operator=(const Display&) = delete;

        ~Display();

        void CheckEvents(void);
        void OnCycle(CycleNo cycle)
        {
            if (m_lastUpdate + m_refreshDelay > cycle)
                return;

            m_lastUpdate = cycle;
            CheckEvents();
        }

        static Display* GetDisplay() { return m_singleton; }

    protected:
        void Resize(unsigned w, unsigned h, bool erase);
        void Refresh() const;
        void ResetCaption() const;
        void ResizeScreen(unsigned int w, unsigned int h);
        void DumpFrameBuffer(unsigned key, int stream, bool gen_timestamp) const;
    };


}

#endif
