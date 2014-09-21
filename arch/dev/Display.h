// -*- c++ -*-
#ifndef DISPLAY_H
#define DISPLAY_H

#include <arch/IOBus.h>
#include <sim/kernel.h>
#include <sim/storage.h>
#include <sim/config.h>

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

            std::string GetIODeviceName() const { return GetFQN(); }
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

            std::string GetIODeviceName() const { return GetFQN(); }
        };

        ControlInterface      m_ctlinterface;
        FrameBufferInterface  m_fbinterface;
        static Display*       m_singleton;

        std::vector<uint8_t>  m_framebuffer;
        std::vector<uint32_t> m_palette;
        CycleNo               m_lastUpdate;
        SDL_Surface*          m_screen;
        unsigned int          m_bpp; /* 8, 16, 24, 32 */
        unsigned int          m_width, m_height;
        float                 m_scalex_orig,       m_scalex;
        float                 m_scaley_orig,       m_scaley;
        unsigned int          m_refreshDelay_orig, m_refreshDelay;
        unsigned int          m_max_screen_h, m_max_screen_w;
        bool                  m_indexed;

        bool                  m_enabled;



        friend class ControlInterface;
        friend class FrameBufferInterface;

    public:
        Display(const std::string& name, Object& parent, IIOBus& iobus, IODeviceID ctldevid, IODeviceID fbdevid, Config& config);

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
