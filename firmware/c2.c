#include "fx2.h"
#include "c2.h"

/* PA0=C2CK (/RST), PA1=C2D. C2CK must never remain low for 5 us in a frame. */
#define C2CK 1
#define C2D 2
#define FPDAT 0xb4

static uint8_t ready;

static void strobe(void)
{
    IOA &= (uint8_t)~C2CK;
    __asm nop; nop; __endasm;
    IOA |= C2CK;
    __asm nop; nop; __endasm;
}

static void release_data(void) { OEA &= (uint8_t)~C2D; }
static void drive_data(void) { IOA |= C2D; OEA |= C2D; }

static void send_bit(uint8_t bit)
{
    if (bit) IOA |= C2D;
    else IOA &= (uint8_t)~C2D;
    strobe();
}

static uint8_t receive_bit(void)
{
    strobe();
    return (IOA & C2D) ? 1 : 0;
}

static void frame_start(uint8_t instruction)
{
    release_data();
    strobe();
    drive_data();
    send_bit(instruction & 1);
    send_bit((instruction >> 1) & 1);
}

static void frame_stop(void)
{
    release_data();
    strobe();
}

static void address_write(uint8_t address)
{
    uint8_t i;
    frame_start(3);
    for (i = 0; i < 8; ++i) send_bit((address >> i) & 1);
    frame_stop();
}

static uint8_t address_read(void)
{
    uint8_t i, value = 0;
    frame_start(2); /* wire order 0,1 */
    release_data();
    for (i = 0; i < 8; ++i) value |= receive_bit() << i;
    frame_stop();
    return value;
}

static uint8_t data_wait(void)
{
    uint16_t retry;
    for (retry = 0; retry < 10000; ++retry) {
        if (receive_bit()) return C2_OK;
    }
    return C2_FRAME_TIMEOUT;
}

static uint8_t data_write(uint8_t value)
{
    uint8_t i, result;
    frame_start(1); /* wire order 1,0 */
    send_bit(0); send_bit(0); /* one byte */
    for (i = 0; i < 8; ++i) send_bit((value >> i) & 1);
    release_data();
    result = data_wait();
    frame_stop();
    return result;
}

static uint8_t data_read(uint8_t *value)
{
    uint8_t i, result, data = 0;
    frame_start(0);
    send_bit(0); send_bit(0);
    release_data();
    result = data_wait();
    if (result == C2_OK) {
        for (i = 0; i < 8; ++i) data |= receive_bit() << i;
        *value = data;
    }
    frame_stop();
    return result;
}

static uint8_t poll_status(uint8_t mask, uint8_t expected)
{
    uint16_t retry;
    for (retry = 0; retry < 20000; ++retry) {
        if ((address_read() & mask) == expected) return C2_OK;
    }
    return C2_PI_TIMEOUT;
}

static uint8_t pi_write(uint8_t value)
{
    uint8_t result = data_write(value);
    if (result) return result;
    return poll_status(2, 0);
}

static uint8_t pi_read(uint8_t *value)
{
    uint8_t result = poll_status(1, 1);
    if (result) return result;
    return data_read(value);
}

static uint8_t pi_command(uint8_t command)
{
    uint8_t result, status;
    address_write(FPDAT);
    result = pi_write(command);
    if (result) return result;
    result = pi_read(&status);
    if (result) return result;
    return status == 0x0d ? C2_OK : C2_PI_ERROR;
}

static void reset_pulse(void)
{
    volatile uint16_t n;
    IOA |= C2CK | C2D;
    OEA = C2CK;
    IOA &= (uint8_t)~C2CK;
    for (n = 0; n < 200; ++n) __asm nop; __endasm;
    IOA |= C2CK;
    for (n = 0; n < 50; ++n) __asm nop; __endasm;
}

uint8_t c2_init(uint8_t *revision)
{
    uint8_t id, result;
    volatile uint16_t n;
    ready = 0;
    PORTACFG = 0;
    reset_pulse();
    address_write(0);
    result = data_read(&id);
    if (result) return result;
    if (id != 0x04) return C2_WRONG_DEVICE;
    address_write(1);
    result = data_read(revision);
    if (result) return result;
    address_write(2);
    result = data_write(2); if (result) return result;
    result = data_write(4); if (result) return result;
    result = data_write(1); if (result) return result;
    /* AN127 requires >=20 ms after entering PI. At 48 MHz this exceeds it. */
    for (n = 0; n < 60000; ++n) __asm nop; __endasm;
    address_write(0xb2); /* F30x VDD monitor setup, AN127 table 3.6 */
    result = data_write(7);
    if (result) return result;
    ready = 1;
    return C2_OK;
}

uint8_t c2_erase_page(uint8_t page)
{
    uint8_t result, status;
    if (!ready) return C2_NOT_READY;
    if (page >= 15) return C2_BAD_ARGUMENT; /* page 15 is factory reserved */
    result = pi_command(8); if (result) return result;
    result = pi_write(page); if (result) return result;
    result = poll_status(1, 0); if (result) return result;
    result = data_read(&status); if (result) return result;
    if (status != 0x0d) return C2_PI_ERROR;
    result = pi_write(0); if (result) return result;
    result = pi_read(&status); if (result) return result;
    return status == 0x0d ? C2_OK : C2_PI_ERROR;
}

uint8_t c2_read_block(uint16_t address, uint8_t count, volatile __xdata uint8_t *out)
{
    uint8_t result, i;
    if (!ready) return C2_NOT_READY;
    if (!count || count > 60 || (uint32_t)address + count > 0x1e00) return C2_BAD_ARGUMENT;
    result = pi_command(6); if (result) return result;
    result = pi_write((uint8_t)(address >> 8)); if (result) return result;
    result = pi_write((uint8_t)address); if (result) return result;
    result = pi_write(count); if (result) return result;
    for (i = 0; i < count; ++i) {
        uint8_t value;
        result = pi_read(&value); if (result) return result;
        out[i] = value;
    }
    return C2_OK;
}

uint8_t c2_write_block(uint16_t address, uint8_t count, const volatile __xdata uint8_t *in)
{
    uint8_t result, i, status;
    if (!ready) return C2_NOT_READY;
    if (!count || count > 59 || (uint32_t)address + count > 0x1e00) return C2_BAD_ARGUMENT;
    result = pi_command(7); if (result) return result;
    result = pi_write((uint8_t)(address >> 8)); if (result) return result;
    result = pi_write((uint8_t)address); if (result) return result;
    result = pi_write(count); if (result) return result;
    for (i = 0; i < count; ++i) {
        result = pi_write(in[i]); if (result) return result;
    }
    result = pi_read(&status); if (result) return result;
    return status == 0x0d ? C2_OK : C2_PI_ERROR;
}

void c2_reset_target(void)
{
    ready = 0;
    reset_pulse();
    OEA = 0;
}

void c2_cancel(void)
{
    ready = 0;
    OEA = 0;
}
