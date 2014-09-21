// -*- c++ -*-
#ifndef PORTS_H
#define PORTS_H

#include "kernel.h"
#include <cassert>
#include <algorithm>
#include <map>
#include <set>
#include <limits>

namespace Simulator
{

    template <typename I> class Structure;
    template <typename I> class ArbitratedWritePort;
    class ArbitratedReadPort;

    //
    // ArbitratedPort: base class for arbitrated ports.
    // An arbitrated port controls concurrent accesses to a
    // shared resource by two or more processes.
    //
    // Not intended for direct use, instead to be refined.
    // See SimpleArbitratedPort below.
    class ArbitratedPort
    {
    private:
        // This arbitrator's name
        std::string    m_name;

        // The process that "wins" the arbitration (acquires the port)
        const Process* m_selected;

        // The number of cycles the arbitrator was actively arbitrating
        uint64_t       m_busyCycles;

    protected:
        // Accessors for m_selected (the process that acquires the port)
        const Process* GetSelectedProcess() const { return m_selected; }
        void SetSelectedProcess(const Process* p) { m_selected = p; }

        // Test if the designated process has acquired the port
        bool HasAcquired(const Process& process) const
        {
            return m_selected == &process;
        }

        // Increment the busy counter
        void MarkBusy() { ++m_busyCycles; }

        // Constructor, destructor etc.
        ArbitratedPort(const std::string& name);
        virtual ~ArbitratedPort() {}
        ArbitratedPort(const ArbitratedPort&) = delete;
        ArbitratedPort& operator=(const ArbitratedPort&) = delete;

    public:
        // Retrieve the active cycle count
        uint64_t GetBusyCycles() const { return m_busyCycles; }
        // Retrieve the arbitrator name
        const std::string& GetName() const { return m_name; }
    };

    //
    // SimpleArbitratedPort: simple base class for ports using a
    // simple list of processes.
    //
    // This is not intended for direct use; rather to be refined with
    // a concrete arbitration policy.  See PriorityArbitratedPort and
    // CyclicArbitratedPort.
    //
    class SimpleArbitratedPort : public ArbitratedPort
    {
    protected:
        typedef std::vector<const Process*> ProcessList;

        // The list of all processes that *may* access the port.
        ProcessList    m_processes;

        // The list of all processes that have requested access within
        // this cycle.
        ProcessList    m_requests;

        // The last cycle counter of a request.
        CycleNo        m_lastrequest;

    public:
        // Register a process that may access the port.
        void AddProcess(const Process& process);

    protected:
        // Test if a process may access the port.
        bool CanAccess(const Process& process) const
        {
            return std::find(m_processes.begin(),
                             m_processes.end(),
                             &process) != m_processes.end();
        }

        // Register a process as wanting to access the port (candidate for
        // arbitration).
        void AddRequest(const Process& process, CycleNo c);

        // Constructor, destructor etc.
        SimpleArbitratedPort(const std::string& name);
        virtual ~SimpleArbitratedPort() {}
        SimpleArbitratedPort(const SimpleArbitratedPort&) = delete;
        SimpleArbitratedPort& operator=(const SimpleArbitratedPort&) = delete;
    };

    //
    // PriorityArbitratedPort: arbitrated port with priority levels.
    //
    // The list of processes that may access the port also defines
    // priority order. Processes earlier in the list have a higher
    // priority than processes later in the list.
    //
    // Not intended for direct use, rather to be refined or integrated
    // with IStructure, see ArbitratedReadPort, ArbitratedWritePort
    // and ArbitratedService below.
    class PriorityArbitratedPort : public SimpleArbitratedPort
    {
    public:
        // Decide which process acquires the port.
        void Arbitrate();

    protected:
        // Constructor, destructor etc.
        PriorityArbitratedPort(const std::string& name);
        virtual ~PriorityArbitratedPort() {}
        PriorityArbitratedPort(const PriorityArbitratedPort&) = delete;
        PriorityArbitratedPort& operator=(const PriorityArbitratedPort&) = delete;
    };

    //
    // CyclicArbitratedPort: arbitrated port with round-robin
    // scheduling.
    //
    // Each process gets a turn to use the arbitrated port.
    //
    // Not intended for direct use, rather to be refined or integrated
    // with IStructure, see PriorityCyclicArbitratedPort and
    // ArbitratedService below.
    class CyclicArbitratedPort : public SimpleArbitratedPort
    {
    protected:
        // The index (in m_processes) of the process
        // that has last acquired the port.
        size_t m_lastSelected;

