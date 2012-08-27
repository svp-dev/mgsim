===============
 mgsimdev-uart
===============

------------------------------------
 Serial UART pseudo-device in MGSim
------------------------------------

:Author: MGSim was created by Mike Lankamp. MGSim is now under
   stewardship of the Microgrid project. This manual page was written
   by Raphael 'kena' Poss.
:Date: August 2012
:Copyright: Copyright (C) 2008-2012 the Microgrid project.
:Version: PACKAGE_VERSION
:Manual section: 7


DESCRIPTION
===========

The MG UART device allows to connect the Microgrid to an external
(serial) terminal. It tries to mimics the NS/PC16550D standard
Universal Asynchronous Receiver-Transmitter (UART) chip.

An I/O device of this type can be specified in MGSim using the device
type ``UART``.

CONFIGURATION
=============

Each ``uart`` device support the following configuration variables:

``<dev>:UARTInputFIFOSize``, ``<dev>:UARTOutputFIFOSize``
   Number of bytes held by the UART FIFO.

``<dev>:UARTConnectMode``
   How the UART is connected to the "outside world" (the environment
   where the simulation is run). Can be either of the following:

   ``FILE``
       Data is read/written from/to a standard file, whose name
       is specified by ``<dev>:UARTFile``.

   ``FILEPAIR``
       Data is read from one file, and written to another. The two
       file names are specified with ``<dev>:UARTInputFile`` and
       ``<dev>:UARTOutputFile``.

   ``STDIO``
       The UART is connected to the standard Unix input and output
       streams of MGSim itself. Note that this mode is incompatible
       with MGSim's interactive mode (``-i``).

   ``PTY``
       A POSIX pseudo-terminal (pty) is allocated and connected to
       the UART. The corresponding slave device is printed
       by MGSim prior to the simulation start-up.

PROTOCOL
========

The pseudo-device follows the protocol of the standard NS/PC16650D
chip, with the following exceptions:

- Line and MODEM status are not supported. However loopback mode can be
  configured and used (MCR4)
- the FIFO mode is always enabled, and cannot be disabled (FCR0 is
  fixed to 1).
- the FIFO buffers cannot be reset (FCR1/2 are inoperative)
- DMA is not supported (FCR3 is inoperative)
- transmit speed / divisor latch is not supported (and DLAB is inoperative)

INTERFACE
=========

The UART device presents itself to the I/O bus as a single device. It
must be accessed using 8-bit I/O operations. Its device address space
follows the NS/PC16550D specification as follows:

======== ======= =====================================
Register Mode    Description
======== ======= =====================================
0        R       Receiver Buffer Register / FIFO input
0        W       Transmit Hold Register (THR) / FIFO output
1        R/W     Interrupt Enable Register (IER)
2        R       Interrupt Identification Register (IIR)
2        W       FIFO Control Register (FCR)
3        R/W     Line Control Register (LCR)
4        R/W     MODEM Control Register (MCR)
5        R/W     Line Status Register (LSR)
6        R/W     MODEM Status Register (MSR)
7        R/W     Scratch register
8        R/W     (MG extension) Interrupt channel for THRE interrupts
9        R/W     (MG extension) Interrupt channel for output FIFO underruns
10       R/W     (MG extension) UART enable/disable
======== ======= =====================================

To change the notification channel number, it is recommended to first
disable notifications (reset IER1), so as to cancel any pending
notification to the old channel number.

SEE ALSO
========

* mgsim(1), mgsimdoc(7)

* NS/PC16650D specification: http://www.national.com/ds/PC/PC16550D.pdf

BUGS
====

Report bugs & suggest improvements to PACKAGE_BUGREPORT.
