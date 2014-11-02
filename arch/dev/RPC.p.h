// -*- c++ -*-
#ifndef RPC_H
#define RPC_H

#include <vector>
#include <cstdint>

#include <sim/kernel.h>
#include <sim/flag.h>
#include <sim/buffer.h>
#include <arch/IOBus.h>

namespace Simulator
{
    class IRPCServiceProvider
    {
    public:
        virtual void Service(uint32_t procedure_id,
                             std::vector<char>& res1, size_t res1_maxsize,
                             std::vector<char>& res2, size_t res2_maxsize,
                             const std::vector<char>& arg1,
                             const std::vector<char>& arg2,
                             uint32_t arg3, uint32_t arg4) = 0;

        virtual const std::string& GetName() const = 0;
        virtual ~IRPCServiceProvider() {};
    };

    class RPCInterface : public Object, public IIOBusClient
    {
        // {% from "sim/macros.p.h" import gen_struct %}

        // {% call gen_struct() %}
        ((name IncomingRequest)
        (state
          (uint32_t                procedure_id)
          (uint32_t                extra_arg1)
          (uint32_t                extra_arg2)
          (IODeviceID              dca_device_id)
          (MemAddr                 arg1_base_address)
          (MemSize                 arg1_size)
          (MemAddr                 arg2_base_address)
          (MemSize                 arg2_size)
          (MemAddr                 res1_base_address)
          (MemAddr                 res2_base_address)
          (Integer                 completion_tag)
          (IONotificationChannelID notification_channel_id)))
        // {% endcall %}

        // {% call gen_struct() %}
        ((name ProcessRequest)
        (state
         (uint32_t                procedure_id (init 0))
         (uint32_t                extra_arg1 (init 0))
         (uint32_t                extra_arg2 (init 0))
         (IODeviceID              dca_device_id (init 0))
         (MemAddr                 res1_base_address (init 0))
         (MemAddr                 res2_base_address (init 0))
         (Integer                 completion_tag (init 0))
         (std::vector<char>       data1 nocopy (init ""))
         (std::vector<char>       data2 nocopy (init ""))

         (IONotificationChannelID notification_channel_id (init 0))))
        // {% endcall %}

        // {% call gen_struct() %}
        ((name ProcessResponse)
        (state
         (IODeviceID              dca_device_id (init 0))
         (MemAddr                 res1_base_address (init 0))
         (MemAddr                 res2_base_address (init 0))
         (IONotificationChannelID notification_channel_id (init 0))
         (Integer                 completion_tag (init 0))
         (std::vector<char>       data1 nocopy (init ""))
         (std::vector<char>       data2 nocopy (init ""))
            ))
        // {% endcall %}

        // {% call gen_struct() %}
        ((name CompletionNotificationRequest)
        (state
         (IONotificationChannelID notification_channel_id)
         (Integer                 completion_tag)))
        // {% endcall %}

        enum ArgumentFetchState
        {
            ARGFETCH_READING1,
            ARGFETCH_READING2,
            ARGFETCH_FINALIZE,
        };

        enum ResponseWritebackState
        {
            RESULTWB_WRITING1,
            RESULTWB_WRITING2,
            RESULTWB_BARRIER,
            RESULTWB_FINALIZE,
        };


        IIOBus&                 m_iobus;
        IODeviceID              m_devid;

        MemSize                 m_lineSize;

        DefineStateVariable(IncomingRequest, inputLatch);

        DefineStateVariable(ArgumentFetchState, fetchState);
        DefineStateVariable(MemAddr, currentArgumentOffset);
        DefineStateVariable(size_t, numPendingDCAReads);

        size_t                  m_maxArg1Size;
        size_t                  m_maxArg2Size;
        size_t                  m_maxRes1Size;
        size_t                  m_maxRes2Size;
        std::vector<char>       m_currentArgData1;
        std::vector<char>       m_currentArgData2;

        DefineStateVariable(ResponseWritebackState, writebackState);
        DefineStateVariable(MemAddr, currentResponseOffset);

        Flag                    m_queueEnabled;
        Buffer<IncomingRequest> m_incoming;
        Buffer<ProcessRequest>  m_ready;
        Buffer<ProcessResponse> m_completed;
        Buffer<CompletionNotificationRequest> m_notifications;

        IRPCServiceProvider&    m_provider;

    public:

        RPCInterface(const std::string& name, Object& parent, IIOBus& iobus, IODeviceID devid, IRPCServiceProvider& provider);

        Process p_queueRequest;
        Result  DoQueue();

        Process p_argumentFetch;
        Result  DoArgumentFetch();

        Process p_processRequests;
        Result  DoProcessRequests();

        Process p_writeResponse;
        Result  DoWriteResponse();

        Process p_sendCompletionNotifications;
        Result  DoSendCompletionNotifications();

        // from IIOBusClient
        bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
        bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& iodata);
        bool OnReadResponseReceived(IODeviceID from, MemAddr address, const IOData& iodata);

        void GetDeviceIdentity(IODeviceIdentification& id) const;
        const std::string& GetIODeviceName() const;
    };
}


#endif
