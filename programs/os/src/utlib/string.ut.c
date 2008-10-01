#include <utlib/string.ut.h>

thread void strlen(shared int n, const char *str)
{
  int i = 0;
  while (str[i]) i++;
  n = i;
}

thread void strcat(shared char *str, const char *s2) {
  const char *p = str;
  while(*p)
    p++;
  const char *p2 = s2;
  while (*p = *p2++) 
    p++;
  str = p;
}

thread void str_print_int(shared char *str, int val) {
  char *s = str;
  if (val < 0)
    *s++ = '-';
  long i, d;
  for (i = ((val < 0) ? -1 : 1); (val / i) >= 10; i *= 10);
  while (i)
    { 
      d = val / i;
      d = ((d < 0) ? -d : d);
      *s++ = d + '0';
      val -= d * i;
      i /= 10;
    };
  *s = '\0';
  str = s;
}
