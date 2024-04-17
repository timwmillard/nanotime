#include <stdio.h>
#include <stdint.h>

/* --- Binary Format macro's --- */
#define BINFORMAT_INT8 "%c%c%c%c%c%c%c%c"
#define BIN_INT8(i)    \
    (((i) & 0x80ll) ? '1' : '0'), \
    (((i) & 0x40ll) ? '1' : '0'), \
    (((i) & 0x20ll) ? '1' : '0'), \
    (((i) & 0x10ll) ? '1' : '0'), \
    (((i) & 0x08ll) ? '1' : '0'), \
    (((i) & 0x04ll) ? '1' : '0'), \
    (((i) & 0x02ll) ? '1' : '0'), \
    (((i) & 0x01ll) ? '1' : '0')

#define BINFORMAT_INT16 \
    BINFORMAT_INT8 " " BINFORMAT_INT8
#define BIN_INT16(i) \
    BIN_INT8((i) >> 8),   BIN_INT8(i)
#define BINFORMAT_INT32 \
    BINFORMAT_INT16 " " BINFORMAT_INT16
#define BIN_INT32(i) \
    BIN_INT16((i) >> 16), BIN_INT16(i)
#define BINFORMAT_INT64    \
    BINFORMAT_INT32 " " BINFORMAT_INT32
#define BIN_INT64(i) \
    BIN_INT32((i) >> 32), BIN_INT32(i)
/* --- end macros --- */

int main(void)
{
    int64_t n1 = 5;
    int64_t n2 = 9;
    int64_t num = n1 & ~n2;
    printf("num = 0x%llx\n", num);
    printf("num = %lld\n", num);
    printf("n1  = " BINFORMAT_INT64 "\n", BIN_INT64(n1));
    printf("n1  = " BINFORMAT_INT64 "\n", BIN_INT64(n2));
    printf("num = " BINFORMAT_INT64 "\n", BIN_INT64(num));
}

