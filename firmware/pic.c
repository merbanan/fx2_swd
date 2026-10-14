#include "fx2.h"
#include "pic.h"

/* PIC16F18313 LVP: PA0 ICSPCLK, PA1 ICSPDAT, PA2 MCLR (open drain). */
#define PGC 0x01
#define PGD 0x02
#define MCLR 0x04

static uint8_t enabled;

static void pause(void)
{
    __asm nop; nop; nop; nop; __endasm;
}

static void clock_bit(uint8_t bit)
{
    if (bit) IOA |= PGD; else IOA &= (uint8_t)~PGD;
    pause();
    IOA |= PGC;
    pause();
    IOA &= (uint8_t)~PGC;
}

static uint8_t read_bit(void)
{
    uint8_t bit;
    pause();
    IOA |= PGC;
    pause();
    bit = (IOA & PGD) ? 1 : 0;
    IOA &= (uint8_t)~PGC;
    return bit;
}

void pic_enter(void)
{
    uint8_t i;
    uint32_t key = 0x4d434850UL;
    PORTACFG = 0;
    IOA &= (uint8_t)~(PGC | PGD | MCLR);
    OEA = PGC | PGD | MCLR;
    for (i = 0; i < 32; ++i) {
        clock_bit((uint8_t)(key & 1));
        key >>= 1;
    }
    clock_bit(0); /* 33rd clock enables key match */
    IOA &= (uint8_t)~PGD;
    enabled = 1;
}

void pic_leave(void)
{
    enabled = 0;
    OEA = 0;
    IOA |= MCLR;
}

uint8_t pic_active(void) { return enabled; }

uint16_t pic_command(uint8_t command, uint8_t mode, uint32_t value)
{
    uint8_t i, count;
    uint32_t result = 0;
    OEA |= PGD;
    for (i = 0; i < 6; ++i) {
        clock_bit(command & 1);
        command >>= 1;
    }
    if (!mode) return 0;
    /* Serial command/data delay exceeds the specified 1 us minimum. */
    for (i = 0; i < 20; ++i) pause();
    count = mode == 2 ? 24 : 16;
    if (mode == 3) {
        OEA &= (uint8_t)~PGD;
        for (i = 0; i < 16; ++i) result |= (uint32_t)read_bit() << i;
        IOA &= (uint8_t)~PGD;
        OEA |= PGD;
        return (uint16_t)(result >> 1) & 0x3fff;
    }
    value <<= 1; /* start=0, payload LSB first, stop=0 */
    for (i = 0; i < count; ++i) {
        clock_bit((uint8_t)(value & 1));
        value >>= 1;
    }
    IOA &= (uint8_t)~PGD;
    return 0;
}
