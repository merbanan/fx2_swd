#include "fx2.h"
#include "spi.h"

/* PA0 SCK, PA1 MOSI, PA2 MISO, PA3 active-low chip select. */
#define SCK 1
#define MOSI 2
#define MISO 4
#define CS 8

static uint8_t active, spi_mode;

uint8_t spi_open(uint8_t mode)
{
    if (mode > 3) { spi_close(); return 1; }
    spi_mode = mode;
    PORTACFG = 0;
    IOA |= MOSI | CS;
    if (mode & 2) IOA |= SCK; else IOA &= (uint8_t)~SCK;
    OEA = SCK | MOSI | CS;
    IOA &= (uint8_t)~CS;
    active = 1;
    return 0;
}

void spi_close(void)
{
    IOA |= CS;
    OEA = 0;
    active = 0;
}

uint8_t spi_active(void) { return active; }

void spi_exchange(const volatile __xdata uint8_t *tx,
                  volatile __xdata uint8_t *rx, uint8_t count)
{
    uint8_t i, bit, input, output;
    while (count--) {
        output = *tx++;
        input = 0;
        for (i = 0; i < 8; ++i) {
            bit = output & 0x80;
            output <<= 1;
            if (!(spi_mode & 1)) {
                if (bit) IOA |= MOSI; else IOA &= (uint8_t)~MOSI;
            }
            __asm nop; nop; __endasm;
            IOA ^= SCK;
            if (spi_mode & 1) {
                if (bit) IOA |= MOSI; else IOA &= (uint8_t)~MOSI;
            } else {
                input <<= 1;
                if (IOA & MISO) input |= 1;
            }
            __asm nop; nop; __endasm;
            IOA ^= SCK;
            if (spi_mode & 1) {
                input <<= 1;
                if (IOA & MISO) input |= 1;
            }
        }
        *rx++ = input;
    }
}
