#ifndef SPI_H
#define SPI_H
#include <stdint.h>
uint8_t spi_open(uint8_t mode);
void spi_close(void);
uint8_t spi_active(void);
void spi_exchange(const volatile __xdata uint8_t *tx,
                  volatile __xdata uint8_t *rx, uint8_t count);
#endif
