#include <cstring>
#include "sim/config.h"
#include <arch/dev/RPC.h>

/*

MDS = metadata service
ODS = object data service

The RPC device is pipelined.


The input to the pipeline is a tuple:
- device ID for DCA (ID of requestor); channel ID for completion notification
- procedure ID
- completion writeback (payload in the completion notification)
- address in memory where the arguments are to be fetched from
- memory size for the argument data
- address in memory where the results are to be stored

The completion writeback and memory addresses are passive registers;
the command is issued with their current value when the "command
action register" is written to.

Internally; for each command issued:

- the argument data is fetched from the I/O core's memory via DCA
- the command is performed
- the results are sent back via DCA
- the notification is sent back.

The amount of data transferred from the I/O core's memory is
predefined in the command; but the internal format is dependent on the
command type (more below; expect something RPC-ish).

Queues:
- incoming queue: commands issued via the I/O port
- ready queue: commands after the argument data has been fetched
- completed queue: results that need to be communicated back to memory
- notification queue: for completion notifications

The following processes run concurrently (pipeline):

- argument fetch: issues DCA reead requests until the argument for the
  front incoming request has been read, then deactivate until the last read
  completes; then queue the complete reauest to the processing queue;
  then pop the front incoming request.

- processing: reads in requests from the processing queue; dispatches
  the behavior, then queues the result to the completion queue.

- result writeback: issues DCA write reauests until the result data
  has been sent to memory; then send a read request of 0 as a memory barrier;
  then deactivate until the MB completes; then pop the front incoming request.

- notifications:  issues completion notification for the frontmost
  notification entry. Then pop the entry.

  - read request handler:
    informs the latches for all parameters except command type
    informs the current size of the queues for monitoring
    informs the current state of the processes for monitoring

  - write reauest handler:
    stores values for the parameters into latches
    initiates commands by enqueuing into the incoming queue

  - read response handler:

    if the size is greater than 0; then populate the argument data for
    the frontmost entry in the incoming queue.  If there are no pending read
    requests; then wake up the argument fetch process

    if the size is 0, this is the end of a memory barrier; flag the
    frontmost entry in the completion queue. (will be picked up by the
    result writeback process)

*/

// FIXME: because the input buffer has a fixed size, why use DCA? the
// issuing core could be in charge of populating the input. -- NO: DCA
// has more throughput! entire cache lines transferred at once.

// FIXME: completion n otifications are sent to all I/O cores and
// are queued on each core: all cores not interested in a given
// channel must have a thread that flushes the queue, otherwise
// the I/O network will stall.

namespace Simulator
{

    RPCInterface::RPCInterface(const std::string& name, Object& parent, IIOBus& iobus, IODeviceID devid, IRPCServiceProvider& provider)
        : Object(name, parent),
          m_iobus(iobus),
          m_devid(devid),

          m_lineSize(GetConfOpt("RPCLineSize", size_t, GetTopConf("CacheLineSize", size_t))),

          m_inputLatch(),

          InitStateVariable(fetchState, ARGFETCH_READING1),
          InitStateVariable(currentArgumentOffset, 0),
          InitStateVariable(numPendingDCAReads, 0),
          m_maxArg1Size(GetConf("RPCBufferSize1", size_t)),
          m_maxArg2Size(GetConf("RPCBufferSize2", size_t)),
          m_maxRes1Size(m_maxArg1Size),
          m_maxRes2Size(m_maxArg2Size),
          m_currentArgData1(m_maxArg1Size, 0),
          m_currentArgData2(m_maxArg2Size, 0),

          InitStateVariable(writebackState, RESULTWB_WRITING1),
          InitStateVariable(currentResponseOffset, 0),

          InitStorage(m_queueEnabled, iobus.GetClock(), false),
          InitBuffer(m_incoming, iobus.GetClock(), "RPCIncomingQueueSize"),
          InitBuffer(m_ready, iobus.GetClock(), "RPCReadyQueueSize"),
          InitBuffer(m_completed, iobus.GetClock(), "RPCCompletedQueueSize"),
          InitBuffer(m_notifications, iobus.GetClock(), "RPCNotificationQueueSize"),

          m_provider(provider),

