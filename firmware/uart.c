#include "fx2.h"
#include "uart.h"

/* Two-wire UART: PA4 TX, PA5 RX. UPDI mode: PA4 is one-wire open drain. */
#define TX 0x10
#define RX 0x20
#define AUX0 0x40
#define AUX1 0x80

static uint8_t active, uart_mode;
static uint16_t bit_ticks;

static uint16_t timer_now(void)
{
    uint8_t high, low;
    do {
        high = TH0;
        low = TL0;
    } while (high != TH0);
    return ((uint16_t)high << 8) | low;
}

static void wait_until(uint16_t deadline)
{
    while ((int16_t)(timer_now() - deadline) < 0) { }
}

static void drive_tx(uint8_t high)
{
    if (uart_mode == 1) {
        if (high) { IOA |= TX; OEA &= (uint8_t)~TX; }
        else { IOA &= (uint8_t)~TX; OEA |= TX; }
    } else {
        if (high) IOA |= TX;
        else IOA &= (uint8_t)~TX;
    }
}

static uint8_t read_rx(void)
{
    return (IOA & (uart_mode == 1 ? TX : RX)) ? 1 : 0;
}

uint8_t uart_config(uint8_t mode, uint32_t baud)
{
    if (mode > 2 || baud < 1200 || baud > 38400) return 1;
    active = 1;
    uart_mode = mode;
    bit_ticks = (uint16_t)(4000000UL / baud); /* Timer 0 at 48 MHz / 12 */
    CKCON &= (uint8_t)~8;
    TMOD = (TMOD & 0xf0) | 1; /* Timer 0, 16-bit */
    TH0 = 0; TL0 = 0;
    TCON &= (uint8_t)~0x20;
    TCON |= 0x10;
    PORTACFG = 0;
    IOA |= TX;
    OEA &= (uint8_t)~(TX | RX);
    if (uart_mode != 1) OEA |= TX;
    return 0;
}

void uart_cancel(void)
{
    active = 0;
    TCON &= (uint8_t)~0x10;
    IOA |= TX;
    OEA &= (uint8_t)~(TX | RX | AUX0 | AUX1);
}

static void send_byte(uint8_t value)
{
    uint8_t i, parity = 0;
    uint16_t deadline = timer_now();
    drive_tx(0); /* start */
    for (i = 0; i < 8; ++i) {
        deadline += bit_ticks;
        wait_until(deadline);
        drive_tx(value & 1);
        parity ^= value & 1;
        value >>= 1;
    }
    deadline += bit_ticks;
    wait_until(deadline);
    drive_tx(uart_mode ? parity : 1);
    deadline += bit_ticks;
    wait_until(deadline);
    drive_tx(1);
    deadline += bit_ticks;
    wait_until(deadline);
    if (uart_mode == 1) { deadline += bit_ticks; wait_until(deadline); }
}

static uint8_t receive_byte(uint8_t *value, uint8_t timeout_units)
{
    uint8_t i, parity = 0, wraps = 0, data = 0;
    uint16_t deadline;
    TCON &= (uint8_t)~0x20;
    while (read_rx()) {
        if (TCON & 0x20) {
            TCON &= (uint8_t)~0x20;
            if (++wraps >= timeout_units) return 1;
        }
    }
    deadline = timer_now() + bit_ticks / 2;
    wait_until(deadline);
    if (read_rx()) return 2; /* false start */
    for (i = 0; i < 8; ++i) {
        deadline += bit_ticks;
        wait_until(deadline);
        if (read_rx()) { data |= 1 << i; parity ^= 1; }
    }
    if (uart_mode) {
        deadline += bit_ticks;
        wait_until(deadline);
        if (read_rx() != parity) return 3;
    }
    deadline += bit_ticks;
    wait_until(deadline);
    if (!read_rx()) return 4;
    if (uart_mode == 1) { deadline += bit_ticks; wait_until(deadline); if (!read_rx()) return 4; }
    *value = data;
    return 0;
}

uint8_t uart_exchange(const volatile __xdata uint8_t *tx, uint8_t tx_count,
                      volatile __xdata uint8_t *rx, uint8_t rx_count,
                      uint8_t timeout_units, uint8_t *received)
{
    uint8_t i, result;
    *received = 0;
    if (!active) return 5;
    if (!timeout_units) timeout_units = 1;
    for (i = 0; i < tx_count; ++i) send_byte(tx[i]);
    for (i = 0; i < rx_count; ++i) {
        uint8_t value;
        result = receive_byte(&value, timeout_units);
        if (result) return result;
        rx[i] = value;
        ++*received;
    }
    return 0;
}

uint8_t uart_control(uint8_t output, uint8_t mask)
{
    if (mask & 1) {
        if (output & 1) { IOA |= AUX0; OEA |= AUX0; }
        else { IOA &= (uint8_t)~AUX0; OEA |= AUX0; }
    }
    if (mask & 2) {
        if (output & 2) { IOA |= AUX1; OEA &= (uint8_t)~AUX1; } /* open drain reset released */
        else { IOA &= (uint8_t)~AUX1; OEA |= AUX1; }
    }
    return ((IOA & AUX0) ? 1 : 0) | ((IOA & AUX1) ? 2 : 0);
}

uint8_t uart_break(uint8_t duration_units)
{
    uint8_t units = duration_units ? duration_units : 1;
    if (!active) return 5;
    drive_tx(0);
    while (units--) {
        uint16_t start = timer_now();
        while ((uint16_t)(timer_now() - start) < 4000) { } /* ~1 ms */
    }
    drive_tx(1);
    return 0;
}