    public:
        // Decide which process acquires the port.
        void Arbitrate();

    protected:
        // Constructor, destructor etc.
        CyclicArbitratedPort(const std::string& name);
        virtual ~CyclicArbitratedPort() {}
        CyclicArbitratedPort(const CyclicArbitratedPort&) = delete;
        CyclicArbitratedPort& operator=(const CyclicArbitratedPort&) = delete;
    };

    //
    // PriorityCyclicArbitratedPort: arbitrated port with both
    // priorities and round-robin scheduling.
    //
    // This arbitrator has two sets of processes. One for the cylicic
    // processes, which are at the lowest priority.  One for the
    // priority processes, which all have a higher priority than the
    // cyclic processes.
    //
    // Not intended for direct use, rather to be refined or integrated
    // with IStructure, see ArbitratedService below.
    class PriorityCyclicArbitratedPort : public CyclicArbitratedPort
    {
    protected:
        // The set of cyclic processes (that have lowest priority)
        ProcessList m_cyclicprocesses;

        // Test if a process may access the port.
        bool CanAccess(const Process& process) const {
            return SimpleArbitratedPort::CanAccess(process)
                || (std::find(m_cyclicprocesses.begin(),
                              m_cyclicprocesses.end(),
                              &process) != m_cyclicprocesses.end());
        }

    private:
        // hide AddProcess from base class to force use
        // of AddPriorityProcess below;
        void AddProcess(const Process& process);

    public:
        // Decide which process acquires the port.
        void Arbitrate();

        void AddPriorityProcess(const Process& process) {
            SimpleArbitratedPort::AddProcess(process);
        }
        void AddCyclicProcess(const Process& process) {
            m_cyclicprocesses.push_back(&process);
        }

    protected:
        // Constructor, destructor etc.
        PriorityCyclicArbitratedPort(const std::string& name);
        virtual ~PriorityCyclicArbitratedPort() {}
        PriorityCyclicArbitratedPort(const PriorityCyclicArbitratedPort&) = delete;
        PriorityCyclicArbitratedPort& operator=(const PriorityCyclicArbitratedPort&) = delete;
    };


    //
    // ArbitratedService
    //
    // An arbitrated service is like an arbitrated port, only there is
    // no direct associated structure. Its purpose is to arbitrate
    // access to a single feature of a component or group thereof.
    //
    template <typename Base = class PriorityArbitratedPort>
    class ArbitratedService : public Base, public Arbitrator
    {
    protected:
        // Required by Arbitrator, forward to the port arbitrate function.
        void OnArbitrate() override
        {
            Base::Arbitrate();
        }

    public:
        // Issue a request to access the service.
        // Returns false after 1st simulation phase
        // if arbitration was denied.
        bool Invoke()
        {
            Kernel& kernel = Arbitrator::GetClock().GetKernel();
            auto& process = *kernel.GetActiveProcess();

            // The process must have been registered before.
            assert(Base::CanAccess(process));

            if (kernel.GetCyclePhase() == PHASE_ACQUIRE)
            {
                Base::AddRequest(process, kernel.GetCycleNo());
                Arbitrator::RequestArbitration();
                return true;
            }
            else
            {
                return Base::HasAcquired(process);
            }
        }

        // Requested by Arbitrator, forward to Base::GetName.
        const std::string& GetName() const override { return Base::GetName(); }

        // Constructor, destructor etc.
        ArbitratedService(Clock& clock, const std::string& name)
            : Base(name),
              Arbitrator(clock)
        { }
        virtual ~ArbitratedService() {}
        ArbitratedService(const ArbitratedService&) = delete;
        ArbitratedService& operator=(const ArbitratedService&) = delete;
    };