          InitProcess(p_queueRequest, DoQueue),
          InitProcess(p_argumentFetch, DoArgumentFetch),
          InitProcess(p_processRequests, DoProcessRequests),
          InitProcess(p_writeResponse, DoWriteResponse),
          InitProcess(p_sendCompletionNotifications, DoSendCompletionNotifications)
    {
        RegisterStateVariable(m_inputLatch.procedure_id, "inputLatch.pid");
        RegisterStateVariable(m_inputLatch.extra_arg1, "inputLatch.ea1");
        RegisterStateVariable(m_inputLatch.extra_arg2, "inputLatch.ea2");
        RegisterStateVariable(m_inputLatch.dca_device_id, "inputLatch.ddid");
        RegisterStateVariable(m_inputLatch.arg1_base_address, "inputLatch.a1b");
        RegisterStateVariable(m_inputLatch.arg1_size, "inputLatch.a1s");
        RegisterStateVariable(m_inputLatch.arg2_base_address, "inputLatch.a2b");
        RegisterStateVariable(m_inputLatch.arg2_size, "inputLatch.a2s");
        RegisterStateVariable(m_inputLatch.res1_base_address, "inputLatch.r1b");
        RegisterStateVariable(m_inputLatch.res2_base_address, "inputLatch.r2b");
        RegisterStateVariable(m_inputLatch.completion_tag, "inputLatch.ct");
        RegisterStateVariable(m_inputLatch.notification_channel_id, "inputLatch.ncid");

        iobus.RegisterClient(devid, *this);

        m_queueEnabled.Sensitive(p_queueRequest);
        m_incoming.Sensitive(p_argumentFetch);
        m_ready.Sensitive(p_processRequests);
        m_completed.Sensitive(p_writeResponse);
        m_notifications.Sensitive(p_sendCompletionNotifications);

        if (m_lineSize == 0)
        {
            throw exceptf<InvalidArgumentException>(*this, "RPCLineSize cannot be zero");
        }

        p_queueRequest.SetStorageTraces(m_incoming);
        p_argumentFetch.SetStorageTraces(m_iobus.GetReadRequestTraces(m_devid) ^ m_ready);
        p_processRequests.SetStorageTraces(m_completed);
        p_writeResponse.SetStorageTraces(m_iobus.GetWriteRequestTraces() ^ m_iobus.GetReadRequestTraces(m_devid) ^ m_notifications);
        p_sendCompletionNotifications.SetStorageTraces(m_iobus.GetNotificationTraces());
    }

    Result RPCInterface::DoQueue()
    {
        if (!m_incoming.Push(m_inputLatch))
        {
            DeadlockWrite("Unable to push incoming request");
            return FAILED;
        }
        m_queueEnabled.Clear();
        return SUCCESS;
    }

    Result RPCInterface::DoArgumentFetch()
    {
        assert(!m_incoming.Empty());

        const IncomingRequest& req = m_incoming.Front();
        ArgumentFetchState s = m_fetchState;

        switch(s)
        {
        case ARGFETCH_READING1:
        case ARGFETCH_READING2:
        {
            MemAddr voffset, totalsize;

            if (s == ARGFETCH_READING1)
            {
                voffset = req.arg1_base_address + m_currentArgumentOffset;
                totalsize = req.arg1_size;
            }
            else
            {
                voffset = req.arg2_base_address + m_currentArgumentOffset;
                totalsize = req.arg2_size;
            }

            // transfer size:
            // - cannot be greater than the line size
            // - cannot be greated than the number of bytes remaining on the ROM
            // - cannot cause the range [voffset + size] to cross over a line boundary.
            MemSize transfer_size = std::min(std::min((MemSize)(totalsize - m_currentArgumentOffset), (MemSize)m_lineSize),
                                             (MemSize)(m_lineSize - voffset % m_lineSize));

            if (transfer_size > 0)
            {
                if (!m_iobus.SendReadRequest(m_devid, req.dca_device_id, voffset, transfer_size))
                {
                    DeadlockWrite("Unable to send DCA read request for %#016llx/%u to device %u", (unsigned long long)voffset, (unsigned)transfer_size, (unsigned)req.dca_device_id);
                    return FAILED;
                }
                COMMIT { ++m_numPendingDCAReads; }
            }

            COMMIT {
                if (m_currentArgumentOffset + transfer_size < totalsize)
                {
                    m_currentArgumentOffset += transfer_size;
                }
                else if (s == ARGFETCH_READING1)
                {
                    m_currentArgumentOffset = 0;
                    m_fetchState = ARGFETCH_READING2;
                }
                else
                {
                    m_fetchState = ARGFETCH_FINALIZE;
                    if (m_numPendingDCAReads > 0)
                        // wait until the last read completion re-activates
                        // this process.
                        p_argumentFetch.Deactivate();
                }
            }
            return SUCCESS;
        }
        case ARGFETCH_FINALIZE:
        {
            ProcessRequest preq(req.procedure_id,
                                req.extra_arg1,
                                req.extra_arg2,
                                req.dca_device_id,
                                req.res1_base_address,
                                req.res2_base_address,
                                req.completion_tag,
                                req.notification_channel_id);

            COMMIT {
                preq.data1.insert(preq.data1.begin(), m_currentArgData1.begin(), m_currentArgData1.begin() + req.arg1_size);
                preq.data2.insert(preq.data2.begin(), m_currentArgData2.begin(), m_currentArgData2.begin() + req.arg2_size);
            }

            if (!m_ready.Push(preq))
            {
                DeadlockWrite("Unable to push the current request to the ready queue");
                return FAILED;
            }

            COMMIT {
                m_fetchState = ARGFETCH_READING1;
                m_currentArgumentOffset = 0;
                m_numPendingDCAReads = 0;
            }

            m_incoming.Pop();
            return SUCCESS;
        }
        }
        UNREACHABLE;
    }


