
This is uTOS, a small operating system for the simulator.

Requirements
------------

- the Alpha-based simulator

- a _working_ (!) utc-to-alpha compiler.

Use instructions
----------------

To use proceed as follows:

- write a regular uTC program (e.g. example/fibonacci.ut.c). This can
  use the include files in <utos/...> and <utlib/...>

- compile the program (don't forget -I<simulator>/programs/os/include)

- compile all the sources for the OS (also don't forget -I...)

- link everything together, ensuring that a symbol __heap_start is
  added at the end of the executable by the linker (this needs a link
  script).

Note that the OS contains the entry point _start so that does not need
to be changed when linking.

Services offered by the OS
--------------------------

The OS proposes:

- automatic startup of the special thread "t_main"

- thread-safe dynamic memory allocation (and deallocation)

- thread-safe I/O

- assertions (prints messages, does not terminate the OS yet)

- a mechanism to synchronize access to resources


About the switchboard
---------------------

A swichboard mechanism is implemented. The switchboard is a single
thread created at boot time which is in charge of delivering
(synchronized) services to all other threads.

Dynamic memory allocation and I/O are services delivered by the
switchboard. Additional services could be added, for example muT place
reservation.

The way threads interact with the services is as follows
(e.g. memory):

- thread T creates a family of one thread using utsys_mem_alloc

- utsys_mem_alloc creates a family of one thread using utsys_sb_exchange,
  (thread function implementing the generic switchboard client)

- utsys_sb_exchange performs black magic to "acquire control of" the
  switchboard thread; 

meanwhile...

- the switchboard thread was asynchronously waiting for an acquisition
  request. The request from utsys_sb_exchange is received.

- the switchboard becomes "busy" (no other thread can use it)

meanwhile...

- utsys_sb_exchange "requests" a memory allocation from the switchboard, 
  then waits for completion

meanwhile...

- the switchboard creates a family of one thread to serve the request,
  namely using thread function _mem_do_alloc.

- _mem_do_alloc performs allocation. It has exclusive accesses to its
  data structures since it can only be invoked from the switchboard
  thread.

- the switchboard syncs on _mem_do_alloc then replies to
  utsys_sb_exchange with the results.

meanwhile...

- utsys_sb_exchange "receives" the results, then notifies
  the switchboard that it can be released.

meanwhile...

- the switchboard becomes passive and "available" again.

meanwhile...

- utsys_mem_alloc syncs, receives the results from utsys_sb_exchange.

- thread T syncs, it receives the results, i.e. the pointer to
  dynamically allocated memory.








