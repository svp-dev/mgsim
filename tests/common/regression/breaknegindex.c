#include <svp/testoutput.h>
#include <svp/delegate.h>

#define SAMPLES 200
int indices[SAMPLES];

sl_def(icount, void,
      sl_shparm(unsigned, count),
      sl_glparm(unsigned, max))
{
 sl_index(i);
 if (sl_getp(count) >= sl_getp(max)) {
     sl_setp(count, sl_getp(count));
     sl_break ;
 }
 indices[sl_getp(count)] = i;
 sl_setp(count, sl_getp(count) + 1);
}
sl_enddef

sl_def(iprint, int,
      sl_shparm(unsigned, count),
      sl_glparm(unsigned, refcount))
{
 sl_index(i);
 unsigned c = sl_getp(count);
 if (c >= sl_getp(refcount))
 {
     sl_setp(count, c);
     sl_break ;
 }
 output_int(c, 1);
 output_char(' ', 1);
 output_int(indices[c], 1);
 output_char('\n', 1);
 sl_setp(count, c + 1);
}
sl_enddef

const char *testconf = "\0PLACES: 1"; // for "make check": this program is single-threaded.

int test(void)
{
 int r;

 sl_create(,, -10,200,10, 0, ,
           icount,
           sl_sharg(unsigned, count, 0),
           sl_glarg(unsigned, max, 20));
 sl_sync(r);
 if (r == SVP_EXIT_BREAK) 
     return 1; // should not see break here

 if (sl_geta(count) != 20)
     return 1;

 sl_create(,,, sl_geta(count),,,,
           iprint,
           sl_sharg(unsigned, c, 0),
           sl_glarg(unsigned, refc, sl_geta(count)));
 sl_sync();

 return 0;
}

