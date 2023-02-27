#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
  uint8_t x, y, z;
  x = (uint8_t) atoi (argv[1]);
  y = (uint8_t) atoi (argv[2]);

  if ((uint8_t)(151 * (39 * ((x ^ y) + 2 * (x & y)) + 23) + 111) > (uint8_t)((x ^ y) + 2 * (x & y)))
  {
    z = x & y;
  }

  else if ((uint8_t)(x-y + 2*(~x&y) - (x^y)) == 0x17)
  {
    z = x | y;
  }

  else if ((uint8_t)(195 + 97*x + 159*y + 194*~(x | ~y) + 159*(x ^ y) + (163 + x + 255*y + 2*~(x | ~y) + 255*(x ^ y))* (232 + 248*x + 8*y + 240*~(x | ~y) + 8*(x ^ y)) - 57) < 100)
  {
    z = x ^ y;
  }
  
  else {
    z = 0;
  }

  printf("z = %d\n", z);
  return 0;
}
