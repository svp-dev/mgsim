Alpha Microgrid Simulator
by Mike Lankamp at the University of Amsterdam

Contents
=========
1. Building it
   1.1 Windows 
   1.2 Linux & Unix
   1.3 Build options

2. Running it
   2.1 Command-line
   2.2 Interactive

3. Advanced Features
   3.1 Debugging
   3.2 Profiling

4. Bugs
5. TODO list


1. Building it
===============

1.1 Windows 
------------
Use the vcproj files to load and build the simulator and assembler.
You may, of course, also use the command-line compiler (cl.exe), but a
makefile is not (yet) supplied for Windows.

1.2 Linux, Unix and MacOS
--------------------------
The simulator will build warning-free with gcc. It has been succesfully
compiled with GCC 3.3 and GCC 4.0.

1.3 Build options
------------------
Currently, the following macros can be predefined (with -D<macro>) for
certain behavior:

* NDEBUG; will strips all asserts and other verification and sanity-checking
  code for increased performance. This is NOT related to the Simulator's
  debug on/off setting. It is highly recommended to set this macro!

* PROFILE; will enable profiling (see section 3.2, Profiling).



2. Running it
==============

2.1 Command-line
-----------------
Run the simulator with --help to show the usage and a list of supported
command-line switches.
Without the --interactive switch, the simulator will run the code from
the program file and print the number of cycles (in decimal) that it took
to execute it.

Note: should deadlock occur, the simulator will not notify you. Instead,
it will hang (see TODO list).

2.2 Interactive
----------------
When the simulator is started with the --interactive switch, the simulator
will be initialized with the configuration and program file, and a prompt
will be displayed. The prompt is the current cycle count of the simulator.
At the prompt, you can enter commands to step the simulator, or examine
various components. Type "help" at the prompt for a list of commands.



3. Advanced Features
=====================

3.1 Debugging
--------------
In interactive mode, the command "debug all" will enable printing of debug
messages in the simulator. This could be helpful to figure out what is
happening and if deadlock occurs, where it occurs.
Debug messages are printed in the simulator by use of the DebugWrite()
function and are displayed when "debug sim" is used.
You can also output debug information into the simulated program
by using the "debug" and "debugf" assembly instructions. Upon executing these
instructions, the simulator will output the register value. Use "debug prog" to
enable this behavior.
Note that "debug all" enables both "sim" and "prog" debugging.

3.2 Profiling
--------------
In the code, profile sections can be placed via the PROFILE_BEGIN and
PROFILE_END macros. The simulator will time the execution of the code between
those statements.
At any time, the current profile times can be listed with the "profiles"
command in interactive mode. Currently, the percentages only make sense
if the sections do not overlap (see TODO list).

Note: profiling only works when the simulator has been built with the PROFILE
macro.



4. Bugs
========
There are no known bugs.



5. TODO list
=============
* Change profiling code to deal with overlapping profile sections.
* Add deadlock-checking code.
