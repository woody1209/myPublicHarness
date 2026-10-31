//#############################################################################
//
// FILE:   frame_gen.h
//
// TITLE:  Test-frame generation and verification (pure logic, spec 0002).
//
// DESCRIPTION:
//   Deterministic test payloads shared by every communication subsystem
//   (SPI/I2C/SCIB/LIN/CAN/FSI/DMA): a seeded 16-bit LFSR fill plus a
//   reference FNV-1a constant over the standard 8-word frame. Host tests
//   and the on-target self test consume the same code and constants
//   (no two truths, REQ-020).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_FRAME_GEN_H
#define FULLPLATFORM_FRAME_GEN_H

#include <stdint.h>
#include <stdbool.h>

//! Standard test-frame length in 16-bit words.
#define FRAME_GEN_WORDS              8U

//! Reference FNV-1a over one FRAME_GEN_WORDS frame of seed FRAME_GEN_SEED_1
//! (computed 2026-10-24; frame = 899B 44CD 2266 9133 C899 E44C F226 F913).
#define FRAME_GEN_REF_FNV1A          0x1F2385B5U

//! Canonical seeds.
#define FRAME_GEN_SEED_1             0x1337U
#define FRAME_GEN_SEED_2             0xC0DEU

//! Advance the 16-bit LFSR one step. Exposed for tests.
uint16_t frame_gen_lfsr16(uint16_t state);

//! Fill buf[0..FRAME_GEN_WORDS-1] from the seed. buf may be NULL (no-op).
void frame_gen_fill(uint16_t *buf, uint16_t seed);

//! \return true when buf matches the pattern of seed (8 words).
bool frame_gen_verify(const uint16_t *buf, uint16_t seed);

//! FNV-1a over 8 words (as used for the reference constant). Exposed for
//! tests and for cross-checking the constant.
uint32_t frame_gen_fnv1a(const uint16_t *buf);

#endif // FULLPLATFORM_FRAME_GEN_H