    Result RPCInterface::DoProcessRequests()
    {
        assert(!m_ready.Empty());

        const ProcessRequest& req = m_ready.Front();

        DebugIOWrite("Processing RPC request from client %u for procedure %u, completion tag %#016llx",
                     (unsigned)req.dca_device_id, (unsigned)req.procedure_id, (unsigned long long)req.completion_tag);

        ProcessResponse res(
            req.dca_device_id,
            req.res1_base_address,
            req.res2_base_address,
            req.notification_channel_id,
            req.completion_tag
            );

        COMMIT {
            m_provider.Service(req.procedure_id,
                               res.data1, m_maxRes1Size,
                               res.data2, m_maxRes2Size,
                               req.data1,
                               req.data2,
                               req.extra_arg1,
                               req.extra_arg2);
        }

        if (!m_completed.Push(res))
        {
            DeadlockWrite("Unable to push request completion");
            return FAILED;
        }
        m_ready.Pop();
        return SUCCESS;
    }

    Result RPCInterface::DoWriteResponse()
    {
        assert(!m_completed.Empty());

        const ProcessResponse& res = m_completed.Front();

        ResponseWritebackState s = m_writebackState;

        // if we are just receiving a result and there is no data, or
        // the response address is set to 0 (ignore), then shortcut to
        // notification.
        if (s == RESULTWB_WRITING1 && (res.data1.size() == 0 || res.res1_base_address == 0))
        {
            s = (res.data2.size() == 0 || res.res2_base_address == 0) ? RESULTWB_FINALIZE : RESULTWB_WRITING2;
        }
        else if (s == RESULTWB_WRITING2 && (res.data2.size() == 0 || res.res2_base_address == 0))
        {
            s = RESULTWB_BARRIER;
        }

        switch(s)
        {
        case RESULTWB_WRITING1:
        case RESULTWB_WRITING2:
        {

            MemAddr voffset;
            MemSize totalsize;
            const char *data;
            if (s == RESULTWB_WRITING1)
            {
                voffset = res.res1_base_address + m_currentResponseOffset;
                totalsize = res.data1.size();
                data = &res.data1[0];
            }
            else
            {
                voffset = res.res2_base_address + m_currentResponseOffset;
                totalsize = res.data2.size();
                data = &res.data2[0];
            }

            // transfer size:
            // - cannot be greater than the line size
            // - cannot be greated than the number of bytes remaining on the ROM
            // - cannot cause the range [voffset + size] to cross over a line boundary.
            size_t transfer_size = std::min(std::min((size_t)(totalsize - m_currentResponseOffset), (size_t)m_lineSize),
                                            (size_t)(m_lineSize - voffset % m_lineSize));

            size_t res_offset = m_currentResponseOffset;

            DebugIOWrite("Sending response data %d for offsets %zu - %zu", (s == RESULTWB_WRITING1) ? 1 : 2, res_offset, res_offset + transfer_size - 1);

            IOData iodata;
            iodata.size = transfer_size;
            COMMIT {
                memcpy(iodata.data, data + res_offset, transfer_size);
            }

            if (!m_iobus.SendWriteRequest(m_devid, res.dca_device_id, voffset, iodata))
            {
                DeadlockWrite("Unable to send DCA write request for %#016llx/%u to device %u", (unsigned long long)voffset, (unsigned)transfer_size, (unsigned)res.dca_device_id);
                return FAILED;
            }

            COMMIT {
                if (m_currentResponseOffset + transfer_size < totalsize)
                {
                    m_currentResponseOffset += transfer_size;
                }
                else if (s == RESULTWB_WRITING1 && (res.data2.size() != 0 && res.res2_base_address != 0))
                {
                    m_currentResponseOffset = 0;
                    m_writebackState = RESULTWB_WRITING2;
                }
                else
                {
                    m_writebackState = RESULTWB_BARRIER;
                }
            }
            return SUCCESS;
        }
        case RESULTWB_BARRIER:
        {
            if (!m_iobus.SendReadRequest(m_devid, res.dca_device_id, 0, 0))
            {
                DeadlockWrite("Unable to send DCA write barrier to device %u", (unsigned)res.dca_device_id);
                return FAILED;
            }

            COMMIT {
                m_writebackState = RESULTWB_FINALIZE;
                // wait until the last read completion re-activates
                // this process.
                p_writeResponse.Deactivate();
            }

            return SUCCESS;
        }
        case RESULTWB_FINALIZE:
        {
            CompletionNotificationRequest creq;
            creq.notification_channel_id = res.notification_channel_id;
            creq.completion_tag = res.completion_tag;

            if (!m_notifications.Push(creq))
            {
                    DeadlockWrite("Unable to push the current result to the notification queue");
                    return FAILED;
            }

            COMMIT {
                m_writebackState = RESULTWB_WRITING1;
                m_currentResponseOffset = 0;
            }

            m_completed.Pop();
            return SUCCESS;
        }
        }
        UNREACHABLE;
    }

