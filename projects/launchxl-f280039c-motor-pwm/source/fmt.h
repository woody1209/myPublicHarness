//#############################################################################
//
// FILE:   fmt.h
//
// TITLE:  Bounded number formatting (pure logic, no stdio).
//
// DESCRIPTION:
//   Firmware logs must never sprintf() into a fixed-size buffer (AGENTS.md §4).
//   These helpers convert uint32_t values into ASCII with an explicit output
//   size, always NUL-terminate when there is room, and report truncation with a
//   negative return code. Pure logic: host-testable, no hardware, no libc.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef MOTORPWM_FMT_H
#define MOTORPWM_FMT_H

#include <stdint.h>

//! Buffer too small for the value and the terminating NUL.
#define FMT_ERR_TOO_SMALL   (-1)

//! Render value as decimal ASCII.
//! \param dst      destination buffer
//! \param dst_size capacity of dst in bytes (including space for the NUL)
//! \return number of characters written (excluding NUL), or FMT_ERR_TOO_SMALL
int32_t fmt_u32_dec(uint32_t value, char *dst, uint32_t dst_size);

//! Render value as hexadecimal ASCII with a "0x" prefix and no leading zeros
//! (0 -> "0x0", 0x1A2B -> "0x1A2B", digits in upper case).
int32_t fmt_u32_hex(uint32_t value, char *dst, uint32_t dst_size);

#endif // MOTORPWM_FMT_H
