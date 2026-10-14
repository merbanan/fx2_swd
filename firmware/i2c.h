#ifndef I2C_H
#define I2C_H
#include <stdint.h>
uint8_t i2c_open(void);
void i2c_close(void);
uint8_t i2c_transaction(uint8_t address,
                        const volatile __xdata uint8_t *tx, uint8_t tx_count,
                        volatile __xdata uint8_t *rx, uint8_t rx_count);
#endif
