===============================================
 C++ interface for MGSim core component models
===============================================

:Author: Raphael 'kena' Poss
:Date: July 2013

:Abstract: This note outlines how to implement alternate core models
    next to the model "DRISC" already present in the MGSim component
    library.

.. contents::

Overview
========

Core component models may interact with the following objects in the
simulation framework:

- the simulation kernel;
- memory models, via the "memory client" interface;
- I/O network models via the "I/O client" interface;
- asynchronous, shared FPUs via the "FPU client" interface.
- the instantiation procedure (eg. ``MGSystem``);

The number of interfaces is not limited per core. For example the
``DRISC`` model has 3 memory client interfaces (L1-I, L1-D and DCA),
either 0 or 1 I/O client interface, and either 0 or 1 FPU interface.

Simulation kernel interface
===========================

The kernel interface is organized as follows:

- the component uses one or more instances of ``Simulation::Process`` (``<sim/kernel.h>``);
- each ``Process`` is instantiated and connected to a callback method via ``delegate::create``;
- each ``Process`` is also connected to a "storage" (latch / flag / FIFO etc) which only *activate*
  the process if there is some data in the storage;
- at each cycle, the kernel calls the delegate method of each *active* process 3 times:
  - the first time to request arbitrations;
  - the second time to report arbitration results and select a control path;
  - the third time to commit side-effects.

At phase 1 and 2 (ACQUIRE / CHECK) the delegate may return anything
else than ``SUCCESS`` to indicate a stall: phase 3 (COMMIT) is
skipped and the delegate will be retried in the next cycle. If
phases 1 and 2 report ``SUCCESS``, then phase 3 must also report
``SUCCESS``. At the next cycle, all storage updates from the
previous cycle are used to determine which processes have become
inactive and which new processes have become active.

All services from other components in the library should be also
called recursively at each phase.

Memory client interface
=======================

The memory interface is organized as follows:

- the sub-component that interacts with memory inherits ``IMemoryCallback`` (``<arch/Memory.h>``);
- during initialization, it is given a reference to ``IMemory``;
- during memory operations the component can use ``IMemory::Read`` and ``IMemory::Write``;
- the memory interface is asynchronous: ``Read`` and ``Write`` merely *issue* the request; completion
  is notified via the ``IMemoryCallback`` interface;
- before any operation, the component must register its
  ``IMemoryCallback`` interface to the memory model with
  ``IMemory::RegisterClient``. Also, ``RegisterClient`` gives a
  ``MCID`` value back (Memory Client IDentifier) which must be
  provided to subsequent calls to ``Read`` and ``Write``.

For example, ``DRISC`` implements ``IMemoryCallback`` via its
``ICache``, ``DCache`` and ``IODirectMemoryAccess`` sub-components.

I/O client interface
====================

The I/O interface is organized as follows:

- the sub-component that interacts with I/O inherits ``IIOBusClient`` [#]_ (``<arch/IOBus.h>``);
- during initialization, it is given a reference to ``IIOBus``;
- during I/O operations the component can use the services
  ``SendReadRequest``, ``SendReadResponse``, ``SendWriteRequest``,
  ``SendInterruptRequest``, ``SendNotification``,
  ``SendActiveMessage`` from ``IIOBus``;
- the I/O interface is asynchronous; completion are notified via the ``IIOBusClient`` interface;
- before any operation, the component must register its
  ``IIOBusClient`` interface to the I/O model with
  ``IIOBus::RegisterClient``. Also, ``RegisterClient`` gives a
  ``IODeviceID`` value back (IO Device IDentifier) which must be
  provided to subsequent calls in ``IIOBus``.

.. [#] NB: despite the name "bus" the I/O interconnect may implement an arbitrary topology.

For example, ``DRISC`` implements ``IIOBusClient`` via its
``IOInterface`` sub-component.

FPU client interface
====================

The FPU interface is organized as follows:

- the sub-component that interacts with an FPU inherits ``FPU::IFPUClient`` (``<arch/FPU.h>``);
- during initialization, it is given a reference to ``FPU``;
- during FPU operations the component can use the service ``QueueOperation``;
- the FPU is asynchronous; completion is notified via the ``FPU::IFPUClient`` interface;
- before any operation, the component must register its ``IFPUClient``
  interface to the FPU model with ``FPU::RegisterSource``. Also,
  ``RegisterSource`` gives a "Source identifier" (``size_t``) back
  which must be provided to subsequent calls to ``QueueOperation``.

For example, ``DRISC`` implements ``FPU::IFPUClient`` via its
``RegisterFile`` sub-component.

Instantiation interface
=======================

At the time of this writing there is no common instantiation
interface. To support new component models, some new conditionals must
be added in ``MGSystem.cpp``. However the following guidelines apply:

- a separation exists between *construction*, *connection* and *initialization*.
- construction allocates the component model, gives it a name and
  places it in the logical component tree of the entire simulation model;
- connection registers pointers and interfaces to other component models;
- initialization is only called after all components are connected together.
