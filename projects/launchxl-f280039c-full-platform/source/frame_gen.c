//#############################################################################
//
// FILE:   frame_gen.c
//
// TITLE:  Test-frame generation and verification (pure logic).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "frame_gen.h"

#define FRAME_GEN_FNV_OFFSET  2166136261U
#define FRAME_GEN_FNV_PRIME   16777619U

uint16_t frame_gen_lfsr16(uint16_t state)
{
    /* 16-bit maximal LFSR, taps 16/14/13/11 */
    uint16_t bit = (uint16_t)((state ^ (state >> 1U) ^ (state >> 3U) ^
                              (state >> 4U)) & 0x1U);
    return (uint16_t)((state >> 1U) | (uint16_t)(bit << 15U));
}

void frame_gen_fill(uint16_t *buf, uint16_t seed)
{
    uint16_t i;
    uint16_t s = seed;

    if (buf == 0) { return; }
    for (i = 0U; i < FRAME_GEN_WORDS; i++)
    {
        s = frame_gen_lfsr16(s);
        buf[i] = s;
    }
}

bool frame_gen_verify(const uint16_t *buf, uint16_t seed)
{
    uint16_t expect[FRAME_GEN_WORDS];
    uint16_t i;

    if (buf == 0) { return false; }
    frame_gen_fill(expect, seed);
    for (i = 0U; i < FRAME_GEN_WORDS; i++)
    {
        if (buf[i] != expect[i]) { return false; }
    }
    return true;
}

uint32_t frame_gen_fnv1a(const uint16_t *buf)
{
    uint32_t h = FRAME_GEN_FNV_OFFSET;
    uint16_t i;
    uint16_t j;

    if (buf == 0) { return 0U; }
    for (i = 0U; i < FRAME_GEN_WORDS; i++)
    {
        /* C28x char is 16-bit: hash both bytes of the word, high first */
        for (j = 0U; j < 2U; j++)
        {
            uint32_t byte = ((buf[i] >> (8U * (1U - j))) & 0xFFU);
            h ^= byte;
            h *= FRAME_GEN_FNV_PRIME;
        }
    }
    return h;
}
