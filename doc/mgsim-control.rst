===============
 mgsim-control
===============

-------------------------
 MGSim control interface
-------------------------

:Author: MGSim was created by Mike Lankamp. MGSim is now under
   stewardship of the Microgrid project. This manual page was written
   by Raphael 'kena' Poss.
:Date: October 2012
:Copyright: Copyright (C) 2008-2012 the Microgrid project.
:Version: PACKAGE_VERSION
:Manual section: 7

DESCRIPTION
===========

Programs running on the MGSim platform can control the
simulation environment via the MGSim control interface.

The control interface is composed of four modules, exposed
via memory-mapped I/O on all cores where the corresponding
configuration variable ``mmio_baseaddr`` is configured and not zero.

When a control module is enabled, programs can issue regular memory
stores to the module's addresses. These stores are then handled
separately from memory requests: they are serviced within the memory
stage of the pipeline without inducing stalls or thread suspends.

The four modules are:

- the standard output debug line (``cpuN.debug_stdout:mmio_baseaddr``,
  by default based at 0x200);
- the standard error debug line (``cpuN.debug_stderr:mmio_baseaddr``,
  by default based at 0x230);
- the action control interface (``cpuN.action:mmio_baseaddr``, by
  default based at 0x260);
- the MMU control interface (``cpuN.mmu:mmio_baseaddr``, by default
  based at 0x300).

.. note:: This query interface and the corresponding simulation
   metrics are an artefact of the simulation environment; ie. they
   should not be considered available on actual hardware implementing
   the Microgrid platform.

.. note:: The base addresses to these modules should be considered
   architectural constants, since software is encouraged to use
   absolute reference to their addresses for efficiency.

STANDARD OUTPUT/DEBUG LINES
===========================

The standard output/debug lines provide direct access to the Unix
standard output and error streams of the MGSim process.

The modules also provide automatic conversion of integer and
floating-point values to their textual representation, so that
programs running on the platform do not need to compute the textual
representation explicitly.

The two lines each expose the following ports:

==== ===========================================================================
Word Description
==== ===========================================================================
0    Output the low-order 8 bits as a character.
1    Output the entire word, considered as an unsigned integer, in decimal.
2    Output the entire word, considered as a signed integer, in decimal.
3    Output the entire word, considered as an unsigned integer, in hexadecimal.
4    Set the floating-point precision for subsequent uses of word 5.
5    Output the entire word, considered as an IEEE754 floating-point
     value, in scientific decimal representation.
==== ===========================================================================

Default addresses on 32-bit cores
---------------------------------

Using the default configuration placing the lines at 0x200 and 0x230,
the ports can be accessed as follows:

===== =============== =============== ==============
Word  Stdout, 32-bit  Stderr, 32-bit  Summary
===== =============== =============== ==============
0     0x200           0x230           Write char
1     0x204           0x234           Write udec
2     0x208           0x238           Write sdec
3     0x20C           0x23C           Write uhex
4     0x210           0x240           Set FP prec.
5     0x214           0x244           Write FPdec
===== =============== =============== ==============

Default addresses on 64-bit cores
---------------------------------

Using the default configuration placing the lines at 0x200 and 0x230,
the ports can be accessed as follows:

===== =============== =============== ==============
Word  Stdout, 64-bit  Stderr, 64-bit  Summary
===== =============== =============== ==============
0     0x200           0x230           Write char
1     0x208           0x238           Write udec
2     0x210           0x240           Write sdec
3     0x218           0x248           Write uhex
4     0x220           0x250           Set FP prec.
5     0x228           0x258           Write FPdec
===== =============== =============== ==============

ACTION CONTROL INTERFACE
========================

The action control interface provides access to the simulation kernel
to interrupt the simulation or request termination of the MGSim
process.

The interface provides the following ports:

===== ===========================================================================
Word  Description
===== ===========================================================================
0/4   Do nothing, continue.
1/5   Interrupt the simulation in a way that is resumable (self-requested breakpoint).
2/6   Abort the simulation.
3/7   Exit the simulator with the given exit code.
===== ===========================================================================

Writing a value to words 0-3 performs the action with no output. 

Writing a value X to words 4-7 performs the action, and additionally
prints a message composed of the low-order printable bytes of X.