    //
    // WritePort: arbitrate between write requests.
    //
    // This is used when a structure (eg a register file) has multiple
    // write ports that can operate simultaneously.  Although multiple
    // ports means arbitration is not needed to access the shared
    // structure generally, in the specific case of writes we still
    // want to prevent simultaneous writes to the same position in the
    // structure. (eg simultaneous writes to different positions are
    // ok.)
    //
    // This class "remembers" which position was selected by write
    // requests, and reports the one that was chosen to writing
    // processes.
    //
    // Not intended for direct use, rather should be refined by
    // classes implementing Write(), eg. DedicatedWritePort or
    // ArbitratedWritePort.
    template <typename I>
    class WritePort
    {
        I       m_index;    ///< Index (position) of the request in the larger structure
        bool    m_valid;    ///< Is there a request? (set by Write requests)
        bool    m_chosen;   ///< Have we been chosen? (decided by arbitration)

    protected:
        // Register a request to a particular index.
        void SetRequestIndex(const I& index)
        {
            assert(m_valid == false);
            m_index  = index;
            m_valid  = true;
            m_chosen = false;
        }


        // Constructors, destructors etc.
        WritePort() : m_index(), m_valid(false), m_chosen(false) {}
        virtual ~WritePort() {}
        WritePort(const WritePort&) = delete;
        WritePort& operator=(const WritePort&) = delete;

    public:
        // Return the index if there is a request pending, otherwise 0.
        const I* GetIndex() const { return (m_valid) ? &m_index : 0; }
        // Test whether this port was selected by arbitration.
        bool     IsChosen() const { return m_chosen; }

        // Decide whether this port can go on with its request.
        // (called by arbitration)
        void Notify(bool chosen)
        {
            m_chosen = chosen;
            m_valid  = false;
        }
    };

    //
    // ReadOnlyStructure
    //
    // A shared structure with zero or more read ports.
    // Each port may be shared by multiple clients.
    // Arbitration is decided in its own clock domain.
    class ReadOnlyStructure : public virtual Object, private Arbitrator
    {
        typedef std::vector<ArbitratedReadPort*> ReadPortList;

        // Set of all read ports.
        ReadPortList m_readPorts;

    protected:
        // Perform arbitration of all read ports.
        void ArbitrateReadPorts();

    public:
        // Request arbitration, requested each time a request to a
        // shared arbitrator comes in (see derived/user classes).
        void RequestArbitration() { Arbitrator::RequestArbitration(); }

        // Register a new read port to this arbitrator.
        void RegisterReadPort(ArbitratedReadPort& port);

        // Required by Arbitrator, forwards to ArbitrateReadPorts()
        void OnArbitrate() override;

        // Required by Arbitrator, forwards to Object::GetName()
        const std::string& GetName() const override;

        // Constructor, destructor etc.
        ReadOnlyStructure(const std::string& name, Object& parent, Clock& clock);
        virtual ~ReadOnlyStructure() {}
        ReadOnlyStructure(const ReadOnlyStructure&) = delete;
        ReadOnlyStructure& operator=(const ReadOnlyStructure&) = delete;
    };

    //
    // ReadWriteStructure
    //
    // A shared structure with zero or more read or write ports.
    // Each port may be shared by multiple clients.
    // Arbitration is decided in its own clock domain.
    template <typename I>
    class ReadWriteStructure : public ReadOnlyStructure
    {
        typedef std::vector<ArbitratedWritePort<I>*> ArbitratedWritePortList;
        typedef std::vector<WritePort<I>*>  WritePortList;

        // Set of all write ports.
        // This is populated by RegisterWritePort below.
        WritePortList           m_writePorts;

        // Set of those write ports that need arbitration.
        // This must be a subset of m_writePorts above.
        // This is populated by RegisterArbitratedWritePort below.
        ArbitratedWritePortList m_arbitratedWritePorts;

        // Port priority list. The order in this list determines the
        // priority order for write requests to the same index.
        // Ports earlier in the list have higher priority.
        // The order is set by AddPort below.
        WritePortList           m_priorities;

    protected:
        // Perform process arbitration of all arbitrated write ports.
        // The result of this gives access to each port to only 1 process.
        void ArbitrateWritePorts()
        {
            for (auto p : m_arbitratedWritePorts)
                p->Arbitrate();
        }

