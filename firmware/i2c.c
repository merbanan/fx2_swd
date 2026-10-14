#include "fx2.h"
#include "i2c.h"

/* PA0 SCL and PA1 SDA are open drain; external pullups are required. */
#define SCL 1
#define SDA 2

static uint8_t active;

static void pause(void)
{
    uint8_t i;
    for (i = 0; i < 12; ++i) __asm nop; __endasm;
}

static void low(uint8_t pin) { IOA &= (uint8_t)~pin; OEA |= pin; pause(); }
static void release(uint8_t pin) { IOA |= pin; OEA &= (uint8_t)~pin; pause(); }

static uint8_t clock_high(void)
{
    uint16_t timeout = 2000;
    release(SCL);
    while (!(IOA & SCL)) if (!--timeout) return 1;
    pause();
    return 0;
}

static uint8_t start(void)
{
    release(SDA);
    if (clock_high()) return 1;
    if (!(IOA & SDA)) return 1;
    low(SDA);
    low(SCL);
    return 0;
}

static void stop(void)
{
    low(SDA);
    clock_high();
    release(SDA);
}

static uint8_t write_byte(uint8_t value)
{
    uint8_t i, ack;
    for (i = 0; i < 8; ++i) {
        if (value & 0x80) release(SDA); else low(SDA);
        if (clock_high()) return 2;
        low(SCL);
        value <<= 1;
    }
    release(SDA);
    if (clock_high()) return 2;
    ack = (IOA & SDA) ? 1 : 0;
    low(SCL);
    return ack;
}

static uint8_t read_byte(uint8_t nack, uint8_t *out)
{
    uint8_t i, value = 0;
    release(SDA);
    for (i = 0; i < 8; ++i) {
        value <<= 1;
        if (clock_high()) return 2;
        if (IOA & SDA) value |= 1;
        low(SCL);
    }
    if (nack) release(SDA); else low(SDA);
    if (clock_high()) return 2;
    low(SCL);
    release(SDA);
    *out = value;
    return 0;
}

uint8_t i2c_open(void)
{
    PORTACFG = 0;
    release(SCL | SDA);
    active = (IOA & (SCL | SDA)) == (SCL | SDA);
    return active ? 0 : 1;
}

void i2c_close(void)
{
    release(SCL | SDA);
    active = 0;
}

uint8_t i2c_transaction(uint8_t address,
                        const volatile __xdata uint8_t *tx, uint8_t tx_count,
                        volatile __xdata uint8_t *rx, uint8_t rx_count)
{
    uint8_t i, status = 0;
    if (!active || address > 0x7f) return 3;
    if (start()) return 2;
    if (tx_count || !rx_count) {
        status = write_byte(address << 1);
        if (status) goto done;
        for (i = 0; i < tx_count; ++i) {
            status = write_byte(tx[i]);
            if (status) goto done;
        }
    }
    if (rx_count) {
        if (start()) { status = 2; goto done; }
        status = write_byte((address << 1) | 1);
        if (status) goto done;
        for (i = 0; i < rx_count; ++i) {
            uint8_t value;
            status = read_byte(i == rx_count - 1, &value);
            if (status) goto done;
            rx[i] = value;
        }
    }
done:
    stop();
    return status;
}