    Result RPCInterface::DoSendCompletionNotifications()
    {
        assert(!m_notifications.Empty());

        const CompletionNotificationRequest& req = m_notifications.Front();

        if (!m_iobus.SendNotification(m_devid, req.notification_channel_id, req.completion_tag))
        {
            DeadlockWrite("Unable to send completion notification to channel %u (tag %#016llx)", (unsigned)req.notification_channel_id, (unsigned long long)req.completion_tag);
            return FAILED;
        }

        m_notifications.Pop();
        return SUCCESS;
    }

    bool RPCInterface::OnReadResponseReceived(IODeviceID /*from*/, MemAddr address, const IOData& iodata)
    {
        if (iodata.size == 0)
        {
            // this is a write barrier completion, the writeback process is interested.
            assert(m_writebackState == RESULTWB_FINALIZE);
            COMMIT {
                m_completed.GetClock().ActivateProcess(p_writeResponse);
            }
        }
        else
        {
            // this is a read completion for the argument read.
            assert(!m_incoming.Empty());

            const IncomingRequest& req = m_incoming.Front();

            size_t data_size = iodata.size;

            // Since the two memory areas may overlap, the address
            // range for a given read response can match both. Also,
            // the size of the read response can exceed the size of
            // either argument.

            if (address >= req.arg1_base_address && address < req.arg1_base_address + req.arg1_size)
            {
                size_t arg_offset = address - req.arg1_base_address;

                if (address + data_size >= req.arg1_base_address + req.arg1_size)
                    data_size = req.arg1_base_address + req.arg1_size - address;

                DebugIOWrite("Received argument data 1 for offset %zu - %zu", arg_offset, arg_offset + data_size - 1);
                COMMIT {
                    memcpy(&m_currentArgData1[arg_offset], iodata.data, data_size);
                }
            }

            if (address >= req.arg2_base_address && address < req.arg2_base_address + req.arg2_size)
            {
                size_t arg_offset = address - req.arg2_base_address;

                if (address + data_size >= req.arg2_base_address + req.arg2_size)
                    data_size = req.arg2_base_address + req.arg2_size - address;

                DebugIOWrite("Received argument data 2 for offset %zu - %zu", arg_offset, arg_offset + data_size - 1);
                COMMIT {
                    memcpy(&m_currentArgData2[arg_offset], iodata.data, data_size);
                }
            }


            if (m_fetchState == ARGFETCH_FINALIZE && m_numPendingDCAReads == 1)
            {
                DebugSimWrite("Last read completion, waking up reader process");
                COMMIT { m_incoming.GetClock().ActivateProcess(p_argumentFetch); }
            }

            COMMIT { --m_numPendingDCAReads; }

        }

        return SUCCESS;
    }

