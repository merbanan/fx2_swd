#include "fx2.h"
#include "jtag.h"

/* PA0 TCK, PA1 TMS, PA2 TDI, PA3 TDO, PA4 nTRST, PA5 nRESET. */
#define TCK 0x01
#define TMS 0x02
#define TDI 0x04
#define TDO 0x08
#define TRST 0x10
#define RESET 0x20

static uint8_t enabled;

void jtag_connect(void)
{
    PORTACFG = 0;
    IOA |= TMS | TDI | TRST | RESET;
    IOA &= (uint8_t)~TCK;
    OEA = TCK | TMS | TDI;
    enabled = 1;
}

void jtag_disconnect(void)
{
    enabled = 0;
    OEA = 0;
}

uint8_t jtag_active(void) { return enabled; }

uint8_t jtag_pins(uint8_t output, uint8_t select)
{
    uint8_t result = 0, pin;
    if (select & 1) { if (output & 1) IOA |= TCK; else IOA &= (uint8_t)~TCK; }
    if (select & 2) { if (output & 2) IOA |= TMS; else IOA &= (uint8_t)~TMS; }
    if (select & 4) { if (output & 4) IOA |= TDI; else IOA &= (uint8_t)~TDI; }
    if (select & 0x20) {
        pin = TRST;
        if (output & 0x20) { OEA &= (uint8_t)~pin; IOA |= pin; }
        else { IOA &= (uint8_t)~pin; OEA |= pin; }
    }
    if (select & 0x80) {
        pin = RESET;
        if (output & 0x80) { OEA &= (uint8_t)~pin; IOA |= pin; }
        else { IOA &= (uint8_t)~pin; OEA |= pin; }
    }
    if (IOA & TCK) result |= 1;
    if (IOA & TMS) result |= 2;
    if (IOA & TDI) result |= 4;
    if (IOA & TDO) result |= 8;
    if (IOA & TRST) result |= 0x20;
    if (IOA & RESET) result |= 0x80;
    return result;
}

uint8_t jtag_step(uint8_t tms, uint8_t tdi)
{
    uint8_t sample;
    if (tms) IOA |= TMS; else IOA &= (uint8_t)~TMS;
    if (tdi) IOA |= TDI; else IOA &= (uint8_t)~TDI;
    __asm nop; nop; __endasm;
    IOA |= TCK;
    __asm nop; nop; __endasm;
    sample = (IOA & TDO) ? 1 : 0;
    IOA &= (uint8_t)~TCK;
    return sample;
}

void jtag_shift(uint8_t bits, uint8_t tms,
                const volatile __xdata uint8_t *tdi,
                volatile __xdata uint8_t *tdo)
{
    uint8_t i;
    if (tdo) for (i = 0; i < (uint8_t)((bits + 7) >> 3); ++i) tdo[i] = 0;
    for (i = 0; i < bits; ++i) {
        if (jtag_step(tms, (tdi[i >> 3] >> (i & 7)) & 1) && tdo)
            tdo[i >> 3] |= 1 << (i & 7);
    }
}

void jtag_reset(void)
{
    uint8_t i;
    IOA |= TMS | TDI;
    for (i = 0; i < 5; ++i) { IOA |= TCK; __asm nop; __endasm; IOA &= (uint8_t)~TCK; }
    IOA &= (uint8_t)~TMS;
    IOA |= TCK;
    __asm nop; __endasm;
    IOA &= (uint8_t)~TCK;
}
