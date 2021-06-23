#include "stdio.h"
#include "stdlib.h"

int add(int a, int b)
{
    printf("[%s]: %d + %d = %d\n", __FILE__, a, b, a + b);
    return a + b;
}

int sub(int a, int b)
{
    printf("[%s]: %d - %d = %d\n", __FILE__, a, b, a - b);
    return a - b;
}