    bool RPCInterface::OnWriteRequestReceived(IODeviceID /*from*/, MemAddr address, const IOData& data)
    {
        // word size: 32 bit
        // word 0: command status (0: idle, 1: busy/queueing)
        // word 1: low 16 bits: device ID of DCA peer; high 16 bits: notification channel ID for response
        // word 2: procedure ID
        // word 4: low 32 bits of completion tag
        // word 5: high 32 bits of completion tag
        // word 6: number of words in 1st argument data
        // word 7: number of words in 2nd argument data
        // word 8: low 32 bits of arg 1 base address
        // word 9: high 32 bits of arg 1 base address
        // word 10: low 32 bits of arg 2 base address
        // word 11: high 32 bits of arg 2 base address
        // word 12: extra arg 1
        // word 13: extra arg 2
        // word 14: low 32 bits of res 1 base address
        // word 15: high 32 bits of res 1 base address
        // word 16: low 32 bits of res 2 base address
        // word 17: high 32 bits of res 2 base address

        if (address % 4 != 0 || data.size != 4)
        {
            throw exceptf<>(*this, "Invalid unaligned RPC write: %#016llx (%u)", (unsigned long long)address, (unsigned)data.size);
        }

        unsigned word = address / 4;

        if (word == 3 || word > 17)
        {
            throw exceptf<>(*this, "Invalid RPC write to word: %u", word);
        }

        uint32_t value = UnserializeRegister(RT_INTEGER, data.data, 4);
        COMMIT{
            switch(word)
            {
            case 0: m_queueEnabled.Set(); break;
            case 1:
                m_inputLatch.dca_device_id = value & 0xffff;
                m_inputLatch.notification_channel_id = (value >> 16) & 0xffff;
                break;
            case 2: m_inputLatch.procedure_id = value; break;

            case 4: m_inputLatch.completion_tag      = (m_inputLatch.completion_tag       & ~(MemAddr)0xffffffff) |           (value & 0xffffffff); break;
#if INTEGER_WIDTH > 32
            case 5: m_inputLatch.completion_tag      = (m_inputLatch.completion_tag       &           0xffffffff) | ((Integer)(value & 0xffffffff) << 32); break;
#endif

            case 6: m_inputLatch.arg1_size = std::min((uint32_t)m_maxArg1Size, value); break;
            case 7: m_inputLatch.arg2_size = std::min((uint32_t)m_maxArg1Size, value); break;

            case 8: m_inputLatch.arg1_base_address = (m_inputLatch.arg1_base_address & ~(MemAddr)0xffffffff) |           (value & 0xffffffff); break;
#if MEMSIZE_WIDTH > 32
            case 9: m_inputLatch.arg1_base_address = (m_inputLatch.arg1_base_address &           0xffffffff) | ((MemAddr)(value & 0xffffffff) << 32); break;
#endif

            case 10: m_inputLatch.arg2_base_address = (m_inputLatch.arg2_base_address & ~(MemAddr)0xffffffff) |           (value & 0xffffffff); break;
#if MEMSIZE_WIDTH > 32
            case 11: m_inputLatch.arg2_base_address = (m_inputLatch.arg2_base_address &           0xffffffff) | ((MemAddr)(value & 0xffffffff) << 32); break;
#endif

            case 12: m_inputLatch.extra_arg1 = value; break;
            case 13: m_inputLatch.extra_arg2 = value; break;

            case 14: m_inputLatch.res1_base_address = (m_inputLatch.res1_base_address & ~(MemAddr)0xffffffff) |           (value & 0xffffffff); break;
#if MEMSIZE_WIDTH > 32
            case 15: m_inputLatch.res1_base_address = (m_inputLatch.res1_base_address &           0xffffffff) | ((MemAddr)(value & 0xffffffff) << 32); break;
#endif

            case 16: m_inputLatch.res2_base_address = (m_inputLatch.res2_base_address & ~(MemAddr)0xffffffff) |           (value & 0xffffffff); break;
#if MEMSIZE_WIDTH > 32
            case 17: m_inputLatch.res2_base_address = (m_inputLatch.res2_base_address &           0xffffffff) | ((MemAddr)(value & 0xffffffff) << 32); break;
#endif

            default:
                break;
            }
        }

        return true;
    }

    bool RPCInterface::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        // word size: 32 bit
        // word 0: command status (0: idle, 1: busy/queueing)
        // word 1: low 16 bits: device ID of DCA peer; high 16 bits: notification channel ID for response
        // word 2: procedure ID

