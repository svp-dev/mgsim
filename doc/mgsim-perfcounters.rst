====================
 mgsim-perfcounters
====================

----------------------------
 MGSim performance counters
----------------------------

:Author: MGSim was created by Mike Lankamp. MGSim is now under
   stewardship of the Microgrid project. This manual page was written
   by Raphael 'kena' Poss.
:Date: October 2012
:Copyright: Copyright (C) 2008-2012 the Microgrid project.
:Version: PACKAGE_VERSION
:Manual section: 7

DESCRIPTION
===========

Programs running on the MGSim platform can inspect simulation metrics
by querying the MGSim performance counters.

The performance counters are exposed via memory-mapped I/O on all
cores where the configuration variable
``cpuN.perfcounters:mmio_baseaddr`` is configured and not zero.  When
performance counters are enabled, programs can issue regular memory
loads from the performance counter addresses. These loads are then
handled separately from memory requests: they are serviced within the
memory stage of the pipeline without inducing stalls or thread
suspends.

The default configuration starts the performance counters 
at address 8 in the address space: the first counter has address 8,
the second counter address 8 + wordsize, etc.

.. note:: This query interface and the corresponding simulation
   metrics are an artefact of the simulation environment; ie. they
   should not be considered available on actual hardware implementing
   the Microgrid platform.

COUNTERS
========

The following counters are currently supported:

======= ========================================================================
Counter Description
======= ========================================================================
0       Current clock cycle on the simulation master clock.
1       Total number of executed instructions in the current place (core 
        cluster) [1]_.
2       Total number of instructions issued to FPUs in the current place.
3       Total number of completed load instructions in the current place [2]_.
4       Total number of completed store instructions in the current place [2]_.
5       Total number of bytes read from memory via load instructions (including 
        L1 hits) in the current place [2]_.
6       Total number of bytes written to memory via store instructions 
        (including L1 hits) in the current place [2]_.

7       Total number of cache lines loaded by L1 caches from the upper level
        over the entire chip.
8       Total number of cache lines written to by L1 caches towards the upper
        level over the entire chip.
9       Number of cores in the current place.
10      Total threads allocated in slot-cycles in the current place [3]_.
11      Total families allocated in slot-cycles in the current place [3]_.
12      Total exclusive families allocated in slot-cycles in the current place [3]_.
13      Current time in MGSim's host environment, as per Unix time(3).
14      Current host date, bit packed [4]_.
15      Current host time, bit packed [5]_.
16      Total number of cache lines loaded from external memory.
17      Total number of cache lines written to external memory.
18      Total number of processed thread creation requests.
19      Total number of processed family creation requests.
20      Current clock cycle on the local core's clock.
======= ========================================================================

MONITORING VARIABLES
====================

Each query to the performance counter 0 (master clock cycles)
increases the monitoring variable ``nCycleSampleOps``. Each query to
any other counter increases ``nOtherSampleOps``. See mgsimdoc(7) for
details about monitoring variables.

NOTES
=====

.. [1] The "current place" is the core cluster where the thread
   quering the counter has been created.

.. [2] The number of memory load/stores and bytes read/written do not
   include memory-mapped I/O; they include only accesses to main
   memory.

.. [3] The number of thread/family allocations is expressed in
   slot-cycles: the running sum of the instantaneous number of
   currently allocated entries over the number of core cycles. For
   example if there are 2 entries allocated at cycle 0, and no more
   entries are allocated, then at cycle 100 the counter will report
   200: number of entries times number of cycles.

   An average over two measurements can thus be obtained by dividing
   the difference of counter values by the difference of core clock
   cycles elapsed.

   The running sum allows programs to track all allocation events,
   even when the sampling period is coarse compared to the density of
   thread/family allocation events.

.. [4] The date is packed into an integer word from LSB to MSB as
   follows:

   - bits 0-4: day in month (0-31)
   - bits 5-8: month in year (0-11)
   - bits 9-31: year from 1900

.. [5] The time is packed into an integer word from LSB to MSB as
   follows:

   - bits 0-14: microseconds / 2^5 (topmost 15 bits)
   - bits 15-20: seconds (0-59)
   - bits 21-26: minutes (0-59)
   - bits 27-31: hours (0-23)

SEE ALSO
========

mgsim(1), mgsimdoc(7), mgsim-memranges(7)

BUGS
====

Report bugs & suggest improvements to PACKAGE_BUGREPORT.




