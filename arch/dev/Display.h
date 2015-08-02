// -*- c++ -*-
#ifndef DISPLAY_H
#define DISPLAY_H

#include <arch/IOMessageInterface.h>
#include <sim/kernel.h>
#include <sim/storage.h>
#include <sim/config.h>
#include <sim/sampling.h>

namespace Simulator
{
    struct SDLContext;
    class Display : public Object
    {
        class ControlInterface;

        class FrameBufferInterface : public IIOMessageClient, public Object
        {
            IOMessageInterface& m_ioif;
            IODeviceID          m_devid;

            friend class ControlInterface;

            Display&        GetDisplay() { return *dynamic_cast<Display*>(GetParent()); }

        public:
            FrameBufferInterface(const std::string& name, Display& parent,
                                 IOMessageInterface& ioif, IODeviceID devid);

            friend class Display;

            bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size) override;
            StorageTraceSet GetReadRequestTraces() const override;
            bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data) override;
            StorageTraceSet GetWriteRequestTraces() const override;
            void GetDeviceIdentity(IODeviceIdentification& id) const override;

            const std::string& GetIODeviceName() const override;
        };

        class ControlInterface : public IIOMessageClient, public Object
        {
            IOMessageInterface&   m_ioif;
            std::vector<uint32_t> m_control;
            IODeviceID      m_devid;
            DefineStateVariable(unsigned, key);

            Display&        GetDisplay() { return *dynamic_cast<Display*>(GetParent()); }

        public:
            ControlInterface(const std::string& name, Display& parent,
                             IOMessageInterface& ioif, IODeviceID devid);

            friend class Display;

            bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size) override;
            StorageTraceSet GetReadRequestTraces() const override;
            bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data) override;
            StorageTraceSet GetWriteRequestTraces() const override;
            void GetDeviceIdentity(IODeviceIdentification& id) const override;

            const std::string& GetIODeviceName() const override;
        };

        ControlInterface      m_ctlinterface;
        FrameBufferInterface  m_fbinterface;
        unsigned int          m_max_screen_h;
        unsigned int          m_max_screen_w;

        bool                  m_video_memory_updated;
        bool                  m_logical_screen_resized;
        bool                  m_logical_screen_updated;
        bool                  m_window_resized;

        std::vector<uint8_t>  m_video_memory;
        std::vector<uint32_t> m_logical_screen_pixels;

        bool                  m_sdl_enabled;
        SDLContext*           m_sdl_context;

        DefineStateVariable(unsigned int, logical_width);
        DefineStateVariable(unsigned int, logical_height);
        DefineStateVariable(uint32_t, command_offset);
        DefineStateVariable(float, scalex);
        DefineStateVariable(float, scaley);

        friend class ControlInterface;
        friend class FrameBufferInterface;

    public:
        Display(const std::string& name, Object& parent,
                IOMessageInterface& iobus,
                IODeviceID ctldevid, IODeviceID fbdevid);

        Display(const Display&) = delete;
        Display& operator=(const Display&) = delete;

        ~Display();

    protected:
        // Resize the logical screen and its framebuffer
        // (m_logical_screen_pixels) The logical screen defines the
        // coordinate system for command lists in video memory.
        void ResizeLogicalScreen(unsigned w, unsigned h, bool erase);
        // Paint m_video_memory into m_logical_screen_pixels.
        void PrepareLogicalScreen();
        // Dump the logical screen pixels to file/stream
        void DumpLogicalScreen(unsigned key, int stream, bool gen_timestamp);

        // Perform the rendering pipeline to the screen
        void Show();

        // Change/update the rendering scaling factor
        // set = true -> change m_scalex/m_scaley to specified factors;
        // set = false -> multiply m_scalex/m_scaly by specified factors.
        void SetWindowScale(double sx, double sy, bool set);
        // ResetRatio: set x/y to the same scale.
        void EqualizeWindowScale();
        // Resize the render window, set m_need_redraw to true, call RenderLogicalScreen.
        void SetWindowSize(unsigned int w, unsigned int h);
        // Change/update the window caption
        void ResetWindowCaption() const;

        // Reset: force re-creating windows, renderer, etc (used by
        // DisplayManager below)
        void ResetDisplay();
        // CloseWindow: delete window and renderer
        void CloseWindow();

        friend class DisplayManager;
    };


    class DisplayManager
    {
    public:
        // RegisterDisplay/UnregisterDisplay: register/unregister a
        // display instance that may be connected to a SDL window and
        // thus receive key or window resize events.
        void RegisterDisplay(Display *disp);
        void UnregisterDisplay(Display *disp);

        // GetMaxWindowSize: retrieve the largest possible resolution
        // for a window.
        void GetMaxWindowSize(unsigned& w, unsigned& h);

        // OnCycle(): only call CheckEvents (which is expensive) every
        // m_refreshDelay cycles.  Called by Kernel::Step().
        void OnCycle(CycleNo cycle)
        {
            if (m_lastUpdate + m_refreshDelay > cycle)
                return;
            m_lastUpdate = cycle;
            CheckEvents();
        }

        // CheckEvents: poll the SDL event queue and dispatch events
        // to the appropriate display. Used by OnCycle and CommandLineReader::ReadLineHook.
        void CheckEvents(void);


        // GetRefreshDelay: accessor for the refresh delay.  Used by
        // individual Displays to generate their window caption.
        unsigned GetRefreshDelay() const { return m_refreshDelay; }

        // IsSDLInitialized: used by displays to decide whether SDL can be used
        bool IsSDLInitialized() const { return m_sdl_initialized; }

        // ResetDisplays: reset all enabled displays; ie re-create
        // their SDL window and render context and redraw.  Should be
        // used after deserializing a simulation state.
        void ResetDisplays() const;

        // Singleton methods
        static void CreateManagerIfNotExists(Config& cfg);
        static DisplayManager* GetManager() { return g_singleton; }

    protected:
        bool                   m_sdl_initialized;    ///< Whether SDL is available
        unsigned               m_refreshDelay_orig; ///< Initial refresh delay from config
        unsigned               m_refreshDelay;      ///< Current refresh delay as set by user
        CycleNo                m_lastUpdate;        ///< Cycle number of last check
        std::vector<Display*>  m_displays;          ///< Currently registered Display instances
        static DisplayManager* g_singleton;         ///< Singleton instance


        // Constructor, used by CreateManagerIfNotExists.
        DisplayManager(unsigned refreshDelay);
    };

}

#endif
