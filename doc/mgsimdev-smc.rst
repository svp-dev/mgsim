==============
 mgsimdev-smc
==============

-----------------------------------------------------
 System Management Controller pseudo-device in MGSim
-----------------------------------------------------

:Author: MGSim was created by Mike Lankamp. MGSim is now under
   stewardship of the Microgrid project. This manual page was written
   by Raphael 'kena' Poss.
:Date: July 2013
:Copyright: Copyright (C) 2008-2013 the Microgrid project.
:Version: PACKAGE_VERSION
:Manual section: 7


DESCRIPTION
===========

The SMC is a helper device that provides IO Network enumeration.
An I/O device of this type can be specified in MGSim using the device
type ``SMC``.

PROTOCOL
========

After system start-up, the SMC serves as an enumerator for the I/O
interconnect where it is attached: explicit *I/O read* requests will
be answered with information about what devices are attached.

The enumeration data is composed of 8-byte packets.

The first 8 bytes indicate how many clients are attached to the I/O
interconnect.

Subsequent packates of 8 bytes enumerate the devices. For now, only
bytes 2-3 are used in each packet, and indicate the device type. Which
identifier correspond to which device type is given by the command
``show devicedb`` at MGSim's interactive prompt (``-i``).

INTERFACE
=========

The SMC device presents itself to the I/O bus as a single read-only
device. It can be accessed using any data size.

Its contents are as follows:

============= ======= ===========================================
Bytes         Mode    Description
============= ======= ===========================================
0-7           R       Number of I/O devices attached to the same interconnect
8-15          R       Enumeration data for device 0
16-23         R       Enumeration data for device 1
(...)         R       (...)
============= ======= ===========================================

SEE ALSO
========

mgsim(1), mgsimdoc(7)

BUGS
====

Report bugs & suggest improvements to PACKAGE_BUGREPORT.
