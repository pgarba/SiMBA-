#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

uint8_t E(uint8_t x, uint8_t y) { return x-y + 2*(~x&y) - (x^y); }

int main(int argc, char* argv[])
{
  uint8_t i = 0; uint8_t j = 0;
  do
  {
      do
      {
          if (E(i, j) != 0) { printf("E(x, y) != 0)\n"); return -1; }
          j++;
      } while (j != 0);
      i++;
  } while (i != 0);
  printf("E(x, y) = 0\n"); return 0;
}
