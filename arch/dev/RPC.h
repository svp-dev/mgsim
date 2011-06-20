#ifndef RPC_H
#define RPC_H

#include "kernel.h"
#include "storage.h"
#include "IOBus.h"

namespace Simulator 
{
    class IRPCServiceProvider
    {
    public:
        virtual void Service(uint32_t procedure_id,
                             std::vector<uint32_t>& res1, size_t res1_maxsize,
                             std::vector<uint32_t>& res2, size_t res2_maxsize,
                             const std::vector<uint32_t>& arg1, 
                             const std::vector<uint32_t>& arg2, 
                             uint32_t arg3, uint32_t arg4) = 0;

        virtual std::string GetName() = 0;
        virtual ~IRPCServiceProvider() {};
    };
   
    class RPCInterface : public Object, public IIOBusClient
    {
        struct IncomingRequest
        {
            uint32_t                procedure_id;
            uint32_t                extra_arg1;
            uint32_t                extra_arg2;
            IODeviceID              dca_device_id;
            MemAddr                 argres1_base_address;
            MemSize                 argres1_numwords;
            MemAddr                 argres2_base_address;
            MemSize                 argres2_numwords;
            IONotificationChannelID notification_channel_id;
            Integer                 completion_tag;
        };

        struct ProcessRequest
        {
            uint32_t                procedure_id;
            uint32_t                extra_arg1;
            uint32_t                extra_arg2;
            IODeviceID              dca_device_id;
            MemAddr                 argres1_base_address;
            MemAddr                 argres2_base_address;
            IONotificationChannelID notification_channel_id;
            Integer                 completion_tag;
            std::vector<uint32_t>   data1;
            std::vector<uint32_t>   data2;
        };
            
        struct ProcessResponse
        {
            IODeviceID              dca_device_id;
            MemAddr                 argres1_base_address;
            MemAddr                 argres2_base_address;
            IONotificationChannelID notification_channel_id;
            Integer                 completion_tag;
            std::vector<uint32_t>   data1;
            std::vector<uint32_t>   data2;
        };

        struct CompletionNotificationRequest
        {
            IONotificationChannelID notification_channel_id;
            Integer                 completion_tag;
        };

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

        IncomingRequest         m_inputLatch;

        ArgumentFetchState      m_fetchState;
        MemAddr                 m_currentArgumentOffset;
        size_t                  m_numPendingDCAReads;
        std::vector<uint32_t>   m_currentArgData1;
        std::vector<uint32_t>   m_currentArgData2;

        ResponseWritebackState  m_writebackState;
        MemAddr                 m_currentResponseOffset;

        SingleFlag              m_queueEnabled;
        Buffer<IncomingRequest> m_incoming;
        Buffer<ProcessRequest>  m_ready;
        Buffer<ProcessResponse> m_completed;
        Buffer<CompletionNotificationRequest> m_notifications;

        IRPCServiceProvider&    m_provider;

    public:

        RPCInterface(const std::string& name, Object& parent, IIOBus& iobus, IODeviceID devid, Config& config, IRPCServiceProvider& provider);

        Process p_queue;
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
        std::string GetIODeviceName() const { return GetFQN(); }
    };

}


#endif
