==============
 mgsimdev-gfx
==============

----------------------------------------------
 Graphical framebuffer pseudo-device in MGSim
----------------------------------------------

:Author: MGSim was created by Mike Lankamp. MGSim is now under
   stewardship of the Microgrid project. This manual page was written
   by Raphael 'kena' Poss.
:Date: October 2014
:Copyright: Copyright (C) 2008-2014 the Microgrid project.
:Version: PACKAGE_VERSION
:Manual section: 7


DESCRIPTION
===========

The graphical display is a 2D pixel-oriented output device with
windowing acceleration.

An I/O device of this type can be specified in MGSim using the device
type ``GFX``.

A "device driver" libray is provided with MGSim as
``programs/gfxlib.{c,h}``.

CONFIGURATION
=============

``GfxFrameSize``
   Size of the video memory in bytes.

``GfxEnableSDLOutput``
   Enable rendering the framebuffer to screen outside of the
   simulation. When this is not enabled, software running on the
   simulated platform can update the framebuffer but no pixels are
   displayed on screen.

``SDLRefreshDelay``
   Defines how many simulation cycles to wait between updates to the
   output display, when enabled. Can be adjusted at run-time.

PROTOCOL
========

The device is exposed to software as a pair of interfaces: a control
interface which can set general display parameters, and a frame buffer
interface which provides raw access to the video memory.

The process of generating an output display image is performed by the
GFX device using a *command array* stored by software in video
memory. The commands in this array can configure color palettes or
render 2D textures of variable sizes and depths, whose data is
specified elsewhere in video memory, with automatic scaling to a
display size.

The start address of the command array in video memory is configurable
using the control interface.


Changing the display resolution
-------------------------------

When read from, words 1-2 of the control interface indicate the
current logical display size in pixels.  The values in words 6 and 7 indicate
the maximum supported resolution.

The following protocol can be used to configure a new output
resolution:

1. the desired size is written into words 1-2 of the control interface;

2. the command word 0 is written to, to commit the desired size;

3. the words 1-2 are read back to check whether the size was accepted.

The desired resolution is rounded up to the nearest valid resolution,
which must be one of the following: 10x10, 100x100, 160x100, 160x120,
320x200, 320x240, 640x400, 640x480, 800x600, 1024x768, 1280x1024.

Unrecognized size configurations are ignored and leave the previous mode
unchanged.

Changing mode vs clearing screen
--------------------------------

As described above, writing to word 0 of the control device sets a new
display mode.

If the value 0 is written to word 0, the new mode is set but existing
pixels on the output screen are preserved (up to the minimum of the
old and new display sizes).

If the value 1 is written, the new mode is set *and* the output screen
is cleared.

Screen dump
-----------

Writing to control word 4 outputs the contents of the output screen to
a portable pixmap (PPM) image in the simulation's host environment.

The value written to control word 4 further configures the screen
dump, as follows:

- bits 0-1 determine where the PPM data is output to. The value 0
  causes the data to be output to a file. The value 1 causes the data
  to be printed to MGSim's standard output stream. The value 2 outputs
  to MGSim's standard error stream.

- bit 8 determines whether to embed a timestamp in the file name when
  bits 0-1 are set to 0.


Display commands
----------------

The command array in video memory is an array of 32-bit values. Each
value in the array indicates how the following elements are to be
interpreted, using the table below:

========= ======================== ===========================
Command   Decoded as               Meaning
========= ======================== ===========================
0x000     Code 0x000 + 0 arguments End of the command array.
0x102     Code 0x100 + 2 arguments Set the indexed palette (see below).
0x206     Code 0x200 + 6 arguments Render a frame (see below).
0x3NN     Code 0x300 + N arguments Do nothing.
========= ======================== ===========================

Code 0x100, defines an indexed palette: the following two arguments N
and M define a palette of N elements, stored at byte offset 4xM from
the start of the video memory. Each element is a 32-bit value
interpreted as an RGB triplet (B bits 0-7, G bits 8-15, R bits 16-23).

Code 0x200, defines the rendering of a frame, ie. the scaling and rendering
of a texture on the output screen. The following 6 arguments B, S, L,
(W, H), (X, Y), (PW, PH) define:

- B - the pixel mode, how the bytes in video memory are translated into pixel values;
- S - start address: the texture starts in video memory at byte address 4xS;
- L - the scan length, ie the number of pixels in memory between two successive lines in the texture;
- W - the texture width, ie the number of pixels in memory actually drawn to screen;
- H - the number of texture lines to draw
- X, Y - the position of the frame on the output screen.
- PW, PH - the width and height of the frame in the output screen.

The values for W, H, X, Y, PW and PH are encoded in 16 bits, so W/H
are encoded as one 32-bit command element (W in MSB, H in LSB), so are
X/Y and PW/PH.

Code 0x300 is a no-op. It can be used to quickly disable a command in the
command array without shifting the remainder of the array.

Pixel modes
-----------

The 32-bit pixel mode for frame commands is defined as follows:

========== =========== ====== ====================
Bits 0-15  Bits 16-31  Value  Resulting pixel mode
========== =========== ====== ====================
1          1           65537  1-bit indexed
4          1           65540  4-bit indexed
8          0           8      RGB 2-3-3
8          1           65544  8-bit indexed
16         0           16     RGB 5-6-5
24         0           24     RGB 8-8-8
32         0           32     RGB 8-8-8, upper 8 bits ignored
========== =========== ====== ====================

When in RGB mode, the color components of the resulting pixels are
defined directly by the bits in the texture buffer. For example, in pixel mode 24,
3 adjacent bytes in the texture buffer define one pixel on screen, with
the first byte for red, 2nd byte for green, 3rd byte for blue. With
pixel mode 8, one byte of the framebuffer is decomposed as 3 values,
one value of 2 bits for red (bits 6-7), one value of 3 bits for green
(bits 4-6), and one value of 3 bits for blue (bits 1-3).

When in indexed mode, the value in the texture buffer is used as an
index in the palette defined by the last palette command. The palette
then defines which R/G/B values to use. In 1-bit and 4-bit indexed
mode, the order of pixels is from lowest significant to higest
significant.


INTERFACE
=========

The pseudo-device presents itself to the I/O bus as two logical
devices: the *control* interface and the *framebuffer* interface.

When multiple graphical outputs are connected to a bus, the device
identifier of the framebuffers can be matched to their control devices
via word 9 of the control device. (see below).

Control device
--------------

The gfx control device must be accessed using 32-bit I/O
operations. Its device address space is as follows:

============= ======= ===========================================
32-bit word   Mode    Description
============= ======= ===========================================
0             R       Boolean: indicates whether the physical screen is connected
0             W       Command: commit the mode configured using words 1-2, non-zero clears screen
1             R       Current logical width in pixels
1             W       Desired logical width in pixels
2             R       Current logical height in pixels
2             W       Desired logical height in pixels
3             R       Current start of command array in video memory
3             W       Set start of command array
4             W       Command: dump the framebuffer contents
5             R/W     Image index (key) for the next dump
6             R       Maximum supported physical width
7             R       Maximum supported physical height
8             R       Screen refresh interval in bus clock cycles
9             R       Device identifier of the corresponding framebuffer device on the I/O bus
============= ======= ===========================================

Framebuffer device
------------------

The framebuffer device can be accessed using any I/O data width, as
long as no address past the framebuffer size is accessed.

On the host side of the simulation
----------------------------------

When the screen output is enabled, the following keystrokes are
recognized:

Escape
   Closes the display.

Page down / Page up
   Modify the scaling factor quickly (how many output pixels are used
   to display each logical pixel)

Home / End
   Modify the scaling factor slowly.

Tab
   Restore the aspect ratio (set the horizontal scaling factor equal
   to the vertical factor).

Up / Down
   Increase / decrease the refresh delay (refresh rate).

R
   Reset the delay and scaling factor to the base configuration.

Moreover, the display window can be interactively resized using the
regular window size manipulation method (eg mouse) to adjust the
scaling factor at a finer grain.

SEE ALSO
========

mgsim(1), mgsimdoc(7)

BUGS
====

Report bugs & suggest improvements to PACKAGE_BUGREPORT.
