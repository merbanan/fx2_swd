#ifndef JTAG_H
#define JTAG_H

#include <stdint.h>

void jtag_connect(void);
void jtag_disconnect(void);
uint8_t jtag_active(void);
void jtag_shift(uint8_t bits, uint8_t tms,
                const volatile __xdata uint8_t *tdi,
                volatile __xdata uint8_t *tdo);
uint8_t jtag_step(uint8_t tms, uint8_t tdi);
uint8_t jtag_pins(uint8_t output, uint8_t select);
void jtag_reset(void);

#endif
