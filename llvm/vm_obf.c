#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

typedef struct stack 
{
    uint8_t size; // Enough to address 256 elements
    uint64_t content[256];
} stack;

//inline void vm_push(stack* s, uint64_t value) __attribute__((always_inline));
//inline uint64_t vm_pop(stack* s) __attribute__((always_inline));
/*
inline void vm_add(stack* s) __attribute__((always_inline));
inline void vm_sub(stack* s) __attribute__((always_inline));
inline void vm_mul(stack* s) __attribute__((always_inline));
inline void vm_xor(stack* s) __attribute__((always_inline));
inline void vm_and(stack* s) __attribute__((always_inline));
inline void vm_or(stack* s) __attribute__((always_inline));
*/

void vm_push(stack* s, uint64_t value)
{
    s->content[++(s->size)] = value;
}

uint64_t vm_pop(stack* s)
{
    return s->content[(s->size)--];
}

void vm_add(stack* s)
{
    uint64_t op1, op2, result;
    op2 = vm_pop(s);
    op1 = vm_pop(s);
    result = 3*(op1 & op2) - (op1 | op2) + 2*(op1 ^ op2);
    vm_push(s, result);
}

void vm_sub(stack* s)
{
    uint64_t op1, op2, result;
    op2 = vm_pop(s);
    op1 = vm_pop(s);
    result = -3*(op1 & op2) - 2*(op1 ^ op2) + 2*op1 + (op1 | op2);
    vm_push(s, result);
}

void vm_mul(stack* s)
{
    uint64_t op1, op2, result;
    op2 = vm_pop(s);
    op1 = vm_pop(s);
    result = -op1 + op2 + (op1^op2) + (op1*op2) + 2*(op1 | (~op2)) + 2;
    vm_push(s, result);
}

void vm_xor(stack* s)
{
    uint64_t op1, op2, result;
    op2 = vm_pop(s);
    op1 = vm_pop(s);
    result = 39*(151*(op1 - op2 - 2*(op1 | ~op2) - 2) + 111) + 23;
    vm_push(s, result);
}

void vm_and(stack* s)
{
    uint64_t op1, op2, result;
    op2 = vm_pop(s);
    op1 = vm_pop(s);
    result = -op1 + op2 - 2*(op1 ^ op2) + 2*op1 + (op1 | op2) - 2*(op1 & op2);
    vm_push(s, result);
}

void vm_or(stack* s)
{
    uint64_t op1, op2, result;
    op2 = vm_pop(s);
    op1 = vm_pop(s);
    result = -op1 - op2 + 3*(op1 & op2) + 2*(op1 ^ op2);
    vm_push(s, result);
}

uint64_t interpreter(uint64_t* bytecode)
{
    uint64_t vm_ip, current_ins, result;
    stack s;

    vm_ip = 0;
    s.size = 0;

    while (current_ins = bytecode[vm_ip])
    {
        switch (current_ins)
        {
        case 0xA0:
            vm_push(&s, bytecode[++vm_ip]);
            break;

        case 0xB0:
            vm_pop(&s);
            break;

        case 0x10:
            vm_add(&s);
            break;

        case 0x20:
            vm_sub(&s);
            break;

        case 0x30:
            vm_mul(&s);
            break;

        case 0x40:
            vm_xor(&s);
            break;

        case 0x50:
            vm_and(&s);
            break;

        case 0x60:
            vm_or(&s);
            break;            
        }
        vm_ip++;
    }
    return vm_pop(&s);
}

uint64_t vmSecretComputation(uint64_t x, uint64_t y)
{
    uint64_t result;

    // POSTFIX: 4 x y + x y & x y | ^ - *	
    uint64_t bytecode[] =
    {
        0xA0, // vm_push
        0x04, // 4
        0xA0, // vm_push
        x,    // x
        0xA0, // vm_push
        y,    // y
        0x10, // vm_add
        0xA0, // vm_push
        x,    // x
        0xA0, // vm_push
        y,    // y
        0x50, // vm_and
        0xA0, // vm_push
        x,    // x
        0xA0, // vm_push
        y,    // y
        0x60, // vm_or
        0x40, // vm_xor
        0x20, // vm_sub
        0x30, // vm_mul
    };
    
    result = interpreter(bytecode);
    return result;
}

uint64_t secretComputation(uint64_t x, uint64_t y)
{
    uint64_t result;
    result = 4 * ( (x + y) - ((x & y) ^ (x | y)) );
    return result;
}

int main()
{
    printf("Result %" PRIu64 "\n", vmSecretComputation(1234, 5678));
    return 0;
}
