==============
 mgsimdev-lcd
==============

-------------------------------------------------
 Character matrix display pseudo-device in MGSim
-------------------------------------------------

:Author: MGSim was created by Mike Lankamp. MGSim is now under
   stewardship of the Microgrid project. This manual page was written
   by Raphael 'kena' Poss.
:Date: August 2012
:Copyright: Copyright (C) 2008-2012 the Microgrid project.
:Version: PACKAGE_VERSION
:Manual section: 7

DESCRIPTION
===========

The MG matrix display is text-oriented output device. It provides both
grid addressing of individual characters and a serial interface with
automatic management of line feeds, tabs and scrolling.

The matrix display is rendered in text mode on the terminal where
``mgsim`` runs, using ANSI escape codes.

An I/O device of this type can be specified in MGSim using the device
type ``LCD``.

CONFIGURATION
=============

Each ``lcd`` device support the following configuration variables:

``<dev>:LCDDisplayWidth``, ``<dev>:LCDDisplayHeight``
   Size in characters of the display device.

``<dev>:LCDOutputRow``, ``<dev>:LCDOutputColumn``
   Position in the terminal where the matrix display is rendered. 

``<dev>:LCDBackgroundColor``, ``<dev>:LCDForegroundColor``
   Terminal colors to use when rendering the matrix display. This
   uses standard ANSI color codes.

``<dev>:LCDTraceFile``
   File name to output every byte sent to the matrix display.

PROTOCOL
========

Writing characters at specific positions
----------------------------------------

Sending *I/O write* requests to the first WxH bytes, where W and H are
the display size in characters, causes the corresponding characters to
be displayed. Only printable characters are recognized.

The characters are organized in row-major order, that is bytes
0...(W-1) correspond to the first row of characters, W...(2W-1) to the
second row, and so on.

Accessing the display size
--------------------------

When queried using an *I/O read* request to offset 0 and size 32 bits,
the pseudo-device will report the matrix display size.  The low-order
16 bits indicate the height, and the high-order 16 bits indicate the
width. This must be accessed as a single 32-bit operation.

Serial output
-------------

The special offset W*H supports a serial output terminal with internal
display cursor.  Each character sent at that offset is printed at the
current cursor position, and the cursor position is modified
automatically. The pseudo-terminal recognizes newline (\\n),
tabulations (\\t), form feeds (\\f), carriage returns (\\r) and
backspaces (\\b). Lines wrap automatically, and outputs beyond the last
display line cause the existing characters to "scroll up".


INTERFACE
=========

The device presents itself to the I/O bus as a single device. 

========== ============ ====== =============================
Address    Access width Mode   Description
========== ============ ====== =============================
0          4 bytes      Read   Display size (see below)
0 to W*H-1 (any)        Write  Grid-addressed output buffer
W*H        1 byte       Write  Serial output
========== ============ ====== =============================

SEE ALSO
========

mgsim(1), mgsimdoc(7)

BUGS
====

Report bugs & suggest improvements to PACKAGE_BUGREPORT.




