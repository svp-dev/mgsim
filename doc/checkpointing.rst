=================================
 Work in progress: checkpointing
=================================

The header ``sim/sampling.h`` provides macros to declare and
initialize "sampling" variables (for statistics) and "state"
variables. The variables registered this way are then available for
inspection via the "show vars" and "read" commands at the simulator
prompt, also via monitoring (-m).

Assuming that *all* relevant simulation state is registered in this
way, it would then become possible to save and restore the simulation
state easily. Work is thus in progress to increase the amount of state
declared in this way, with checkpointing as ultimate goal.

At the time of this writing, there are a few obstacles to completion
of this task:

- serialization of Buffer<T> instances where T is a struct:

  Solution must introduce a serialization method in such structs.

- serialization of Buffer<T> instances where T contains pointers:

  Solution must replace pointers by index to an array serialized separately.

- serialization of other std::container<T> instances:

  Solution must introduce a serialization method in the surrounding object.

  Known cases::

    FPU::Source::inputs
    FPU::Unit::slots
    DDRMemory::m_activeRequests
    CDMA::Directory::m_dir
    CDMA::RootDirectory::m_dir
    CDMA::RootDirectory::m_active
    ZLCDMA::Directory::m_dir
    ZLCDMA::RootDirectory::m_dir
    ZLCDMA::RootDirectory::m_active

Progress:

- ``arch/dev``
- ``arch/mem`` except ``cdma``
- ``arch/drisc/DCache``, ``ICache``
- ``arch/drisc/Allocator`` (partial?)
