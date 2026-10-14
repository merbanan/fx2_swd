#include "fx2.h"
#include "usb.h"
#include "swd.h"
#include "dap.h"

void main(void)
{
    uint8_t length, i;
    swd_init();
    usb_init();
    for (;;) {
        usb_poll();
        if (!usb_configured() || (EP1OUTCS & 2) || (EP1INCS & 2)) continue;
        length = EP1OUTBC;
        if (length > 64) length = 64;
        length = dap_process(EP1OUTBUF, EP1INBUF, length);
        for (i = length; i < 64; ++i) EP1INBUF[i] = 0;
        EP1INBC = 64;
        SYNCDELAY();
        EP1OUTBC = 0; /* accept the next command */
        SYNCDELAY();
    }
}
