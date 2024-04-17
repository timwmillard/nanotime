#include <stdio.h>
#include <stdlib.h>

static inline void nt_panic(char *v)
{
    printf("%s", v);
    exit(1);
}

