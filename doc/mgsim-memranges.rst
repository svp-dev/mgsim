=================
 mgsim-memranges
=================

-------------------------------------------------------
 Visible address ranges for programs running on MGSim
-------------------------------------------------------

:Author: MGSim was created by Mike Lankamp. MGSim is now under
   stewardship of the Microgrid project. This manual page was written
   by Raphael 'kena' Poss.
:Date: October 2012
:Copyright: Copyright (C) 2008-2012 the Microgrid project.
:Version: PACKAGE_VERSION
:Manual section: 7

DESCRIPTION
===========

The following table provides a high-level overview of the memory address space,
as seen from the perspective of software running on the MGSim platform.

================ ================ ====== ==== === ================================ =====================
Start            End              Size   Mode A/C Description                      Related documentation
================ ================ ====== ==== === ================================ =====================
               0                3      4      A   Invalid (NULL pointer detection) 
               4                7      4  R   A   Reset register to empty          
               8              1FF    504  R   A   Perf. counters                   mgsim-perfcounters(7)
             200              22F     48   W  A   Debug line to MGSim stdout       mgsim-control(7)
             230              25F     48   W  A   Debug line to MGSim stderr       mgsim-control(7)
             260              29F     64   W  A   MGSim control interface          mgsim-control(7)
             300              3FF    208   W  A   MMU control                      mgsim-control(7)
             400              4FF    256  R   A   Ancillary system registers
             500              5FF    256  RW  A   Ancillary program registers
             400              FFF     3K  --  --  RESERVED
                                                  
            1000         6FFF0000   1.7G  --  --  Available for program
                                                  (used by 32-bit linker)

        6FFF0000         6FFF0FFF     4K  RW  C   Argv[] data [2]_                 mgsimdev-arom(7)
        6FFF1000         6FFFFF00    59K  RW  C   MGSim config data [2]_           mgsimdev-arom(7), mgsimconf(7)
                                                  
        6FFFFF00         6FFFFFFF    256  RW  C   I/O notification channels [3]_   
        70000000         7FFFFFFF   256M  RW  C   I/O device access [3]_           
                                                  
        80000000         FFFFFFFF     2G  RW  A   32-bit only: TLS area            

        80000000 7FFFFFFFFFFFFFFF     8E  --  --  Available for program
                                                  (used by 64-bit linker)
                                                  
8000000000000000 FFFFFFFFFFFFFFFF     8E  RW  A   TLS area (64-bit)                mgsim-tls(7)
================ ================ ====== ==== === ================================ =====================

Addresses are given in hexadecimal and sizes in decimal.  The column
"Mode" indicates read (R) or write (W) capabilities. The column "A/C"
indicates whether the range address is an architectural constant (A)
or configurable per platform instance (C) [1]_.

NOTES
=====

.. [1] The difference between architectural constants and
   configurable parameters is that the program code must be recompiled
   when architectural constants change, whereas the same program binary
   should be portable to multiple platforms with different configurable
   parameter values.

.. [2] The ``argv[]`` and MGSim configuration data is *primarily*
   available as two ActiveROM devices (``rom_argv`` and
   ``rom_config``, cf. mgsimdev-arom(7), content sources ``ARGV`` and
   ``CONFIG``.). They are thus primarily reachable using regular
   memory-mapped I/O via the I/O device access ranges
   (0x70000000..0x7FFFFFFF), on cores where I/O is reachable.

   As a *secondary* mechanism, these two devices are pre-configured so
   that a DCA transfer will copy their contents in *main memory* in
   the address range 0x6FFF0000..6FFFFF00. This mechanism is provided
   for convenience, so that a lightweight test program can access this
   data without the overhead of DCA configuration, by simply defining
   ``PreloadROMtoRAM`` for these two devices.

.. [3] The memory-mapped I/O notification channels and device access
   mechanisms are only available on cores with ``EnableIO`` defined to
   ``true``. As of MGSim 3.3, only core 0 has this enabled by default.

SEE ALSO
========

mgsim(1), mgsimdoc(7), mgsimconf(7), mgsimdev-arom(7)

BUGS
====

Report bugs & suggest improvements to PACKAGE_BUGREPORT.

