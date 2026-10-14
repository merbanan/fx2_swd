#ifndef ISP_H
#define ISP_H
#include <stdint.h>

uint8_t isp_enter(void);
void isp_leave(void);
uint8_t isp_active(void);
void isp_transaction(const volatile __xdata uint8_t *command,
                     volatile __xdata uint8_t *response);

#endif
