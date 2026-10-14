#include "fx2.h"
#include "swd.h"

#define CLK 0x01
#define DIO 0x02
#define RST 0x04

static uint16_t half_delay = 60;
static uint8_t turnaround = 1;
static uint8_t data_phase = 0;

static void delay_half(void)
{
    volatile uint16_t n = half_delay;
    while (n--) { __asm nop; __endasm; }
}

static void clock_bit(void)
{
    delay_half();
    IOA |= CLK;
    delay_half();
    IOA &= (uint8_t)~CLK;
}

static void put_bit(uint8_t bit)
{
    if (bit) IOA |= DIO;
    else IOA &= (uint8_t)~DIO;
    clock_bit();
}

static uint8_t get_bit(void)
{
    uint8_t bit;
    delay_half();
    IOA |= CLK;
    delay_half();
    bit = (IOA & DIO) ? 1 : 0;
    IOA &= (uint8_t)~CLK;
    return bit;
}

static void release_dio(void) { OEA &= (uint8_t)~DIO; }
static void drive_dio(void) { IOA |= DIO; OEA |= DIO; }

static void turn_clocks(void)
{
    uint8_t n;
    for (n = 0; n < turnaround; ++n) clock_bit();
}

void swd_init(void)
{
    PORTACFG = 0;
    IOA = DIO | RST;
    OEA = CLK;
    swd_set_clock(100000);
}

void swd_connect(void)
{
    IOA = DIO | RST;
    OEA = CLK | DIO;
}

void swd_disconnect(void)
{
    IOA &= (uint8_t)~CLK;
    OEA = 0;
}

void swd_set_clock(uint32_t hz)
{
    uint32_t ticks;
    /* Conservative software limit. Each loop takes several CPU instructions. */
    if (hz > 100000U) hz = 100000U;
    if (hz < 1000U) hz = 1000U;
    ticks = 6000000UL / hz;
    half_delay = (uint16_t)ticks;
    if (!half_delay) half_delay = 1;
}

void swd_set_config(uint8_t cfg)
{
    turnaround = (cfg & 3) + 1;
    data_phase = cfg & 4;
}

void swd_sequence(uint8_t bits, const volatile __xdata uint8_t *data)
{
    uint8_t i;
    drive_dio();
    for (i = 0; i < bits; ++i) put_bit((data[i >> 3] >> (i & 7)) & 1);
}

uint8_t swd_pins(uint8_t output, uint8_t select)
{
    uint8_t pins;
    if (select & 1) {
        if (output & 1) IOA |= CLK;
        else IOA &= (uint8_t)~CLK;
    }
    if (select & 2) {
        if (output & 2) IOA |= DIO;
        else IOA &= (uint8_t)~DIO;
        OEA |= DIO;
    }
    if (select & 0x80) {
        if (output & 0x80) { IOA |= RST; OEA &= (uint8_t)~RST; }
        else { IOA &= (uint8_t)~RST; OEA |= RST; }
    }
    pins = IOA & (CLK | DIO);
    if (IOA & RST) pins |= 0x80;
    return pins;
}

void swd_idle(uint8_t clocks)
{
    drive_dio();
    while (clocks--) clock_bit();
}

uint8_t swd_transfer(uint8_t request, uint32_t *data)
{
    uint8_t i, ack = 0, parity = 0, received_parity;
    uint32_t value = 0;
    drive_dio();
    put_bit(1);
    for (i = 0; i < 4; ++i) {
        uint8_t bit = (request >> i) & 1;
        put_bit(bit);
        parity ^= bit;
    }
    put_bit(parity);
    put_bit(0);
    put_bit(1);
    release_dio();
    turn_clocks();
    for (i = 0; i < 3; ++i) ack |= get_bit() << i;

    if (ack == 1 || (data_phase && (ack == 2 || ack == 4))) {
        if (request & 2) {
            parity = 0;
            for (i = 0; i < 32; ++i) {
                uint8_t bit = get_bit();
                if (bit) value |= (1UL << i);
                parity ^= bit;
            }
            received_parity = get_bit();
            turn_clocks();
            drive_dio();
            if (ack == 1) {
                *data = value;
                if (received_parity != parity) return 0x08 | ack;
            }
        } else {
            turn_clocks();
            drive_dio();
            parity = 0;
            for (i = 0; i < 32; ++i) {
                uint8_t bit = (uint8_t)(*data >> i) & 1;
                put_bit(bit);
                parity ^= bit;
            }
            put_bit(parity);
        }
    } else {
        turn_clocks();
        drive_dio();
    }
    return ack;
}
