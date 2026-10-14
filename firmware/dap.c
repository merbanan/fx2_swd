#include "dap.h"
#include "swd.h"
#include "c2.h"
#include "isp.h"
#include "uart.h"
#include "jtag.h"
#include "pic.h"
#include "spi.h"
#include "i2c.h"

#define DAP_OK 0
#define DAP_ERROR 0xff
#define ACK_OK 1
#define ACK_WAIT 2
#define ACK_PROTOCOL 8

static uint8_t connected;
static uint8_t idle_cycles;
static uint16_t wait_retry = 100;
static uint16_t match_retry;
static uint32_t match_mask = 0xffffffffUL;

static uint16_t get16(const volatile __xdata uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get32(const volatile __xdata uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(volatile __xdata uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint8_t transfer_retry(uint8_t request, uint32_t *data)
{
    uint16_t retry = wait_retry;
    uint8_t ack;
    do {
        ack = swd_transfer(request, data);
    } while (ack == ACK_WAIT && retry--);
    if (ack == ACK_OK && idle_cycles) swd_idle(idle_cycles);
    return ack;
}

static uint8_t read_register(uint8_t request, uint32_t *data)
{
    uint8_t ack = transfer_retry(request, data);
    /* AP reads are posted. DP RDBUFF returns the requested AP value. */
    if (ack == ACK_OK && (request & 1)) ack = transfer_retry(0x0e, data);
    return ack;
}

static uint8_t write_register(uint8_t request, uint32_t *data)
{
    uint8_t ack = transfer_retry(request, data);
    /* Drain an AP write so a deferred fault is observed in this command. */
    if (ack == ACK_OK && (request & 1)) {
        uint32_t dummy = 0;
        ack = transfer_retry(0x0e, &dummy);
    }
    return ack;
}

static uint8_t info(const volatile __xdata uint8_t *q,
                    volatile __xdata uint8_t *r)
{
    static const char __code vendor[] = "FX2LP project";
    static const char __code product[] = "FX2LP CMSIS-DAP";
    static const char __code version[] = "1.0.0";
    static const char __code firmware[] = "0.1";
    const char __code *str = 0;
    uint8_t n = 0;
    switch (q[1]) {
    case 1: str = vendor; break;
    case 2: str = product; break;
    case 4: str = version; break;
    case 9: str = firmware; break;
    case 0xf0: r[1] = 1; r[2] = 1; return 3; /* SWD only */
    case 0xfe: r[1] = 1; r[2] = 1; return 3; /* one packet in flight */
    case 0xff: r[1] = 2; r[2] = 64; r[3] = 0; return 4;
    default: r[1] = 0; return 2;
    }
    while (str[n] && n < 60) { r[2 + n] = str[n]; ++n; }
    r[2 + n] = 0;
    r[1] = n + 1;
    return n + 3;
}

static uint8_t transfers(const volatile __xdata uint8_t *q,
                         volatile __xdata uint8_t *r, uint8_t len)
{
    uint8_t count = q[2], index = 3, out = 3, done = 0, ack = ACK_OK;
    uint8_t request;
    uint32_t data, expected;
    uint16_t retry;
    r[1] = 0;
    r[2] = ACK_OK;
    while (done < count) {
        if (index >= len) { ack = ACK_PROTOCOL; break; }
        request = q[index++];
        if (request & 0x80) { ack = ACK_PROTOCOL; break; } /* no timer capability */
        if (!(request & 2) || (request & 0x10)) {
            if ((uint16_t)index + 4 > len) { ack = ACK_PROTOCOL; break; }
            data = get32(q + index);
            index += 4;
        } else data = 0;

        if (!(request & 2) && (request & 0x20)) {
            match_mask = data;
            ++done;
            continue;
        }
        if (request & 2) {
            if (out > 60) { ack = ACK_PROTOCOL; break; }
            if (request & 0x10) {
                expected = data;
                retry = match_retry;
                do {
                    ack = read_register(request & 0x0f, &data);
                    if (ack != ACK_OK || (data & match_mask) == expected) break;
                } while (retry--);
                if (ack == ACK_OK && (data & match_mask) != expected) ack = 0x10 | ACK_OK;
                if (ack != ACK_OK) break;
            } else {
                ack = read_register(request & 0x0f, &data);
                if (ack != ACK_OK) break;
                put32(r + out, data);
                out += 4;
            }
        } else {
            ack = write_register(request & 0x0f, &data);
            if (ack != ACK_OK) break;
        }
        ++done;
    }
    r[1] = done;
    r[2] = ack;
    return out;
}

static uint8_t transfer_block(const volatile __xdata uint8_t *q,
                              volatile __xdata uint8_t *r, uint8_t len)
{
    uint16_t count = get16(q + 2), done = 0;
    uint8_t request = q[4], ack = ACK_OK, out = 4;
    uint32_t data;
    r[1] = 0; r[2] = 0; r[3] = ACK_OK;
    if (count == 0 || count > 14 || (request & 0xf0) ||
        (!(request & 2) && (uint16_t)(5 + count * 4) > len)) {
        r[3] = ACK_PROTOCOL;
        return out;
    }
    while (done < count) {
        if (request & 2) {
            data = 0;
            ack = read_register(request, &data);
            if (ack != ACK_OK) break;
            put32(r + out, data);
            out += 4;
        } else {
            data = get32(q + 5 + done * 4);
            ack = write_register(request, &data);
            if (ack != ACK_OK) break;
        }
        ++done;
    }
    r[1] = (uint8_t)done;
    r[2] = (uint8_t)(done >> 8);
    r[3] = ack;
    return out;
}

uint8_t dap_process(const volatile __xdata uint8_t *q,
                    volatile __xdata uint8_t *r, uint8_t len)
{
    uint8_t command, n, count, i, pins, index, out, bits, info_byte;
    uint16_t delay_count;
    uint32_t value;
    if (!len) { r[0] = 0xff; return 1; }
    command = q[0];
    r[0] = command;
    switch (command) {
    case 0x00: if (len < 2) break; return info(q, r);
    case 0x01: /* HostStatus: LED is optional. */
        if (len < 3) break;
        r[1] = DAP_OK; return 2;
    case 0x02:
        if (len < 2) break;
        if (q[1] == 0 || q[1] == 1) {
            c2_cancel(); isp_leave(); uart_cancel(); jtag_disconnect(); pic_leave(); spi_close(); i2c_close();
            swd_connect(); connected = 1; r[1] = 1;
        } else if (q[1] == 2) {
            swd_disconnect(); c2_cancel(); isp_leave(); uart_cancel(); pic_leave(); spi_close(); i2c_close();
            jtag_connect(); connected = 0; r[1] = 2;
        } else r[1] = 0;
        return 2;
    case 0x03:
        swd_disconnect(); jtag_disconnect(); connected = 0; r[1] = DAP_OK; return 2;
    case 0x04:
        if (len < 6) break;
        idle_cycles = q[1]; wait_retry = get16(q + 2);
        match_retry = get16(q + 4);
        r[1] = DAP_OK; return 2;
    case 0x05:
        if (len < 3) break;
        if (!connected) { r[1] = 0; r[2] = ACK_PROTOCOL; return 3; }
        return transfers(q, r, len);
    case 0x06:
        if (len < 5) break;
        if (!connected) { r[1] = 0; r[2] = 0; r[3] = ACK_PROTOCOL; return 4; }
        return transfer_block(q, r, len);
    case 0x07: r[1] = DAP_OK; return 2;
    case 0x08:
        if (len < 6) break;
        value = get32(q + 2);
        r[1] = (connected && write_register(0, &value) == ACK_OK) ? DAP_OK : DAP_ERROR;
        return 2;
    case 0x09: /* Delay, microseconds; bit-bang operation is already slow. */
        if (len < 3) break;
        delay_count = get16(q + 1);
        while (delay_count--) { for (i = 0; i < 12; ++i) __asm nop; __endasm; }
        r[1] = DAP_OK; return 2;
    case 0x0a: /* ResetTarget: no target-specific reset sequence. */
        r[1] = DAP_OK; r[2] = 0; return 3;
    case 0x10:
        if (len < 7) break;
        pins = jtag_active() ? jtag_pins(q[1], q[2]) : swd_pins(q[1], q[2]);
        r[1] = pins; return 2;
    case 0x11:
        if (len < 5) break;
        value = get32(q + 1);
        if (value == 0) r[1] = DAP_ERROR;
        else { swd_set_clock(value); r[1] = DAP_OK; }
        return 2;
    case 0x12:
        if (len < 2) break;
        count = q[1];
        n = count ? (uint8_t)((count + 7U) / 8U) : 32;
        if ((uint8_t)(n + 2) > len) break;
        if (count) swd_sequence(count, q + 2);
        else { swd_sequence(128, q + 2); swd_sequence(128, q + 18); }
        r[1] = DAP_OK; return 2;
    case 0x13:
        if (len < 2) break;
        swd_set_config(q[1]); r[1] = DAP_OK; return 2;
    case 0x14: /* CMSIS-DAP JTAG_Sequence: raw TAP clocking. */
        if (len < 2) break;
        r[1] = DAP_ERROR;
        if (!jtag_active()) return 2;
        index = 2; out = 2;
        for (i = 0; i < q[1]; ++i) {
            if (index >= len) return 2;
            info_byte = q[index++];
            bits = info_byte & 0x3f;
            if (!bits) bits = 64;
            n = (bits + 7) >> 3;
            if ((uint16_t)index + n > len ||
                (info_byte & 0x80 && (uint16_t)out + n > 64)) return 2;
            jtag_shift(bits, info_byte & 0x40, q + index,
                       (info_byte & 0x80) ? r + out : 0);
            index += n;
            if (info_byte & 0x80) out += n;
        }
        r[1] = DAP_OK; return out;
    case 0x15: /* One-device chain for basic IDCODE access. */
        r[1] = (jtag_active() && len >= 3 && q[1] == 1 && q[2]) ? DAP_OK : DAP_ERROR;
        return 2;
    case 0x16:
        r[1] = DAP_ERROR;
        if (!jtag_active() || len < 2 || q[1]) return 2;
        jtag_reset();
        jtag_step(1, 0); /* Select-DR-Scan */
        jtag_step(0, 0); /* Capture-DR */
        jtag_step(0, 0); /* Shift-DR */
        value = 0;
        for (i = 0; i < 32; ++i) if (jtag_step(i == 31, 0)) value |= 1UL << i;
        jtag_step(1, 0); /* Update-DR */
        jtag_step(0, 0); /* Run-Test/Idle */
        r[1] = DAP_OK; put32(r + 2, value); return 6;
    case 0x80: /* Vendor extension: initialize C8051F300 C2 programming. */
        swd_disconnect(); isp_leave(); uart_cancel(); jtag_disconnect(); pic_leave(); spi_close(); i2c_close(); connected = 0;
        r[1] = c2_init(&r[2]);
        return 3;
    case 0x81: /* Erase 512-byte flash page, 0..14. */
        if (len < 2) break;
        r[1] = c2_erase_page(q[1]); return 2;
    case 0x82: /* Write <=59 bytes to 16-bit flash address. */
        if (len < 5 || q[3] == 0 || q[3] > 59 || (uint16_t)q[3] + 4 > len) break;
        r[1] = c2_write_block(get16(q + 1), q[3], q + 4); return 2;
    case 0x83: /* Read <=60 bytes from 16-bit flash address. */
        if (len < 4 || q[3] == 0 || q[3] > 60) break;
        r[1] = c2_read_block(get16(q + 1), q[3], r + 2);
        return r[1] == C2_OK ? q[3] + 2 : 2;
    case 0x84:
        c2_reset_target(); r[1] = C2_OK; return 2;
    case 0x90: /* AVR ISP programming enable. */
        swd_disconnect(); c2_cancel(); uart_cancel(); jtag_disconnect(); pic_leave(); spi_close(); i2c_close(); connected = 0;
        r[1] = isp_enter(); return 2;
    case 0x91: /* Batch of 4-byte AVR serial programming instructions. */
        if (len < 2 || q[1] == 0 || q[1] > 15 || (uint16_t)q[1] * 4 + 2 > len) break;
        r[1] = isp_active() ? 0 : 1;
        if (r[1]) return 2;
        for (i = 0; i < q[1]; ++i) isp_transaction(q + 2 + i * 4, r + 2 + i * 4);
        return 2 + q[1] * 4;
    case 0x92:
        isp_leave(); r[1] = 0; return 2;
    case 0xa0: /* UART: 0=8N1, 1=UPDI 8E2, 2=8E1. */
        if (len < 6) break;
        swd_disconnect(); c2_cancel(); isp_leave(); jtag_disconnect(); pic_leave(); spi_close(); i2c_close(); connected = 0;
        r[1] = uart_config(q[1], get32(q + 2)); return 2;
    case 0xa1: /* UART write then read, with per-byte timeout in ~16 ms units. */
        if (len < 4 || q[1] > 60 || q[2] > 61 || (uint16_t)q[1] + 4 > len) break;
        r[1] = uart_exchange(q + 4, q[1], r + 3, q[2], q[3], &r[2]);
        return r[2] + 3;
    case 0xa2:
        if (len < 2) break;
        r[1] = uart_break(q[1]); return 2;
    case 0xa3:
        if (len < 3) break;
        r[1] = uart_control(q[1], q[2]); return 2;
    case 0xc0: /* PIC16F18313 LVP entry. */
        swd_disconnect(); c2_cancel(); isp_leave(); uart_cancel(); jtag_disconnect(); spi_close(); i2c_close(); connected = 0;
        pic_enter(); r[1] = 0; return 2;
    case 0xc1: /* 6-bit ICSP command, optional 16/24-bit payload or read. */
        if (len < 7 || q[1] > 0x3f || q[2] > 3) break;
        r[1] = pic_active() ? 0 : 1;
        value = 0;
        if (!r[1]) value = pic_command(q[1], q[2], get32(q + 3));
        r[2] = (uint8_t)value; r[3] = (uint8_t)(value >> 8);
        return 4;
    case 0xc2:
        pic_leave(); r[1] = 0; return 2;
    case 0xd0: /* Raw SPI mode 0..3; chip select remains asserted. */
        if (len < 2) break;
        swd_disconnect(); c2_cancel(); isp_leave(); uart_cancel(); jtag_disconnect(); pic_leave(); i2c_close(); connected = 0;
        r[1] = spi_open(q[1]); return 2;
    case 0xd1:
        if (len < 2 || !q[1] || q[1] > 60 || (uint16_t)q[1] + 2 > len) break;
        r[1] = spi_active() ? 0 : 1;
        if (r[1]) return 2;
        spi_exchange(q + 2, r + 2, q[1]); return q[1] + 2;
    case 0xd2:
        spi_close(); r[1] = 0; return 2;
    case 0xe0: /* I2C open-drain GPIO master. */
        swd_disconnect(); c2_cancel(); isp_leave(); uart_cancel(); jtag_disconnect(); pic_leave(); spi_close(); connected = 0;
        r[1] = i2c_open(); return 2;
    case 0xe1: /* 7-bit address, write bytes, repeated-start read bytes. */
        if (len < 4 || q[1] > 0x7f || q[2] > 60 || q[3] > 60 ||
            (uint16_t)q[2] + 4 > len) break;
        r[1] = i2c_transaction(q[1], q + 4, q[2], r + 2, q[3]);
        return r[1] ? 2 : q[3] + 2;
    case 0xe2:
        i2c_close(); r[1] = 0; return 2;
    default: r[0] = 0xff; return 1;
    }
    r[0] = 0xff;
    return 1;
}
