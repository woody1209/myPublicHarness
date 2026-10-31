//#############################################################################
//
// FILE:   fmt.c
//
// TITLE:  Bounded number formatting (pure logic, no stdio).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "fmt.h"

int32_t fmt_u32_dec(uint32_t value, char *dst, uint32_t dst_size)
{
    char tmp[10];
    uint32_t n = 0U;
    uint32_t i;
    uint32_t need;

    if ((dst == 0) || (dst_size == 0U))
    {
        return FMT_ERR_TOO_SMALL;
    }

    do
    {
        tmp[n] = (char)('0' + (char)(value % 10U));
        value /= 10U;
        n++;
    } while ((value != 0U) && (n < sizeof(tmp)));

    need = n + 1U;                     // digits + NUL
    if (need > dst_size)
    {
        dst[0] = '\0';
        return FMT_ERR_TOO_SMALL;
    }

    for (i = 0U; i < n; i++)           // reverse into dst
    {
        dst[i] = tmp[n - 1U - i];
    }
    dst[n] = '\0';

    return (int32_t)n;
}

int32_t fmt_u32_hex(uint32_t value, char *dst, uint32_t dst_size)
{
    static const char digits[] = "0123456789ABCDEF";
    char tmp[8];
    uint32_t n = 0U;
    uint32_t i;
    uint32_t need;

    if ((dst == 0) || (dst_size == 0U))
    {
        return FMT_ERR_TOO_SMALL;
    }

    do
    {
        tmp[n] = digits[value & 0xFU];
        value >>= 4;
        n++;
    } while ((value != 0U) && (n < sizeof(tmp)));

    need = n + 3U;                     // "0x" + digits + NUL
    if (need > dst_size)
    {
        dst[0] = '\0';
        return FMT_ERR_TOO_SMALL;
    }

    dst[0] = '0';
    dst[1] = 'x';
    for (i = 0U; i < n; i++)
    {
        dst[2U + i] = tmp[n - 1U - i];
    }
    dst[2U + n] = '\0';

    return (int32_t)(n + 2U);
}
