#include "fx2.h"
#include "isp.h"

/* AVR ISP: PA0=SCK, PA1=MOSI, PA2=MISO, PA3=/RESET. SPI mode 0, MSB first. */
#define SCK  0x01
#define MOSI 0x02
#define MISO 0x04
#define RESET 0x08

static uint8_t active;

static uint8_t shift_byte(uint8_t value)
{
    uint8_t i, received = 0;
    for (i = 0; i < 8; ++i) {
        if (value & 0x80) IOA |= MOSI;
        else IOA &= (uint8_t)~MOSI;
        __asm nop; nop; nop; __endasm;
        IOA |= SCK;
        __asm nop; nop; nop; __endasm;
        received = (received << 1) | ((IOA & MISO) ? 1 : 0);
        IOA &= (uint8_t)~SCK;
        __asm nop; nop; nop; __endasm;
        value <<= 1;
    }
    return received;
}

void isp_transaction(const volatile __xdata uint8_t *command,
                     volatile __xdata uint8_t *response)
{
    uint8_t i;
    for (i = 0; i < 4; ++i) response[i] = shift_byte(command[i]);
}

uint8_t isp_enter(void)
{
    volatile uint16_t wait;
    uint8_t echo;
    active = 0;
    PORTACFG = 0;
    IOA = RESET; /* reset released and SCK low */
    OEA = SCK | MOSI;
    for (wait = 0; wait < 1000; ++wait) __asm nop; __endasm;
    IOA &= (uint8_t)~RESET;
    OEA |= RESET;
    for (wait = 0; wait < 60000; ++wait) __asm nop; __endasm;
    shift_byte(0xac);
    shift_byte(0x53);
    echo = shift_byte(0);
    shift_byte(0);
    if (echo != 0x53) { isp_leave(); return 1; }
    active = 1;
    return 0;
}

void isp_leave(void)
{
    active = 0;
    IOA |= RESET;
    OEA = 0;
}

uint8_t isp_active(void) { return active; }
