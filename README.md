# FX2LP multi-protocol MCU adapter

SDCC firmware for the Cypress/Infineon FX2LP (CY7C68013A) plus Linux host tools. The USB interface is a 64-byte CMSIS-DAP v1 HID device. Its SWD path is intended for OpenOCD and other CMSIS-DAP SWD clients. Additional HID commands provide target-specific programming and general serial buses.

**Status:** the firmware builds and the host protocol tests pass. The repository has not been tested with a physical FX2LP board or target MCU. Check signal timing and wiring on a logic analyzer before programming valuable devices.

## Build and load

Requirements: `sdcc`, `make`, Python 3, `libusb-1.0`, and Linux `hidraw` access. No Python packages are needed.

```sh
make check
python3 tools/load_fx2.py build/fx2_swd.ihx
```

The loader selects one unconfigured Cypress FX2LP (`04b4:8613`), holds CPUCS reset, writes the Intel HEX image, reads it back, and starts it. Use `--bus` and `--address` if more than one candidate is connected. It refuses a device that already has a USB product string unless `--force` is given. The image is RAM firmware and disappears on power loss. The same Cypress VID/PID is used for local development; choose a VID/PID you are entitled to use before distributing hardware.

The running firmware enumerates as **FX2LP CMSIS-DAP**. On Linux, the custom tools use its `/dev/hidraw*` node. Pass `--device /dev/hidrawN` to select it explicitly. Your account must have access to that node.

## Electrical interface

All pin names below are FX2LP **Port A** signals, not package pin numbers. Consult your board schematic for the connector mapping. Supply the target from a suitable external source and connect grounds. Match the target's logic level to FX2LP's 3.3 V interface with level translation where needed. Never connect a PIC VPP or UPDI 12 V pulse directly to FX2LP GPIO. The adapter has no target voltage sensing or power switching.

| Mode | PA0 | PA1 | PA2 | PA3 | PA4 | PA5 | PA6 | PA7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| SWD | SWCLK | SWDIO | nRESET | | | | | |
| C8051F300 C2 | C2CK/nRESET | C2D | | | | | | |
| ATmega328P ISP | SCK | MOSI | MISO | nRESET | | | | |
| UART 8N1 or 8E1 | | | | | TX | RX | BOOT | nRESET |
| UPDI 8E2 | | | | | UPDI | | | | |
| Raw JTAG | TCK | TMS | TDI | TDO | nTRST | nRESET | | |
| PIC16F18313 LVP | ICSPCLK | ICSPDAT | MCLR | | | | | |
| Raw SPI | SCK | MOSI | MISO | nCS | | | | |
| Raw I2C | SCL | SDA | | | | | | |

Use a pull-up on the target side of open-drain reset, UPDI, and I2C lines. UPDI requires a pull-up on PA4; I2C requires pull-ups on PA0 and PA1. UART PA6 is a push-pull BOOT output and PA7 is an open-drain reset output. Check each target's allowed supply and input voltage before connecting. Use one protocol mode at a time because the pins are shared.

## SWD and JTAG

For SWD, use a CMSIS-DAP v1 capable tool, for example:

```sh
openocd -f interface/cmsis-dap.cfg -c 'transport select swd' \
  -f target/stm32f1x.cfg -c 'adapter speed 100'
```

