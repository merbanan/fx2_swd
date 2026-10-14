#ifndef FX2_H
#define FX2_H

#include <stdint.h>

/* FX2LP register and buffer addresses, from the EZ-USB FX2 Technical Reference Manual. */
#define REG8(addr) (*((volatile __xdata uint8_t *)(addr)))
#define CPUCS       REG8(0xE600)
#define EP1OUTCFG   REG8(0xE610)
#define EP1INCFG    REG8(0xE611)
#define EP2CFG      REG8(0xE612)
#define EP4CFG      REG8(0xE613)
#define EP6CFG      REG8(0xE614)
#define EP8CFG      REG8(0xE615)
#define USBIRQ      REG8(0xE65D)
#define PORTACFG    REG8(0xE670)
#define USBCS       REG8(0xE680)
#define TOGCTL      REG8(0xE683)
#define EP0BCH      REG8(0xE68A)
#define EP0BCL      REG8(0xE68B)
#define EP1OUTBC    REG8(0xE68D)
#define EP1INBC     REG8(0xE68F)
#define EP0CS       REG8(0xE6A0)
#define EP1OUTCS    REG8(0xE6A1)
#define EP1INCS     REG8(0xE6A2)
#define SETUPDAT    ((volatile __xdata uint8_t *)0xE6B8)
#define EP0BUF      ((volatile __xdata uint8_t *)0xE740)
#define EP1OUTBUF   ((volatile __xdata uint8_t *)0xE780)
#define EP1INBUF    ((volatile __xdata uint8_t *)0xE7C0)

__sfr __at(0x80) IOA;
__sfr __at(0xB2) OEA;
__sfr __at(0x88) TCON;
__sfr __at(0x89) TMOD;
__sfr __at(0x8A) TL0;
__sfr __at(0x8C) TH0;
__sfr __at(0x8E) CKCON;

/* Register writes to the FX2 register block require a synchronization delay. */
#define SYNCDELAY() __asm nop; nop; nop; nop; __endasm

#endif
