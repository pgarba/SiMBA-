#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

uint8_t OC(uint8_t x, uint8_t y)
{
    return 195 + 97*x + 159*y +
    194*~(x | ~y) + 159*(x ^ y) +
    (163 + x + 255*y + 2*~(x | ~y) + 255*(x ^ y))*
    (232 + 248*x + 8*y + 240*~(x | ~y) + 8*(x ^ y));
}

int main(int argc, char* argv[])
{
    uint8_t i = 0; uint8_t j = 0;
    do
    {
        do
        {
            if (OC(i, j) != 123)
            {
                printf("OC(x, y) != 123)\n");
                return -1;
            }
            j++;
        } while (j != 0);
        i++;
    } while (i != 0);
    printf("OC(x, y) = 123\n"); return 0;
}