Choose the OpenOCD target file for the actual MCU. Firmware caps software SWD at 100 kHz. It implements the common CMSIS-DAP v1 SWD commands, including register transfer and block transfer. [CMSIS-DAP command reference](https://arm-software.github.io/CMSIS-DAP/latest/); [OpenOCD adapter reference](https://openocd.org/doc/html/Debug-Adapter-Configuration.html).

Raw JTAG clocking, TAP reset, and IDCODE reading are available through CMSIS-DAP `JTAG_Sequence` (`0x14`), `JTAG_Configure` (`0x15`, one-device chain), and `JTAG_IDCODE` (`0x16`):

```sh
python3 tools/jtag_scan.py --bits 128
```

This JTAG path is a raw scan interface. ARM JTAG-DP `DAP_Transfer` is not implemented, so it is not advertised as a full CMSIS-DAP JTAG debugging adapter. [CMSIS-DAP JTAG sequence](https://arm-software.github.io/CMSIS-DAP/latest/group__DAP__JTAG__Sequence.html).

## MCU programming tools

| Target | Command | Notes |
| --- | --- | --- |
| Silicon Labs C8051F300 | `python3 tools/c2_program.py app.hex` | C2 flash pages, readback verification; factory 0x1e00-0x1fff area is excluded. |
| ATmega328P | `python3 tools/avr_isp.py app.hex` | SPI ISP, signature check, chip erase, page write, verification. Chip erase can affect EEPROM according to EESAVE. |
| ATtiny1614 | `python3 tools/updi_program.py app.hex` | UPDI 8E2 at 9600 baud, NVM key, page erase/write, verification. `--erase` first performs a full chip erase and can affect EEPROM. |
| STM32 ROM UART | `python3 tools/stm32_uart.py app.hex --flash-size 0x10000 --reset-boot` | AN3155 at 9600 baud 8E1; `--flash-size` must match the part. `--reset-boot` assumes BOOT on PA6 and reset on PA7. Performs mass erase before writing. |
| PIC16F18313 | `python3 tools/pic16_icsp.py app.hex --flash-only` | Low-voltage ICSP, flash rows only, preserves untouched words. Requires LVP enabled. Explicit `--flash-only` skips config/EEPROM/ID records in a full compiler HEX file. Use `--part pic16lf18313` for the LF variant. |

Each programming tool has `--check` to parse its image without hardware and `--device` to select a hidraw node. C2, AVR, UPDI, STM32, and PIC tools provide `--verify-only`. PIC config words and EEPROM are currently outside the PIC tool's supported write area. High-voltage PIC ICSP needs a switched VPP generator and target power controls; these are not present in this hardware design. UPDI recovery when the UPDI pin fuse has been disabled likewise needs a separate 12 V pulse circuit. [PIC16F183XX programming specification](https://ww1.microchip.com/downloads/en/DeviceDoc/40001738D.pdf); [ATtiny1614 UPDI and NVM specification](https://ww1.microchip.com/downloads/en/DeviceDoc/ATtiny1614-16-17-DataSheet-DS40002204A.pdf); [STM32 AN3155](https://www.st.com/resource/en/application_note/an3155-how-to-use-usart-protocol-in-bootloader-on-stm32-mcus-stmicroelectronics.pdf).

## General serial buses

The UART bridge handles 8N1, STM32-style 8E1, and one-wire UPDI 8E2 at 1200–38400 baud. It is a HID byte bridge, not a USB CDC serial port. Its interactive terminal polls one byte at a time:

```sh
python3 tools/uart_bridge.py --baud 9600 --terminal
python3 tools/uart_bridge.py --baud 9600 --tx-hex '55 aa' --rx 4
```

Use Ctrl-C to leave the terminal. The GPIO UART has no background receive buffer, so data arriving while the host is transmitting or outside an active receive command may be lost. For continuous high-rate serial logging, use a dedicated UART adapter.

SPI provides modes 0–3 with chip select held across HID chunks. I2C provides 7-bit addressing, write, and repeated-start read:

```sh
python3 tools/spi_bridge.py --mode 0 --tx-hex 9f000000
python3 tools/i2c_bridge.py 0x50 --write-hex 0000 --read 16
```

The I2C/SPI commands are general access primitives for bootloaders, memories, and peripheral identification. They do not know target-specific erase/write rules.

## Protocol notes

The firmware uses one 64-byte HID OUT and IN report. Standard CMSIS-DAP commands are handled in `firmware/dap.c`; vendor extensions occupy `0x80`–`0xe2`:

| Commands | Function |
| --- | --- |
| `0x80`–`0x84` | C8051F300 C2 init, page erase, block write/read, reset |
| `0x90`–`0x92` | AVR ISP enter, 4-byte instruction batch, leave |
| `0xa0`–`0xa3` | UART configure, exchange, BREAK, BOOT/reset outputs |
| `0xc0`–`0xc2` | PIC low-voltage ICSP enter, 6-bit command, leave |
| `0xd0`–`0xd2` | SPI select, exchange, deselect |
| `0xe0`–`0xe2` | I2C init, transaction, release |

`make check` compiles the SDCC image, validates its Intel HEX bounds, and runs the host tests. The loader and programmers remain hardware unverified until tested on actual boards.
