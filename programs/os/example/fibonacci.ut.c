
/*****************************************************************\
*                                                                 *
* Name:        Fibonacci                                          *
* Description: Calculates Fibonacci numbers 2 to N. Example code  *
*              to show and test the usage of shared variables.    *
*                                                                 *
\*****************************************************************/

/* Computes the N Fibonnaci Numbers from 2..(N-1) */

#include <utos/mem.ut.h>
#include <utlib/assert.ut.h>

thread void fibo_report(int i, int p1, int p2, int res);

thread void fibonacci_compute( shared int prev, shared int prev2, 
			       int *fibonacci)
{
    index i;

    /* Add the values of the two previous Fibonacci numbers to *
     * get the current number                                  */
    fibonacci[i] = prev + prev2;

    int pos, n1, n2, res;

    family fam;
    create(fam;;1;1;1;0;) fibo_report(pos = (int)i, n1 = prev, n2 = prev2,
				      res = fibonacci[i]);
    sync(fam);

    /* Pass on the previous and current Fibonacci number to the *
     * next thread                                              */
    prev2 = prev;
    prev = fibonacci[i];
}


thread void t_main(shared int exit_code, int argc, char *argv[])
{
    family fid;

    int n = 45;
    int *fibonacci;
    int prev, prev2;
    
    /* Allocate Array for Fibonacci Numbers from 0 to N */
    int sz = (n+1) * sizeof(int);
    UTSYS_ALLOC(sz, fibonacci);
    ASSERT(fibonacci != 0);
    
    /* Set two starting Fibonacci Numbers */
    fibonacci[0] = 0;
    fibonacci[1] = 1;

    /* Set shareds to initial values */
    prev2 = fibonacci[0];
    prev = fibonacci[1];
    
    /* Create N-1 threads of fibonacci_compute */
    create(fid;;;2;n;;;) fibonacci_compute(prev, prev2, fibonacci);

    /* Wait for all threads to complete */
    sync(fid); 

    /* Free memory */
    UTSYS_FREE(fibonacci);
   
    exit_code = 0;
}

thread void fibo_report(int i, int p1, int p2, int res) {

  char *str;
  int n;

  UTSYS_ALLOC(150, str);

  ASSERT(str != 0);

  create(fam;;1;1;1;0;) strcat(str, "The ");  sync(fam);
  create(fam;;1;1;1;0;) str_print_int(str, n = i);  sync(fam);
  create(fam;;1;1;1;0;) strcat(str, "th Fibonacci number is "); sync(fam);
  create(fam;;1;1;1;0;) str_print_int(str, n = p1); sync(fam);
  create(fam;;1;1;1;0;) strcat(str, " + ");  sync(fam);
  create(fam;;1;1;1;0;) str_print_int(str, n = p2); sync(fam);
  create(fam;;1;1;1;0;) strcat(str, " = ");  sync(fam);
  create(fam;;1;1;1;0;) str_print_int(str, n = res); sync(fam);
  create(fam;;1;1;1;0;) strcat(str, "\n");  sync(fam);

  UTIO_PUTS(str);

  UTSYS_FREE(str);
}
