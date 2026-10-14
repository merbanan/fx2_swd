#ifndef SWD_H
#define SWD_H
#include <stdint.h>

void swd_init(void);
void swd_connect(void);
void swd_disconnect(void);
void swd_set_clock(uint32_t hz);
void swd_set_config(uint8_t cfg);
void swd_sequence(uint8_t bits, const volatile __xdata uint8_t *data);
uint8_t swd_pins(uint8_t output, uint8_t select);
uint8_t swd_transfer(uint8_t request, uint32_t *data);
void swd_idle(uint8_t clocks);

#endif
