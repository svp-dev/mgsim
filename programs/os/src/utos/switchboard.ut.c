#include "switchboard.ut.h"

#define COMMIT __asm__ volatile("membarrier\n\tswch" : : : "memory")

#define UNIQUE(r) __asm__ volatile("rpcc %0" : "=r"(r) : : "memory") 

/* fixed locations in memory: */
volatile long __syn;
volatile long __hold;
volatile long __ack;
volatile long __code;
volatile void *__ptr;

/* service pointers */
server_t * __srv_table[256];

thread void _set_srv(shared int code, shared void *p) {
  int code_to_register = (code >> 8) & 0xff;
  server_t *previous = __srv_table[code_to_register];
  __srv_table[code_to_register] = p;
  COMMIT;
  p = (void*) previous;
  code = 0; /* success */
}



thread void _actionate(shared int code, shared void *p) {
  server_t *srv = __srv_table[code & 0xff];
  if (!srv) {
    p = 0;
    code = -1; /* failure */
  }
  family fid;
  create(fid;;1;1;1;0;) (*srv)(code, p);
  sync(fid);
}


thread void _manager_() {
  /* this thread runs permanently on one CPU */

  long s;
  long srvnum;
  void *p;
  
  for (s = 1; s < 256; ++s)
    __srv_table[s] = 0;
  __srv_table[1] = _set_srv;

  while(1) {
    /* wait for an acquisition */
    do { COMMIT; s = __syn; } while(!s);
    /* someone requests the switchboard, reply */
    __hold = s;

    /* wait for the service code */
    do { COMMIT; srvnum = __code; } while(!srvnum);

    /* acknowledge, wait for the pointer */
    __ack = s;
    do { COMMIT; p = __ptr; } while(!p);
    
    /* everything ready, run the service */

    family f;
    create(f;0;1;1;1;0;) _actionate(srvnum, p);
    sync(f);

    /* service completed */

    /* write the output, then wait for release */
    __code = srvnum;
    __ptr = p;
    __ack = s;
    do { COMMIT; s = __hold; } while(s);
    
    /* transaction completed, reset the switchboard */
    __ack = __code = 0;
    __ptr = 0;
    COMMIT;
    __syn = 0;
  }
}

thread void exchange(shared int code, shared void* data) {

  long id;
  long s;
  void *p;

  /* acquire the communication channel to the switchboard */
  while(1) {
    UNIQUE(id);
    __syn = id;
    COMMIT;
    do { s = __hold; } while(!s);
    if (s == id)
      break;
    /* otherwise, wait and try again */
    do { s = __syn; } while(s);
  }
  /* got it */

  /* send the service code, then wait */
  __code = code;
  do { COMMIT; s = __ack; } while(!s);
  /* switchboard ready*/

  /* clear the flag and send the pointer, then wait */
  __ack = 0; 
  __ptr = data;
  do { COMMIT; s = __ack; } while(!s);
  /* switchboard has completed */

  /* read the result */
  p = __ptr;
  s = __code;

  /* release the switchboard */
  __hold = 0;
  COMMIT;

  /* return the result to the parent family */

  data = p;
  code = s;
}

