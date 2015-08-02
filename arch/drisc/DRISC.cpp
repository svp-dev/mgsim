#include "DRISC.h"
#include <arch/FPU.h>
#include <sim/sampling.h>
#include <sim/log2.h>
#include <sim/config.h>
#include <sim/ctz.h>

#include <cassert>

using namespace std;

namespace Simulator
{

using namespace drisc;

//
// DRISC implementation
//
DRISC::DRISC(const std::string& name, Object& parent, Clock& clock, PID pid, const vector<DRISC*>& grid, BreakPointManager& bp)
:   Object(name, parent),
    m_clock(clock),
    m_bp_manager(bp),
    m_memory(NULL),
    m_memadmin(NULL),
    m_grid(grid),
    m_fpu(NULL),
    m_symtable(NULL),
    m_pid(pid),
    m_reginits(),
    m_bits(),
    m_familyTable ("families",      *this),
    m_threadTable ("threads",       *this),
    m_registerFile("registers",     *this, clock),
    m_raunit      ("rau",           *this, m_registerFile.GetSizes()),
    m_allocator   ("alloc",         *this, clock),
    m_icache      ("icache",        *this, clock),
    m_dcache      ("dcache",        *this, clock),
    m_pipeline    ("pipeline",      *this, clock),
    m_network     ("network",       *this, clock, grid),
    m_mmio        ("mmio",          *this),
    m_apr_file("aprs", *this),
    m_asr_file("asrs", *this),
    m_perfcounters(*this),
    m_lpout("stdout", *this, std::cout),
    m_lperr("stderr", *this, std::cerr),
    m_mmu("mmu", *this),
    m_action("action", *this),
    m_io_if(NULL)
{
    RegisterModelProperty(*this, "pid", (uint32_t)pid);
    RegisterModelProperty(*this, "ic.assoc", (uint32_t)m_icache.GetAssociativity());
    RegisterModelProperty(*this, "ic.lsz", (uint32_t)m_icache.GetLineSize());
    RegisterModelProperty(*this, "dc.assoc", (uint32_t)m_dcache.GetAssociativity());
    RegisterModelProperty(*this, "dc.sets", (uint32_t)m_dcache.GetNumSets());
    RegisterModelProperty(*this, "dc.lines", (uint32_t)m_dcache.GetNumLines());
    RegisterModelProperty(*this, "dc.lsz", (uint32_t)m_dcache.GetLineSize());
    RegisterModelProperty(*this, "threads", (uint32_t)m_threadTable.GetNumThreads());
    RegisterModelProperty(*this, "families", (uint32_t)m_familyTable.GetFamilies().size());
    RegisterModelProperty(*this, "iregs", (uint32_t)m_registerFile.GetSizes()[RT_INTEGER]);
    RegisterModelProperty(*this, "fpregs", (uint32_t)m_registerFile.GetSizes()[RT_FLOAT]);
    RegisterModelProperty(*this, "freq", (uint32_t)clock.GetFrequency());

    // Get the size, in bits, of various identifiers.
    // This is used for packing and unpacking various fields.
    m_bits.pid_bits = ilog2(GetGridSize());
    m_bits.fid_bits = ilog2(m_familyTable.GetFamilies().size());
    m_bits.tid_bits = ilog2(m_threadTable.GetNumThreads());

    // Configure the MMIO interface for the common devices
    m_perfcounters.Connect(m_mmio, IOMatchUnit::READ);
    m_apr_file.Connect(m_mmio, IOMatchUnit::READWRITE);
    m_asr_file.Connect(m_mmio, IOMatchUnit::READ);
    m_lpout.Connect(m_mmio, IOMatchUnit::WRITE);
    m_lperr.Connect(m_mmio, IOMatchUnit::WRITE);
    m_mmu.Connect(m_mmio, IOMatchUnit::WRITE);
    m_action.Connect(m_mmio, IOMatchUnit::WRITE);

    // Check if there is an initial register configuration
    if (!GetConfOpt("InitRegs", string, "").empty())
    {
        auto regs = GetConfStrings("InitRegs");
        for (auto ri : regs)
        {
            // format is RNNN=VAL or FNNN=VAL
            transform(ri.begin(), ri.end(), ri.begin(), ::tolower);
            if (ri[0] != 'r' && ri[0] != 'f')
            {
                throw exceptf<InvalidArgumentException>("Register name not recognized: %s", ri.c_str());
            }
            // find "=" sign
            size_t i = ri.find('=');
            if (i < 2 || i + 1 > ri.size()) // need at least 2 chars before and 1 char after
            {
                throw exceptf<InvalidArgumentException>("Invalid register specifier: %s", ri.c_str());
            }
            string sidx = ri.substr(1, i - 1);
            string value = ri.substr(i + 1);

            char* endptr;
            unsigned long idx = strtoul(sidx.c_str(), &endptr, 0);
            if (*endptr != '\0')
            {
                throw exceptf<InvalidArgumentException>("Invalid register number: %s (%s)", sidx.c_str(), ri.c_str());
            }

            RegAddr reg_addr = MAKE_REGADDR((ri[0] == 'r') ? RT_INTEGER : RT_FLOAT, idx);

            assert(value.size() > 0); // because of check above

            // First handle value indirections
            if (value[0] == '$')
            {
                value = GetTopConf(value.substr(1), string);
            }
            m_reginits[reg_addr] = value;
        }
    }
}

DRISC::~DRISC()
{
    delete m_io_if;
}

void DRISC::ConnectMemory(IMemory* memory, IMemoryAdmin* admin)
{
    m_memory = memory;
    m_memadmin = admin;
    m_symtable = &admin->GetSymbolTable(),
    m_icache.ConnectMemory(memory);
    m_dcache.ConnectMemory(memory);
    if (m_io_if != NULL)
        m_io_if->ConnectMemory(memory);
}

void DRISC::ConnectLink(DRISC* prev, DRISC* next)
{
    m_network.Connect(prev != NULL ? &prev->m_network : NULL, next != NULL ? &next->m_network : NULL);
}

void DRISC::ConnectFPU(FPU* fpu)
{
    assert(fpu != NULL);

    m_fpu = fpu;
    m_pipeline.ConnectFPU(fpu);

    RegisterModelBidiRelation(*this, *fpu, "fpu");
}

void DRISC::ConnectIO(IIOBus* iobus)
{
    assert(iobus != NULL);

    // This processor also supports I/O
    IODeviceID devid = GetConfOpt("DeviceID", IODeviceID, iobus->GetNextAvailableDeviceID());

    m_io_if = new IOInterface("io_if", *this, m_clock, *iobus, devid);

    if (m_memory != NULL)
        m_io_if->ConnectMemory(m_memory);

    MMIOComponent& async_if = m_io_if->GetAsyncIOInterface();
    async_if.Connect(m_mmio, IOMatchUnit::READWRITE);
    MMIOComponent& pnc_if = m_io_if->GetPNCInterface();
    pnc_if.Connect(m_mmio, IOMatchUnit::READWRITE);

    RegisterModelBidiRelation(*iobus, *this, "client", (uint32_t)devid);
}

void DRISC::Initialize()
{
    // First finish initializing the components
    m_network.Initialize();
    if (m_io_if != NULL)
        m_io_if->Initialize();

    //
    // Set port priorities and connections on all components.
    // First source on a port has the highest priority.
    //

    m_icache.p_service.AddProcess(m_icache.p_Incoming);             // Cache-line returns
    m_icache.p_service.AddProcess(m_allocator.p_ThreadActivation);  // Thread activation
    m_icache.p_service.AddProcess(m_allocator.p_FamilyCreate);      // Create process

    // Unfortunately the D-Cache needs priority here because otherwise all cache-lines can
    // remain filled and we get deadlock because the pipeline keeps wanting to do a read.
    m_dcache.p_service.AddProcess(m_dcache.p_ReadResponses);     // Memory read returns
    m_dcache.p_service.AddProcess(m_pipeline.p_Pipeline);         // Memory read/write
    m_dcache.p_service.AddProcess(m_allocator.p_BundleCreate);    // Indirect create read

    m_allocator.p_allocation.AddProcess(m_pipeline.p_Pipeline);         // ALLOCATE instruction
    m_allocator.p_allocation.AddProcess(m_network.p_DelegationIn);      // Delegated non-exclusive create
    m_allocator.p_allocation.AddProcess(m_allocator.p_FamilyAllocate);  // Delayed ALLOCATE instruction

    m_allocator.p_alloc.AddProcess(m_network.p_Link);                   // Place-wide create
    m_allocator.p_alloc.AddProcess(m_allocator.p_FamilyCreate);         // Local creates

    if (m_io_if != NULL)
    {
        IONotificationMultiplexer &nmux = m_io_if->GetNotificationMultiplexer();

        m_allocator.p_readyThreads.AddProcess(nmux.p_IncomingNotifications); // Thread wakeup due to notification
        m_allocator.p_readyThreads.AddProcess(m_io_if->GetReadResponseMultiplexer().p_IncomingReadResponses); // Thread wakeup due to I/O read completion

        for (size_t i = 0; i < nmux.m_services.size(); ++i)
        {
            nmux.m_services[i]->AddProcess(nmux.p_IncomingNotifications);
            nmux.m_services[i]->AddProcess(m_pipeline.p_Pipeline);
        }
    }

    m_allocator.p_readyThreads.AddProcess(m_network.p_Link);                // Thread wakeup due to write
    m_allocator.p_readyThreads.AddProcess(m_network.p_DelegationIn);        // Thread wakeup due to write
    m_allocator.p_readyThreads.AddProcess(m_dcache.p_ReadWritebacks);        // Thread wakeup due to load completion
    m_allocator.p_readyThreads.AddProcess(m_dcache.p_WriteResponses);       // Thread wakeup due to write completion
    if (m_fpu != NULL)
    {
        m_allocator.p_readyThreads.AddProcess(m_fpu->p_Pipeline);                // Thread wakeup due to FP completion
    }
    m_allocator.p_readyThreads.AddProcess(m_allocator.p_ThreadAllocate);    // Thread creation
    m_allocator.p_readyThreads.AddProcess(m_allocator.p_FamilyAllocate);    // Thread wakeup due to family allocation
    m_allocator.p_readyThreads.AddProcess(m_allocator.p_FamilyCreate);      // Thread wakeup due to local create completion

    m_allocator.p_activeThreads.AddProcess(m_icache.p_Incoming);            // Thread activation due to I-Cache line return
    m_allocator.p_activeThreads.AddProcess(m_allocator.p_ThreadActivation); // Thread activation due to I-Cache hit (from Ready Queue)

    if (m_io_if != NULL)
    {
        m_registerFile.p_asyncW.AddProcess(m_io_if->GetNotificationMultiplexer().p_IncomingNotifications); // I/O notifications
        m_registerFile.p_asyncW.AddProcess(m_io_if->GetReadResponseMultiplexer().p_IncomingReadResponses); // I/O read requests
    }

    m_registerFile.p_asyncW.AddProcess(m_network.p_Link);                   // Place register receives
    m_registerFile.p_asyncW.AddProcess(m_network.p_DelegationIn);           // Remote register receives
    m_registerFile.p_asyncW.AddProcess(m_dcache.p_ReadWritebacks);          // Mem Load writebacks

    if (m_fpu != NULL)
    {
        m_registerFile.p_asyncW.AddProcess(m_fpu->p_Pipeline);                   // FPU Op writebacks
    }
    m_registerFile.p_asyncW.AddProcess(m_allocator.p_FamilyCreate);         // Family creation
    m_registerFile.p_asyncW.AddProcess(m_allocator.p_ThreadAllocate);       // Thread allocation


    m_registerFile.p_asyncR.AddProcess(m_network.p_DelegationIn);           // Remote register requests

    m_registerFile.p_pipelineR1.SetProcess(m_pipeline.p_Pipeline);          // Pipeline read stage
    m_registerFile.p_pipelineR2.SetProcess(m_pipeline.p_Pipeline);          // Pipeline read stage

    m_registerFile.p_pipelineW .SetProcess(m_pipeline.p_Pipeline);          // Pipeline writeback stage

    m_network.m_allocResponse.out.AddProcess(m_network.p_AllocResponse);    // Forwarding allocation response
    m_network.m_allocResponse.out.AddProcess(m_allocator.p_FamilyAllocate); // Sending allocation response

    m_network.m_link.out.AddProcess(m_network.p_Link);                      // Forwarding link messages
    m_network.m_link.out.AddProcess(m_network.p_DelegationIn);              // Delegation message forwards onto link
    m_network.m_link.out.AddProcess(m_dcache.p_ReadWritebacks);             // Completed read causes sync
    m_network.m_link.out.AddProcess(m_allocator.p_FamilyAllocate);          // Allocate process sending place-wide allocate
    m_network.m_link.out.AddProcess(m_allocator.p_FamilyCreate);            // Create process sends place-wide create
    m_network.m_link.out.AddProcess(m_allocator.p_ThreadAllocate);          // Thread cleanup causes sync

    m_network.m_delegateIn.AddProcess(m_network.p_Link);                    // Link messages causes remote

    m_network.m_delegateIn.AddProcess(m_dcache.p_ReadWritebacks);           // Read completion causes sync

    m_network.m_delegateIn.AddProcess(m_allocator.p_ThreadAllocate);        // Allocate process completes family sync
    m_network.m_delegateIn.AddProcess(m_allocator.p_FamilyAllocate);        // Allocate process returning FID
    m_network.m_delegateIn.AddProcess(m_allocator.p_BundleCreate);          // Create process returning FID
    m_network.m_delegateIn.AddProcess(m_allocator.p_FamilyCreate);          // Create process returning FID
    m_network.m_delegateIn.AddProcess(m_network.p_AllocResponse);           // Allocate response writing back to parent
    m_network.m_delegateIn.AddProcess(m_pipeline.p_Pipeline);               // Sending local messages
    for (size_t i = 0; i < m_grid.size(); i++)
    {
        // Every core can send delegation messages here
        m_network.m_delegateIn.AddProcess(m_grid[i]->m_network.p_DelegationOut);
    }
    m_network.m_delegateIn.AddProcess(m_network.p_Syncs);             // Family sync goes to delegation

    m_network.m_delegateOut.AddProcess(m_pipeline.p_Pipeline);        // Sending or requesting registers
    m_network.m_delegateOut.AddProcess(m_network.p_DelegationIn);     // Returning registers
    m_network.m_delegateOut.AddProcess(m_network.p_Link);             // Place sync causes final sync

    m_network.m_delegateOut.AddProcess(m_dcache.p_ReadWritebacks);    // Read completion causes sync

    m_network.m_delegateOut.AddProcess(m_network.p_AllocResponse);    // Allocate response writing back to parent
    m_network.m_delegateOut.AddProcess(m_allocator.p_FamilyAllocate); // Allocation process sends FID
    m_network.m_delegateOut.AddProcess(m_allocator.p_BundleCreate);   // Indirect creation sends bundle info
    m_network.m_delegateOut.AddProcess(m_allocator.p_FamilyCreate);   // Create process sends delegated create
    m_network.m_delegateOut.AddProcess(m_allocator.p_ThreadAllocate); // Thread cleanup caused sync
    m_network.m_delegateOut.AddProcess(m_network.p_Syncs);            // Family sync goes to delegation

    //
    // Set possible storage accesses per process.
    //
    // This is used to verify the code in the debug run-time, and can be
    // used to proof deadlock freedom.
    // Each process has a set of storage access traces it can perform.
    // This set is constructed via an expression of storages, where the
    // following operations are defined:
    // A ^ B    multi-choice, appends two sets.
    //          i.e., either traces from set A or B are valid.
    // A * B    sequence, produces a (kind of) cartesian product.
    //          i.e., first a trace from A, then a trace from B.
    // opt(A)   optional. Appends an empty trace.
    //          i.e., any trace from A, or no trace.

    // Anything that is delegated can either go local or remote
#define DELEGATE (m_network.m_delegateIn ^ m_network.m_delegateOut)

    m_allocator.p_ThreadAllocate.SetStorageTraces(
        /* THREADDEP_PREV_CLEANED_UP */ (opt(m_allocator.m_cleanup) *
        /* FAMDEP_THREAD_COUNT */        opt(m_network.m_link.out ^ m_network.m_syncs ^
        /* AllocateThread */                 m_allocator.m_readyThreadsOther)) ^
        /* FAMDEP_ALLOCATION_DONE */    (opt(m_network.m_link.out ^ m_network.m_syncs) *
        /* AllocateThread */             opt(m_allocator.m_readyThreadsOther)) );

    m_allocator.p_FamilyAllocate.SetStorageTraces(
        m_network.m_allocResponse.out ^ m_allocator.m_creates ^ m_network.m_link.out ^ DELEGATE * opt(DELEGATE) );

    m_allocator.p_FamilyCreate.SetStorageTraces(
        /* CREATE_INITIAL */                opt(m_icache.m_outgoing) ^
        /* CREATE_BROADCASTING_CREATE */    opt(m_network.m_link.out) ^
        /* CREATE_ACTIVATING_FAMILY */      m_allocator.m_alloc ^
        /* CREATE_NOTIFY */                 opt(DELEGATE) );

    m_allocator.p_ThreadActivation.SetStorageTraces(
        ( m_allocator.m_readyThreadsPipe ^ m_allocator.m_readyThreadsOther ) * opt(m_allocator.m_activeThreads ^ m_icache.m_outgoing) );

    m_allocator.p_BundleCreate.SetStorageTraces( m_dcache.m_outgoing ^ DELEGATE );

    m_icache.p_Incoming.SetStorageTraces(
        opt(m_allocator.m_activeThreads) );

    // m_icache.p_Outgoing is set in the memory

    m_dcache.p_WriteResponses.SetStorageTraces(
        /* Writes */    opt(m_allocator.m_readyThreadsOther) ^ opt(m_allocator.m_cleanup) );

    m_dcache.p_ReadResponses.SetStorageTraces(opt(m_dcache.m_writebacks));

    m_dcache.p_ReadWritebacks.SetStorageTraces(
        /* Thread wakeup */ opt(m_allocator.m_readyThreadsOther) *
        /* Family sync */   opt(m_network.m_link.out ^ m_network.m_syncs) );

    // m_dcache.p_Outgoing is set in the memory

    StorageTraceSet pls_writeback =
        opt(DELEGATE) *
        opt(m_allocator.m_bundle ^ /* FIXME: is the bundle creation buffer really involved here? */
            (m_allocator.m_readyThreadsPipe * m_allocator.m_cleanup) ^
            m_allocator.m_cleanup ^
            m_allocator.m_readyThreadsPipe);
    StorageTraceSet pls_memory =
        m_dcache.m_outgoing;
    StorageTraceSet pls_fetch =
        m_allocator.m_activeThreads;

    if (m_io_if != NULL)
    {
        /* I/O wait on notification */
        pls_memory ^=
            opt(m_io_if->GetNotificationMultiplexer().GetWriteBackTraces());

        /* I/O reads / writes */
        pls_memory ^=
            opt(m_io_if->GetReadResponseMultiplexer().GetWriteBackTraces()) * /* I/O read, write has no writeback */
            m_io_if->GetIOBusInterface().m_outgoing_reqs *
            /* in the NullIO interface, the transfer occurs in the same cycle, so we have to add the
               receiver traces here too. This is not realistic, and should disappear with the
               implementation of an I/O substrate with appropriate buffering. */
            opt(m_io_if->GetIOBusInterface().GetReadResponseTraces() ^
                m_io_if->GetIOBusInterface().GetInterruptRequestTraces() ^
                m_io_if->GetIOBusInterface().GetNotificationTraces());

    }

    StorageTraceSet pls_execute;
    if (m_fpu != NULL)
        pls_execute = m_fpu->GetSourceTrace(m_pipeline.GetFPUSource());
    pls_execute ^= m_allocator.m_bundle;

    m_pipeline.p_Pipeline.SetStorageTraces(
        /* Writeback */ opt(pls_writeback) *
        /* Memory */    opt(pls_memory) *
        /* Execute */   opt(pls_execute) *
        /* Fetch */     opt(pls_fetch) *
                        m_pipeline.m_active );

    m_network.p_DelegationIn.SetStorageTraces(m_network.m_delegateIn * (
        /* MSG_ALLOCATE */          (m_network.m_link.out ^ m_allocator.m_allocRequestsExclusive ^
                                     m_allocator.m_allocRequestsSuspend ^ m_allocator.m_allocRequestsNoSuspend) ^
        /* MSG_SET_PROPERTY */      (m_network.m_link.out) ^
        /* MSG_CREATE */            (m_allocator.m_creates) ^
        /* MSG_SYNC */              opt(m_network.m_link.out ^ m_network.m_syncs) ^
        /* MSG_DETACH */            opt(m_network.m_link.out) ^
        /* MSG_BREAK */             (opt(m_network.m_syncs) * opt(m_network.m_link.out)) ^
        /* MSG_RAW_REGISTER */      m_allocator.m_readyThreadsOther ^
        /* RRT_LAST_SHARED */       (DELEGATE) ^
        /* RRT_FIRST_DEPENDENT */   (m_allocator.m_readyThreadsOther) ^
        /* RRT_GLOBAL */            (m_allocator.m_readyThreadsOther * opt(m_network.m_link.out))
                                                  ));

    m_network.p_Link.SetStorageTraces((
        /* MSG_ALLOCATE */          (m_allocator.m_allocRequestsExclusive ^
                                     m_allocator.m_allocRequestsSuspend ^ m_allocator.m_allocRequestsNoSuspend) ^
        /* MSG_BALLOCATE */         (m_network.m_link.out ^ DELEGATE) ^
        /* MSG_SET_PROPERTY */      opt(m_network.m_link.out) ^
        /* MSG_CREATE */            (opt(m_network.m_link.out) ^ (m_allocator.m_alloc * opt(m_network.m_link.out)) ) ^
        /* MSG_DONE */              opt(m_network.m_link.out ^ m_network.m_syncs) ^
        /* MSG_SYNC */              opt(m_network.m_link.out ^ m_network.m_syncs) ^
        /* MSG_DETACH */            opt(m_network.m_link.out) ^
        /* MSG_GLOBAL */            (m_allocator.m_readyThreadsOther * opt(m_network.m_link.out)) ^
        /* MSG_BREAK */             (opt(m_network.m_link.out ^ m_network.m_syncs) * opt(m_network.m_link.out))
                                          ) * m_network.m_link.in);

    m_network.p_AllocResponse.SetStorageTraces(
        ( DELEGATE ^ m_network.m_allocResponse.out ) * m_network.m_allocResponse.in) ;

    m_network.p_Syncs.SetStorageTraces(
        DELEGATE );

    // This core can send a message to every other core.
    // (Except itself, that goes straight into m_delegationIn).
    StorageTraceSet stsDelegationOut;
    for (size_t i = 0; i < m_grid.size(); i++)
    {
        if (m_grid[i] != this) {
            stsDelegationOut ^= m_grid[i]->m_network.m_delegateIn;
        }
    }
    m_network.p_DelegationOut.SetStorageTraces(stsDelegationOut * m_network.m_delegateOut);
#undef DELEGATE

    if (m_io_if != NULL)
    {
        // Asynchronous events from the I/O network can wake up / terminate threads
        // due to a register write.
        m_io_if->GetReadResponseMultiplexer().p_IncomingReadResponses.SetStorageTraces(opt(m_allocator.m_readyThreadsOther) ^ opt(m_allocator.m_cleanup));
        m_io_if->GetNotificationMultiplexer().p_IncomingNotifications.SetStorageTraces(opt(m_allocator.m_readyThreadsOther) ^ opt(m_allocator.m_cleanup));
    }
}

void DRISC::InitializeRegisters()
{
    for (auto& ri : m_reginits)
    {
        RegAddr reg_addr = ri.first;
        auto& value = ri.second;

        RegValue reg_value;
        reg_value.m_state = RST_FULL;
        reg_value.m_integer = 0;

        assert(value.size() > 0);

        if (value[0] == 'b')
        {
            if (m_io_if == NULL)
            {
                clog << "#warning: -RNNN=B... specified but " << GetName() << " is not connected to I/O" << endl;
            }
            else {
                auto devname = m_io_if->GetIOBusInterface().GetIOBus().GetDeviceIDByName(value.substr(1));
                reg_value.m_integer = m_io_if->GetDeviceBaseAddress(devname);
            }
        }
        else if (value[0] == 'i' || (reg_addr.type == RT_INTEGER && (value[0] == '-' || (value[0] >= '0' && value[0] <= '9'))))
        {
            string nval = (value[0] == 'i') ? value.substr(1) : value;
            char *endptr;
            reg_value.m_integer = strtoull(nval.c_str(), &endptr, 0);
            if (*endptr != '\0')
            {
                throw exceptf<InvalidArgumentException>("Invalid register value: %s (%s)", nval.c_str(), reg_addr.str().c_str());
            }
        }
        else if (value[0] == 'f' || (reg_addr.type == RT_FLOAT && (value[0] == '-' || (value[0] >= '0' && value[0] <= '9'))))
        {
            string nval = (value[0] == 'f') ? value.substr(1) : value;
            char *endptr;
            reg_value.m_float.fromfloat(strtod(nval.c_str(), &endptr));
            if (*endptr != '\0')
            {
                throw exceptf<InvalidArgumentException>("Invalid register value: %s (%s)", nval.c_str(), reg_addr.str().c_str());
            }
        }
        else
        {
            throw exceptf<InvalidArgumentException>("Invalid register initializer: %s", reg_addr.str().c_str());
        }

        m_registerFile.WriteRegister(reg_addr, reg_value);
    }
}

bool DRISC::Boot(MemAddr runAddress, bool legacy)
{
    auto placeSize = GetGridSize();

    COMMIT {
        m_allocator.AllocateInitialFamily(runAddress, legacy, placeSize, 0);
        InitializeRegisters();
    }
    return true;
}

bool DRISC::IsIdle() const
{
    return m_threadTable.IsEmpty() && m_familyTable.IsEmpty() && m_icache.IsEmpty();
}

unsigned int DRISC::GetNumSuspendedRegisters() const
{
    unsigned int num = 0;
    for (size_t i = 0; i < NUM_REG_TYPES; ++i)
    {
        RegSize size = m_registerFile.GetSizes()[i];
        for (RegIndex r = 0; r < size; ++r)
        {
            RegValue value;
            m_registerFile.ReadRegister(MAKE_REGADDR((RegType)i, r), value);
            if (value.m_state == RST_WAITING) {
                ++num;
            }
        }
    }
    return num;
}

void DRISC::MapMemory(MemAddr address, MemSize size, ProcessID pid)
{
    assert(m_memadmin != NULL);
    m_memadmin->Reserve(address, size, pid,
                        IMemory::PERM_READ | IMemory::PERM_WRITE |
                        IMemory::PERM_DCA_READ | IMemory::PERM_DCA_WRITE);
}

void DRISC::UnmapMemory(MemAddr address, MemSize size)
{
    assert(m_memadmin != NULL);
    // TODO: possibly check the size matches the reserved size
    m_memadmin->Unreserve(address, size);
}

void DRISC::UnmapMemory(ProcessID pid)
{
    assert(m_memadmin != NULL);
    // TODO: possibly check the size matches the reserved size
    m_memadmin->UnreserveAll(pid);
}

bool DRISC::CheckPermissions(MemAddr address, MemSize size, int access) const
{
    assert(m_memadmin != NULL);
    bool mp = m_memadmin->CheckPermissions(address, size, access);
    if (!mp && (access & IMemory::PERM_READ) && (address & (1ULL << (sizeof(MemAddr) * 8 - 1))))
    {
        // we allow reads to the first cache line (64 bytes) of TLS to always succeed.

        // find the mask for the lower bits of TLS
        MemAddr mask = 1ULL << (sizeof(MemAddr) * 8 - (m_bits.pid_bits + m_bits.tid_bits + 1));
        mask -= 1;

        // ignore the lower 1K of TLS heap
        mask ^= 1023;

        if ((address & mask) == 0)
            return true;
    }
    return mp;
}

//
// Below are the various functions that construct configuration-dependent values
//
MemAddr DRISC::GetTLSAddress(LFID /* fid */, TID tid) const
{
    // 1 bit for TLS/GS
    // P bits for CPU
    // T bits for TID
    assert(sizeof(MemAddr) * 8 > m_bits.pid_bits + m_bits.tid_bits + 1);

    unsigned int Ls  = sizeof(MemAddr) * 8 - 1;
    unsigned int Ps  = Ls - m_bits.pid_bits;
    unsigned int Ts  = Ps - m_bits.tid_bits;

    return (static_cast<MemAddr>(1)     << Ls) |
           (static_cast<MemAddr>(m_pid) << Ps) |
           (static_cast<MemAddr>(tid)   << Ts);
}

MemSize DRISC::GetTLSSize() const
{
    assert(sizeof(MemAddr) * 8 > m_bits.pid_bits + m_bits.tid_bits + 1);

    return static_cast<MemSize>(1) << (sizeof(MemSize) * 8 - (1 + m_bits.pid_bits + m_bits.tid_bits));
}

static Integer GenerateCapability(unsigned int bits)
{
    Integer capability = 0;
    Integer step = (Integer)RAND_MAX + 1;
    for (Integer limit = (1ULL << bits) + step - 1; limit > 0; limit /= step)
    {
        capability = capability * step + rand();
    }
    return capability & ((1ULL << bits) - 1);
}

FCapability DRISC::GenerateFamilyCapability() const
{
    assert(sizeof(Integer) * 8 > m_bits.pid_bits + m_bits.fid_bits);
    return GenerateCapability(sizeof(Integer) * 8 - m_bits.pid_bits - m_bits.fid_bits);
}

Integer DRISC::PackPlace(const PlaceID& place) const
{
    assert(IsPowerOfTwo(place.size));
    assert(place.pid % place.size == 0);

    return place.capability << (m_bits.pid_bits + 1) | (place.pid << 1) | place.size;
}

PlaceID DRISC::UnpackPlace(Integer id) const
{
    // Unpack the place value: <Capability:N, PID*2|Size:P+1>
    PlaceID place;

    place.size       = 0;
    place.pid        = 0;
    place.capability = id >> (m_bits.pid_bits + 1);

    // Clear the capability bits
    id &= (2ULL << m_bits.pid_bits) - 1;

    // ctz below only works if id != 0
    if (id != 0)
    {
        // Find the lowest bit that's set to 1
        unsigned int bits = ctz(id);
        place.size = (1 << bits);           // That bit is the size
        place.pid  = (id - place.size) / 2; // Clear bit and shift to get base
    }
    return place;
}

FID DRISC::UnpackFID(Integer id) const
{
    // Unpack the FID: <Capability:N, LFID:F, PID:P>
    FID fid;
    fid.pid        =  (PID)((id >>               0) & ((1ULL << m_bits.pid_bits) - 1));
    fid.lfid       = (LFID)((id >> m_bits.pid_bits) & ((1ULL << m_bits.fid_bits) - 1));
    fid.capability = id >> (m_bits.pid_bits + m_bits.fid_bits);
    return fid;
}

Integer DRISC::PackFID(const FID& fid) const
{
    // Construct the FID: <Capability:N, LFID:F, PID:P>
    return (fid.capability << (m_bits.pid_bits + m_bits.fid_bits)) | (fid.lfid << m_bits.pid_bits) | fid.pid;
}

}
