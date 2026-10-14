#ifndef PIC_H
#define PIC_H

#include <stdint.h>

void pic_enter(void);
void pic_leave(void);
uint8_t pic_active(void);
uint16_t pic_command(uint8_t command, uint8_t mode, uint32_t value);

#endif
