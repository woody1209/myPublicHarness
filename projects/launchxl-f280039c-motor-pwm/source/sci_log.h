//#############################################################################
//
// FILE:   sci_log.h
//
// TITLE:  SCI logging over the XDS110 virtual COM port (hardware layer).
//
// DESCRIPTION:
//   Polling (blocking) transmit only - no interrupts, no printf, no stdio.
//   Numbers must be rendered by fmt.h before being handed to sci_log_puts().
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef MOTORPWM_SCI_LOG_H
#define MOTORPWM_SCI_LOG_H

//! Configure GPIO28/29 and SCIA at 115200-8N1 (call after board_clock_init()).
void sci_log_init(void);

//! Transmit a NUL-terminated string, blocking until the last character is out.
void sci_log_puts(const char *s);

//! Transmit a line: string followed by CR+LF.
void sci_log_line(const char *s);

#endif // MOTORPWM_SCI_LOG_H