Writing a value X to words 3/7 requests MGSim to exit with the process
status code set to the lower order 8 bits of X.

Default addresses
-----------------

Using the default configuration placing the interface at 0x260,
the ports can be accessed as follows:

====== ======================= ======================= ===============
Word   Address on 32-bit cores Address on 64-bit cores Summary
====== ======================= ======================= ===============
0      0x260                   0x260                   Continue
1      0x264                   0x268                   Interrupt
2      0x268                   0x270                   Abort
3      0x26c                   0x278                   Exit
4      0x270                   0x280                   Print; continue
5      0x274                   0x288                   Print; interrupt
6      0x278                   0x290                   Print; abort
7      0x27c                   0x298                   Print; exit with code
====== ======================= ======================= ===============

MMU CONTROL INTERFACE
=====================

The MMU control interface provides access to the virtual memory
manager to map/unmap memory storage to virtual address ranges.

The interface provides the following ports:

====== ===========================================================================
Words  Description
====== ===========================================================================
0-7    Map a memory range with the given size/address, using PID 0.
8-15   Unmap a memory range at given size/address, any PID.
16-23  Map a memory range with the given size/address, associate with current PID.
24     Unmap all memory ranges with the given PID.
25     Set the current PID.
====== ===========================================================================

For the first three ports, the lowest 3 bits of the *word address*
indicate the size of the memory range as a power of two above 4096 bytes, as
follows:

======== ================ =================
Word     Lowest 3 bits    Page size
======== ================ =================
0,8,16   0                4096   (2^12)
1,9,17   1                8192   (2^13)
2,10,18  2                16384  (2^14)
3,11,19  3                32768  (2^15)
4,12,20  4                65536  (2^16)
5,13,21  5                131072 (2^17)
6,14,22  6                262144 (2^18)
7,15,23  7                524288 (2^19)
======== ================ =================

For example, writing the value 0x2000 to word 1 requests a virtual
memory mapping starting at 0x2000 with size 8192, ie for the 
addresses 0x2000-0x3FFF.

Default addresses
-----------------

Using the default configuration placing the interface at 0x300,
the ports can be accessed as follows:

====== ======================= ======================= ==========================================
Word   Address on 32-bit cores Address on 64-bit cores Summary
====== ======================= ======================= ==========================================
0      0x300                   0x300                   Map 4096 bytes, PID 0
1      0x304                   0x308                   Map 8192 bytes, PID 0
2      0x308                   0x310                   Map 16384 bytes, PID 0
3      0x30C                   0x318                   Map 32768 bytes, PID 0
4      0x310                   0x320                   Map 65536 bytes, PID 0
5      0x314                   0x328                   Map 131072 bytes, PID 0
6      0x318                   0x330                   Map 262144 bytes, PID 0
7      0x31C                   0x338                   Map 524288 bytes, PID 0
8      0x320                   0x340                   Unmap 4096 bytes, any PID
9      0x324                   0x348                   Unmap 8192 bytes, any PID
10     0x328                   0x350                   Unmap 16384 bytes, any PID
11     0x32C                   0x358                   Unmap 32768 bytes, any PID
12     0x330                   0x360                   Unmap 65536 bytes, any PID
13     0x334                   0x368                   Unmap 131072 bytes, any PID
14     0x338                   0x370                   Unmap 262144 bytes, any PID
15     0x33C                   0x378                   Unmap 524288 bytes, any PID
16     0x340                   0x380                   Map 4096 bytes, current PID
17     0x344                   0x388                   Map 8192 bytes, current PID
18     0x348                   0x390                   Map 16384 bytes, current PID
19     0x34C                   0x398                   Map 32768 bytes, current PID
20     0x350                   0x3A0                   Map 65536 bytes, current PID
21     0x354                   0x3A8                   Map 131072 bytes, current PID
22     0x358                   0x3B0                   Map 262144 bytes, current PID
23     0x35C                   0x3B8                   Map 524288 bytes, current PID
24     0x360                   0x3C0                   Unmap all ranges of given PID
25     0x364                   0x3C8                   Set current PID.
====== ======================= ======================= ==========================================

SEE ALSO
========

mgsim(1), mgsimdoc(7), mgsim-memranges(7)

BUGS
====

Report bugs & suggest improvements to PACKAGE_BUGREPORT.




