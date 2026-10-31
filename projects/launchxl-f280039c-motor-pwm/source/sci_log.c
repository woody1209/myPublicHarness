//#############################################################################
//
// FILE:   sci_log.c
//
// TITLE:  SCI logging over the XDS110 virtual COM port (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "sci_log.h"
#include "board_pins.h"
#include "driverlib.h"
#include "device.h"

void sci_log_init(void)
{
    //
    // GPIO28/29 are the SCIA pins routed to the onboard XDS110 UART bridge.
    //
    GPIO_setPinConfig(BOARD_SCI_RX_PINCFG);
    GPIO_setPadConfig(BOARD_SCI_RX_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(BOARD_SCI_RX_GPIO, GPIO_DIR_MODE_IN);

    GPIO_setPinConfig(BOARD_SCI_TX_PINCFG);
    GPIO_setPadConfig(BOARD_SCI_TX_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(BOARD_SCI_TX_GPIO, GPIO_DIR_MODE_OUT);

    SCI_setConfig(BOARD_SCI_BASE, BOARD_SCI_LSPCLK_HZ, BOARD_SCI_BAUD,
                  (SCI_CONFIG_WLEN_8 | SCI_CONFIG_STOP_ONE | SCI_CONFIG_PAR_NONE));

    SCI_disableLoopback(BOARD_SCI_BASE);
    SCI_performSoftwareReset(BOARD_SCI_BASE);
    SCI_enableFIFO(BOARD_SCI_BASE);
    SCI_enableModule(BOARD_SCI_BASE);
}

void sci_log_puts(const char *s)
{
    if (s == 0)
    {
        return;
    }

    while (*s != '\0')
    {
        SCI_writeCharBlockingFIFO(BOARD_SCI_BASE, (uint16_t)(*s));
        s++;
    }

    //
    // Make sure the last character has actually left the shift register before
    // returning, so the log is not truncated when the caller continues.
    //
    while (SCI_isTransmitterBusy(BOARD_SCI_BASE))
    {
        ;
    }
}

void sci_log_line(const char *s)
{
    sci_log_puts(s);
    sci_log_puts("\r\n");
}
