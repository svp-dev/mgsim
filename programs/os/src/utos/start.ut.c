#include <utos/switchboard.ut.h>
#include <utos/mem.ut.h>
#include <utos/io.ut.h>
#include <utlib/string.ut.h>

extern thread t_main(shared int, int, char *argv[]);

char __name[] = "t_main";
char * __argv[] = { __name, 0 };
char __message[] = "Main terminates, exit code: XXXXXXXXXXXXXXXXXXXX";

thread void _start() {

  family tfid;
  family mfid;

  /**** INITIALIZE THE SWITCHBOARD ****/

  /* start the switchboard manager in a persistent
     family. The thread does not terminate. */
  create(mfid;;1;1;1;0;) utsys_sb_manager();

  /**** INITIALIZE THE ENVIRONMENT ****/

  /* register memory allocation */
  create(tfid;;1;1;1;0;) utsys_mem_init();
  sync(tfid);

  /* register I/O services */
  create(tfid;;1;1;1;0;) utsys_io_init();
  sync(tfid);

  /**** START THE MAIN THREAD ****/

  /* set up the initial arguments */
  int exit_code = 0;
  int argc = 1;
  char **argv = &__argv;

  /* create and sync */
  create(tfid;;1;1;1;0;) t_main(exit_code, argc, argv);
  sync(tfid);

  /**** PRINT AN EXIT MESSAGE ****/

  /* we want to format the exit code into a string */

  char *msg = &_message;
  msg += sizeof(_message) - 20; /* location of format space */

  /* convert the integer to string */
  create(tfid;;1;1;1;0;) str_print_int(msg, exit_code);
  sync(tfid);
  /* add a trailing newline */
  *msg++ = '\n';
  
  /* perform the I/O */
  UTSYS_IO_WRITE(msg - &_message, &_message);

  /**** CLEANUP ****/
  kill(mfid);
  sync(mfid);
}
