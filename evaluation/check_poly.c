#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

uint8_t P(uint8_t x) { return 97*x + 248*x*x; }
uint8_t Q(uint8_t x) { return 161*x + 136*x*x; }

int main(int argc, char* argv[])
{
  uint8_t i = 0;
  do
  {
      if (P(Q(i+argc)) != (i+argc)) { printf("P(Q(X)) != X)\n"); return -1; }
      i++;
  } while (i != 0);
  printf("P(Q(X)) = X\n");
  return 0;
}