        // Required by Arbitrator
        void OnArbitrate() override
        {
            typedef std::map<I, std::vector<WritePort<I>*> > RequestPortMap;

            // Tell each port to arbitrate.
            // The result is at most 1 process remaining per port.
            ArbitrateReadPorts();
            ArbitrateWritePorts();

            // Get the final requests from all ports
            // Mapping of index -> list of ports requesting that index
            RequestPortMap requests;
            for (auto p : m_writePorts)
            {
                const I* i = p->GetIndex();
                if (i != NULL)
                    requests[*i].push_back(p);
            }

            // Arbitrate between the ports for each request
            for (auto& req : requests)
            {
                const WritePort<I>* selected = NULL;

                // Find the port earliest in the list.
                auto sz = m_priorities.size();
                auto min = sz;
                for (auto p : req.second)
                {
                    size_t prio = std::find(m_priorities.begin(),
                                            m_priorities.end(),
                                            p) - m_priorities.begin();
                    if (prio < min)
                    {
                        min = prio;
                        selected = p;
                    }
                }
                // Report the selection to every port.
                for (auto p : req.second)
                    p->Notify( selected == p );
            }
        }


    public:
        // Register a non-arbitrated write port.
        void RegisterWritePort(WritePort<I>& port)
        {
            // The port must not be registered yet.
            assert(std::find(m_writePorts.begin(),
                             m_writePorts.end(),
                             &port) == m_writePorts.end());
            m_writePorts.push_back(&port);
        }

        // Register an arbitrated write port.
        void RegisterArbitratedWritePort(ArbitratedWritePort<I>& port)
        {
            RegisterWritePort(port);
            m_arbitratedWritePorts.push_back(&port);
        }

        // After a port was registered, set its priority for arbitration.
        // The ports added later have lower priority.
        void AddPort(WritePort<I>& port)
        {
            // The port must be registered already.
            assert(std::find(m_writePorts.begin(),
                             m_writePorts.end(),
                             &port) != m_writePorts.end());
            // The port must not have a priority yet.
            assert(std::find(m_priorities.begin(),
                             m_priorities.end(),
                             &port) == m_priorities.end());

            m_priorities.push_back(&port);
        }

        // Constructor, destructor etc.
        ReadWriteStructure(const std::string& name, Object& parent, Clock& clock)
            : Object(name, parent),
              ReadOnlyStructure(name, parent, clock),
              m_writePorts(),
              m_arbitratedWritePorts(),
              m_priorities()
        {}
        virtual ~ReadWriteStructure() {}
        ReadWriteStructure(const ReadWriteStructure&) = delete;
        ReadWriteStructure& operator=(const ReadWriteStructure&) = delete;
    };

    //
    // ArbitratedReadPort: a PriorityArbitratedPort around
    // a ReadOnlyStructure.
    //
    class ArbitratedReadPort : public PriorityArbitratedPort
    {
        // The structure that the port is accessing.
        ReadOnlyStructure& m_structure;

    public:
        // Issue a request to access the structure for read.
        // Returns false after 1st simulation phase
        // if the request was denied by arbitration.
        bool Read()
        {
            auto kernel = m_structure.GetKernel();
            auto& process = *kernel->GetActiveProcess();

            // Process must have been registered (AddProcess) before.
            assert(CanAccess(process));

            if (kernel->GetCyclePhase() == PHASE_ACQUIRE)
            {
                // In the first phase, register the request.
                AddRequest(process, kernel->GetCycleNo());
                m_structure.RequestArbitration();
                return true;
            }
            else
            {
                return HasAcquired(process);
            }
        }

        // Constructor, destructor etc.
        ArbitratedReadPort(ReadOnlyStructure& structure, const std::string& name);
        virtual ~ArbitratedReadPort() {}
        ArbitratedReadPort(const ArbitratedReadPort&) = delete;
        ArbitratedReadPort& operator=(const ArbitratedReadPort&) = delete;

    };

