#ifndef DAP_H
#define DAP_H
#include <stdint.h>
uint8_t dap_process(const volatile __xdata uint8_t *request,
                    volatile __xdata uint8_t *response, uint8_t length);
#endif
