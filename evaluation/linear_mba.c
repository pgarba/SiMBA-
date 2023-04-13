#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

uint8_t E1(uint8_t x, uint8_t y)
{ return x + y; }
uint8_t E2(uint8_t x, uint8_t y)
{ return (x ^ y) + 2 * (x & y); }
uint8_t E3(uint8_t x, uint8_t y)
{ return 151 * (39 * ((x ^ y) + 2 * (x & y)) + 23) + 111; }

int main(int argc, char* argv[])
{
  uint8_t x = (uint8_t) atoi (argv[1]);
  uint8_t y = (uint8_t) atoi (argv[2]);
  printf ("%s(%d, %d) = %d\n", "E1", x, y, E1(x, y));
  printf ("%s(%d, %d) = %d\n", "E2", x, y, E2(x, y));
  printf ("%s(%d, %d) = %d\n", "E3", x, y, E3(x, y));
  return 0;
}
