#include <utlib/assert.ut.h>
#include <utlib/string.ut.h>
#include <utos/io.ut.h>

static const char __bailout[] = "Assertion failed; no memory for message.\n";

thread void __assert(const char *exmsg, const char *fname, int line) {
  
  char *msg;
  int sz, sz1, sz2;
  family fid1, fid2;

  create(fid1;;1;1;1;0;) strlen(sz1, exmsg);
  create(fid2;;1;1;1;0;) strlen(sz2, fname);
  sync(fid2); 
  sync(fid1);
  
  sz = sz1 + sz2 + 80;
  UTSYS_ALLOC(sz, msg);
  
  if (!msg)
    UTSYS_IO_WRITE(sizeof(__bailout)-1, &__bailout);
  else {
    char *p = msg; 
    char *p2;
    create(fid1;;1;1;1;0;) strcat(p, "Assertion failed: ("); sync(fid1);
    create(fid1;;1;1;1;0;) strcat(p, p2 = exmsg); sync(fid1);
    create(fid1;;1;1;1;0;) strcat(p, "), file "); sync(fid1);
    create(fid1;;1;1;1;0;) strcat(p, p2 = fname); sync(fid1);
    create(fid1;;1;1;1;0;) strcat(p, ", line "); sync(fid1);
    create(fid1;;1;1;1;0;) str_print_int(p, sz = line); sync(fid1);
    create(fid1;;1;1;1;0;) strcat(p, "\n"); sync(fid1);
    UTIO_PUTS(msg);

    UTSYS_FREE(msg);
  }

}

