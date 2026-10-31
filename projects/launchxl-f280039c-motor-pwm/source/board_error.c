//#############################################################################
//
// FILE:   board_error.c
//
// TITLE:  driverlib assertion hook (__error__).
//
// DESCRIPTION:
//   With --define=DEBUG, every driverlib ASSERT() that fails calls __error__().
//   driverlib deliberately leaves this hook to the application; providing it
//   keeps assertions meaningful instead of producing an unresolved-symbol link
//   error. The implementation stops the CPU so the failure is visible in the
//   debugger and cannot be silently walked past.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include <stdint.h>
#include "driverlib.h"
#include "device.h"

//! Line number of the failing assertion, kept for the debugger.
volatile uint32_t g_board_error_line = 0U;

void __error__(const char *filename, uint32_t line)
{
    (void)filename;

    g_board_error_line = line;

    //
    // ESTOP0 halts the CPU when a debugger is attached; the loop keeps the
    // device safely parked when it is not.
    //
    asm(" ESTOP0");

    while (1)
    {
        ;
    }
}
