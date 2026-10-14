#include "fx2.h"
#include "usb.h"

static uint8_t configuration;
static uint8_t idle_rate;
static uint8_t protocol = 1;

/* One 64-byte input report and one 64-byte output report, without report IDs. */
static const uint8_t __code hid_report[] = {
    0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,
    0x95, 0x40, 0x09, 0x01, 0x81, 0x02,
    0x95, 0x40, 0x09, 0x01, 0x91, 0x02, 0xc0
};

/* Lab VID/PID mirrors the unprogrammed Cypress boot device; see README. */
static const uint8_t __code device_desc[] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 64,
    0xb4, 0x04, 0x13, 0x86, 0x00, 0x01, 1, 2, 0, 1
};
static const uint8_t __code qualifier_desc[] = {
    10, 6, 0x00, 0x02, 0, 0, 0, 64, 1, 0
};
static const uint8_t __code config_desc[] = {
    9, 2, 41, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 3, 0, 0, 2,
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof(hid_report), 0,
    7, 5, 0x01, 3, 64, 0, 1,
    7, 5, 0x81, 3, 64, 0, 1
};
static const uint8_t __code other_config_desc[] = {
    9, 7, 41, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 3, 0, 0, 2,
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof(hid_report), 0,
    7, 5, 0x01, 3, 64, 0, 1,
    7, 5, 0x81, 3, 64, 0, 1
};
static const uint8_t __code language_string[] = {4, 3, 9, 4};
static const uint8_t __code vendor_string[] = {
    28, 3, 'F',0,'X',0,'2',0,'L',0,'P',0,' ',0,
    'p',0,'r',0,'o',0,'j',0,'e',0,'c',0,'t',0
};
static const uint8_t __code product_string[] = {
    32, 3, 'F',0,'X',0,'2',0,'L',0,'P',0,' ',0,
    'C',0,'M',0,'S',0,'I',0,'S',0,'-',0,'D',0,'A',0,'P',0
};

static void send_data(const uint8_t __code *source, uint8_t size)
{
    uint8_t i;
    uint16_t requested = (uint16_t)SETUPDAT[6] | ((uint16_t)SETUPDAT[7] << 8);
    if (requested < size) size = (uint8_t)requested;
    for (i = 0; i < size; ++i) EP0BUF[i] = source[i];
    EP0BCH = 0;
    SYNCDELAY();
    EP0BCL = size;
    SYNCDELAY();
    EP0CS = 0x80; /* clear HSNAK to complete the control transfer */
}

static void send_byte(uint8_t value)
{
    EP0BUF[0] = value;
    EP0BCH = 0;
    SYNCDELAY();
    EP0BCL = SETUPDAT[6] ? 1 : 0;
    SYNCDELAY();
    EP0CS = 0x80;
}

static void stall(void) { EP0CS = 1; }
static void ack(void) { EP0CS = 0x80; }

static void setup(void)
{
    uint8_t type = SETUPDAT[0], request = SETUPDAT[1];
    uint8_t value = SETUPDAT[2], descriptor = SETUPDAT[3];
    if ((type == 0x80 || type == 0x81) && request == 6) {
        switch (descriptor) {
        case 1: if (value == 0) { send_data(device_desc, sizeof(device_desc)); return; } break;
        case 2: if (value == 0) { send_data(config_desc, sizeof(config_desc)); return; } break;
        case 3:
            if (value == 0) { send_data(language_string, sizeof(language_string)); return; }
            if (value == 1) { send_data(vendor_string, sizeof(vendor_string)); return; }
            if (value == 2) { send_data(product_string, sizeof(product_string)); return; }
            break;
        case 6: if (value == 0) { send_data(qualifier_desc, sizeof(qualifier_desc)); return; } break;
        case 7: if (value == 0) { send_data(other_config_desc, sizeof(other_config_desc)); return; } break;
        case 0x21:
            if (type == 0x81) { send_data(config_desc + 18, 9); return; }
            break;
        case 0x22:
            if (type == 0x81) { send_data(hid_report, sizeof(hid_report)); return; }
            break;
        }
    }
    if (type == 0 && request == 9 && SETUPDAT[3] == 0 && value <= 1) {
        configuration = value;
        if (configuration) { EP1OUTBC = 0; SYNCDELAY(); }
        ack(); return;
    }
    if (type == 0x80 && request == 8) { send_byte(configuration); return; }
    if (type == 0x81 && request == 10) { send_byte(0); return; }
    if (type == 1 && request == 11 && value == 0) { ack(); return; }
    if ((type == 0x80 || type == 0x81 || type == 0x82) && request == 0) {
        EP0BUF[0] = 0; EP0BUF[1] = 0;
        EP0BCH = 0; SYNCDELAY(); EP0BCL = 2; SYNCDELAY(); ack(); return;
    }
    if (type == 0x21 && request == 10) { idle_rate = SETUPDAT[3]; ack(); return; }
    if (type == 0xa1 && request == 2) { send_byte(idle_rate); return; }
    if (type == 0x21 && request == 11) { protocol = value; ack(); return; }
    if (type == 0xa1 && request == 3) { send_byte(protocol); return; }
    stall();
}

void usb_init(void)
{
    volatile uint16_t wait;
    CPUCS = 0x10; /* 48 MHz 8051 clock */
    SYNCDELAY();
    USBCS = 0x0a; /* disconnect while changing descriptors */
    SYNCDELAY();
    EP1OUTCFG = 0xb0;
    SYNCDELAY();
    EP1INCFG = 0xb0;
    SYNCDELAY();
    EP2CFG = 0; SYNCDELAY();
    EP4CFG = 0; SYNCDELAY();
    EP6CFG = 0; SYNCDELAY();
    EP8CFG = 0; SYNCDELAY();
    EP0BCH = 0;
    USBIRQ = 0x11; /* clear SUDAV and USB reset */
    for (wait = 0; wait < 30000; ++wait) __asm nop; __endasm;
    USBCS = 0x02; /* firmware handles EP0, reconnect */
}

void usb_poll(void)
{
    uint8_t irq = USBIRQ;
    if (irq & 0x10) { configuration = 0; USBIRQ = 0x10; }
    if (irq & 1) {
        USBIRQ = 1;
        setup();
    }
}

uint8_t usb_configured(void) { return configuration; }