    //
    // ArbitratedWritePort: a PriorityArbitratedPort around
    // a ReadWriteStructure.
    //
    template <typename I>
    class ArbitratedWritePort
        : public PriorityArbitratedPort,
          public WritePort<I>
    {
        typedef std::map<const Process*, I> IndexMap;

        ReadWriteStructure<I>& m_structure;
        IndexMap               m_indices;

    protected:
        // Register a request to access the structure for
        // write.
        void AddRequest(const Process& process, const I& index, CycleNo c)
        {
            PriorityArbitratedPort::AddRequest(process, c);
            m_indices[&process] = index;
        }

    public:
        // Issue a request to access the structure for write.
        // Returns false after 1st simulation phase
        // if the request was denied by arbitration.
        bool Write(const I& index)
        {
            auto kernel = m_structure.GetKernel();
            auto& process = *kernel->GetActiveProcess();

            // Process must have been registered (AddProcess) before.
            assert(CanAccess(process));

            if (kernel->GetCyclePhase() == PHASE_ACQUIRE)
            {
                // In the first phase, register the request.
                AddRequest(process, index, kernel->GetCycleNo());
                m_structure.RequestArbitration();
                return true;
            }
            else
            {
                return WritePort<I>::IsChosen() && HasAcquired(process);
            }
        }

        // Decide which process acquires the port.
        void Arbitrate()
        {
            PriorityArbitratedPort::Arbitrate();
            auto process = GetSelectedProcess();
            if (process != NULL)
            {
                // A process was selected; make its index active for
                // write port arbitration
                auto p = m_indices.find(process);
                assert(p != m_indices.end());
                WritePort<I>::SetRequestIndex(p->second);
            }
        }

        // Constructor, destructor etc.
        ArbitratedWritePort(ReadWriteStructure<I>& structure, const std::string& name)
            : PriorityArbitratedPort(name),
              m_structure(structure),
              m_indices()
        {
            m_structure.RegisterArbitratedWritePort(*this);
        }
        virtual ~ArbitratedWritePort() {}
        ArbitratedWritePort(const ArbitratedWritePort&) = delete;
        ArbitratedWritePort& operator=(const ArbitratedWritePort&) = delete;
    };

    //
    // DedicatedPort: a single process port to a structure.
    // This is a base class for dedicated read and write ports below.
    class DedicatedPort
    {
        // The process connected to this port.
        const Process* m_process;

    protected:
        // Test if the given process can access this port.
        bool CanAccess(const Process& process)
        {
            return (m_process == &process);
        }
    public:
        // Associate this port with the given process.
        void SetProcess(const Process& process) { m_process = &process; }

        // Constructor, destructor etc.
        DedicatedPort();
        DedicatedPort(const DedicatedPort&) = delete;
        DedicatedPort& operator=(const DedicatedPort&) = delete;
        virtual ~DedicatedPort() {}
    };

    //
    // DedicatedReadPort: a dedicated port to a read structure.
    //
    class DedicatedReadPort : public DedicatedPort
    {
        // The associated structure.
        ReadOnlyStructure& m_structure;

    public:
        // Issue a request to access the structure for read.
        bool Read()
        {
            // The current process must have been associated with SetProcess.
            assert(CanAccess( *m_structure.GetKernel()->GetActiveProcess() ));

            return true;
        }

        // Constructor, destructor etc.
        DedicatedReadPort(ReadOnlyStructure& structure);
        virtual ~DedicatedReadPort() {}
        DedicatedReadPort(const DedicatedReadPort&) = delete;
        DedicatedReadPort& operator=(const DedicatedReadPort&) = delete;
    };

    //
    // DedicatedWritePort: a dedicated port to a read/write structure.
    //
    template <typename I>
    class DedicatedWritePort : public DedicatedPort, public WritePort<I>
    {
        // The associated structure.
        ReadWriteStructure<I>& m_structure;

    public:
        // Issue a request to access the structure for write.
        // Returns false after 1st simulation phase
        // if the request was denied by arbitration.
        bool Write(const I& index) {
            auto kernel = m_structure.GetKernel();

            // The current process must have been associated with SetProcess.
            assert(CanAccess( *kernel->GetActiveProcess() ));

            if (kernel->GetCyclePhase() == PHASE_ACQUIRE)
            {
                WritePort<I>::SetRequestIndex(index);
                m_structure.RequestArbitration();
                return true;
            }
            else
            {
                return WritePort<I>::IsChosen();
            }
        }

        // Constructor, destructor etc.
        DedicatedWritePort(ReadWriteStructure<I>& structure)
            : DedicatedPort(),
              m_structure(structure)
        {
            m_structure.RegisterWritePort(*this);
        }

        virtual ~DedicatedWritePort() {}
        DedicatedWritePort(const DedicatedWritePort&) = delete;
        DedicatedWritePort& operator=(const DedicatedWritePort&) = delete;
    };
}
#endif