        // word 4: low 32 bits of completion tag
        // word 5: high 32 bits of completion tag
        // word 6: number of words in area 1
        // word 7: number of words in area 2
        // word 8: low 32 bits of arg 1 base address
        // word 9: high 32 bits of arg 1 base address
        // word 10: low 32 bits of arg 2 base address
        // word 11: high 32 bits of arg 2 base address
        // word 12: extra arg 1
        // word 13: extra arg 2
        // word 14: low 32 bits of res 1 base address
        // word 15: high 32 bits of res 1 base address
        // word 16: low 32 bits of res 2 base address
        // word 17: high 32 bits of res 2 base address
        //
        // word 64: arg 1 max size
        // word 65: arg 2 max size
        // word 66: res 1 max size
        // word 67: res 2 max size
        // word 68: current incoming queue size
        // word 69: max incoming queue size
        // word 70: current ready queue size
        // word 71: max ready queue size
        // word 72: current completed queue size
        // word 73: max completed queue size
        // word 74: current notification queue size
        // word 75: max notification queue size

        if (address % 4 != 0 || size != 4)
        {
            throw exceptf<>(*this, "Invalid unaligned RPC read: %#016llx (%u)", (unsigned long long)address, (unsigned)size);
        }

        unsigned word = address / 4;

        if (word == 3 || (word > 17 && word < 64) || word > 75)
        {
            throw exceptf<>(*this, "Invalid RPC read from word: %u", word);
        }

        uint32_t value = 0;
        COMMIT{
            switch(word)
            {
            case 0:  value = m_queueEnabled.IsSet(); break;

            case 1:  value = (m_inputLatch.dca_device_id & 0xffff) | ((m_inputLatch.notification_channel_id & 0xffff) << 16); break;
            case 2:  value = m_inputLatch.procedure_id; break;

            case 4: value = (m_inputLatch.completion_tag             ) & 0xffffffff; break;
#if INTEGER_WIDTH > 32
            case 5: value = (m_inputLatch.completion_tag        >> 32) & 0xffffffff; break;
#endif

            case 6:  value = m_inputLatch.arg1_size; break;
            case 7:  value = m_inputLatch.arg2_size; break;

            case 8:  value = (m_inputLatch.arg1_base_address      ) & 0xffffffff; break;
#if MEMSIZE_WIDTH > 32
            case 9:  value = (m_inputLatch.arg1_base_address >> 32) & 0xffffffff; break;
#endif
            case 10:  value = (m_inputLatch.arg2_base_address      ) & 0xffffffff; break;
#if MEMSIZE_WIDTH > 32
            case 11:  value = (m_inputLatch.arg2_base_address >> 32) & 0xffffffff; break;
#endif

            case 12:  value = m_inputLatch.extra_arg1; break;
            case 13:  value = m_inputLatch.extra_arg2; break;

            case 14:  value = (m_inputLatch.res1_base_address      ) & 0xffffffff; break;
#if MEMSIZE_WIDTH > 32
            case 15:  value = (m_inputLatch.res1_base_address >> 32) & 0xffffffff; break;
#endif
            case 16:  value = (m_inputLatch.res2_base_address      ) & 0xffffffff; break;
#if MEMSIZE_WIDTH > 32
            case 17:  value = (m_inputLatch.res2_base_address >> 32) & 0xffffffff; break;
#endif

            case 64: value = m_maxArg1Size; break;
            case 65: value = m_maxArg2Size; break;
            case 66: value = m_maxRes1Size; break;
            case 67: value = m_maxRes2Size; break;
            case 68: value = m_incoming.size(); break;
            case 69: value = m_incoming.GetMaxSize(); break;
            case 70: value = m_ready.size(); break;
            case 71: value = m_ready.GetMaxSize(); break;
            case 72: value = m_completed.size(); break;
            case 73: value = m_completed.GetMaxSize(); break;
            case 74: value = m_notifications.size(); break;
            case 75: value = m_notifications.GetMaxSize(); break;
            }
        }

        IOData iodata;
        SerializeRegister(RT_INTEGER, value, iodata.data, 4);
        iodata.size = 4;

        if (!m_iobus.SendReadResponse(m_devid, from, address, iodata))
        {
            DeadlockWrite("Cannot send RPC read response to I/O bus");
            return false;
        }
        return true;
    }


    void RPCInterface::GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "RPC", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }
    }

    const std::string& RPCInterface::GetIODeviceName() const
    {
        return GetName();
    }

}
