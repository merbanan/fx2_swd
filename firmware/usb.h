#ifndef USB_H
#define USB_H
#include <stdint.h>
void usb_init(void);
void usb_poll(void);
uint8_t usb_configured(void);
#endif
