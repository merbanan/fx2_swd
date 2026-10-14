#ifndef C2_H
#define C2_H
#include <stdint.h>

enum {
    C2_OK = 0, C2_FRAME_TIMEOUT = 1, C2_PI_TIMEOUT = 2,
    C2_PI_ERROR = 3, C2_WRONG_DEVICE = 4, C2_BAD_ARGUMENT = 5,
    C2_NOT_READY = 6
};

uint8_t c2_init(uint8_t *revision);
uint8_t c2_erase_page(uint8_t page);
uint8_t c2_read_block(uint16_t address, uint8_t count, volatile __xdata uint8_t *out);
uint8_t c2_write_block(uint16_t address, uint8_t count, const volatile __xdata uint8_t *in);
void c2_reset_target(void);
void c2_cancel(void);

#endif
