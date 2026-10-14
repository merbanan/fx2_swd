#ifndef UART_H
#define UART_H
#include <stdint.h>

uint8_t uart_config(uint8_t mode, uint32_t baud);
void uart_cancel(void);
uint8_t uart_exchange(const volatile __xdata uint8_t *tx, uint8_t tx_count,
                      volatile __xdata uint8_t *rx, uint8_t rx_count,
                      uint8_t timeout_units, uint8_t *received);
uint8_t uart_control(uint8_t output, uint8_t mask);
uint8_t uart_break(uint8_t duration_units);

#endif
