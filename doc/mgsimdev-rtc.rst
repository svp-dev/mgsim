==============
 mgsimdev-rtc
==============

----------------------------------------
 Real time clock pseudo-device in MGSim
----------------------------------------

:Author: MGSim was created by Mike Lankamp. MGSim is now under
   stewardship of the Microgrid project. This manual page was written
   by Raphael 'kena' Poss.
:Date: August 2012
:Copyright: Copyright (C) 2008-2012 the Microgrid project.
:Version: PACKAGE_VERSION
:Manual section: 7


DESCRIPTION
===========

The real-time clock allows programs running on the platform to sample
real time (outside of the simulation environment) and signal
asynchronous events to the programs at configurable time periods.

An I/O device of this type can be specified in MGSim using the device
type ``RTC``.

CONFIGURATION
=============

``RTCMeatSpaceUpdateInterval``
  Mimimal real-time interval at which the RTC will check the time and
  trigger events. This defines the clock's resolution.

Note: a higher resolution will degrade the simulation speed.

PROTOCOL
========

Reading the time
----------------

When queried explicitly using *I/O read* requsts, words 6-9 provide a
broken-down description of the current time and date, analogous to
``struct tm`` in C. 

The format is as follows:

==== ======= =======================
Word Bits    Description
==== ======= =======================
6,8  0-5     Seconds (0-59)
6,8  6-11    Minutes (0-59)
6,8  12-16   Hours (0-23)
6,8  17-21   Day in month (0-30)
6,8  22-25   Month in year (0-11)
6,8  26-31   Number of years since 1970 (0-63)
7,9  0-3     Day of week (Sunday = 0)
7,9  4-12    Day of year (0-365)
7,9  13      Whether summer time is in effect
7,9  14-31   Offset from UTC in seconds (0-86399)
==== ======= =======================

Notifications
-------------

The RTC pseudo-device can also be configured to signal (notify) the
system at specified real time intervals. This is set using word 1
(delay) and word 2 (which notification channel to use).

If the I/O bus is busy it is possible that the notification cannot be
sent for an entire period. In this case, the value of word 3
determines whether intermediate notifications are skipped or whether
they accumulate, to be delivered eventually.

INTERFACE
=========

The RTC device presents itself to the I/O bus as a single device. It
must be accessed using 32-bit I/O operations. Its device address space
is as follows:

============= ======= ===========================================
32-bit word   Mode    Description
============= ======= ===========================================
0             R       Clock resolution in microseconds of real time
1             R/W     Notification delay (in microseconds, set to 0 to disable notifications)
2             R/W     Notification channel to use for notifications
3             R/W     Boolean: whether to deliver all events
4             R       Microseconds part of the current Greenwich time since Jan 1, 1970
5             R       Seconds part of the current Greenwich time since Jan 1, 1970
6             R       Packed UTC time/date (part 1, see below)
7             R       Packed UTC time/date (part 2, see below)
8             R       Packed local time/date (part 1, see below)
9             R       Packed local time/date (part 2, see below)
============= ======= ===========================================

To change the notification channel number, it is recommended to first
disable notifications (set the delay to 0), so as to cancel any
pending notification to the old channel number.

SEE ALSO
========

mgsim(1), mgsimdoc(7)

BUGS
====

Report bugs & suggest improvements to PACKAGE_BUGREPORT.


