#!/usr/bin/env python3
"""Program an STM32 through its AN3155 ROM USART bootloader."""

import argparse
import time
from pathlib import Path

if __package__:
    from .load_fx2 import LoadError, parse_hex
    from .hid_transport import find_probe
    from .uart_bridge import UartProbe
else:
    from load_fx2 import LoadError, parse_hex
    from hid_transport import find_probe
    from uart_bridge import UartProbe

ACK = 0x79
FLASH_BASE = 0x08000000


def xor(data):
    result = 0
    for byte in data:
        result ^= byte
    return result


class Stm32Bootloader:
    def __init__(self, uart):
        self.uart = uart
        self.commands = set()

    def ack(self, timeout_ms=1000):
        value = self.uart.read(1, timeout_ms)[0]
        if value != ACK:
            raise LoadError(f"STM32 expected ACK 0x79, received 0x{value:02x}")

    def command(self, number):
        if self.commands and number not in self.commands:
            raise LoadError(f"STM32 bootloader does not advertise command 0x{number:02x}")
        self.uart.write(bytes([number, number ^ 0xff]))
        self.ack()

    def connect(self):
        self.uart.write(b"\x7f")
        self.ack()
        self.command(0x00)
        n = self.uart.read(1)[0]
        data = self.uart.read(n + 1)
        self.ack()
        self.commands = set(data[1:])
        self.command(0x02)
        n = self.uart.read(1)[0]
        identity = self.uart.read(n + 1)
        self.ack()
        return identity.hex(), data[0]

    def address(self, address):
        raw = address.to_bytes(4, "big")
        self.uart.write(raw + bytes([xor(raw)]))
        self.ack()

    def erase(self):
        if 0x44 in self.commands:
            self.command(0x44)
            self.uart.write(b"\xff\xff\x00")
        elif 0x43 in self.commands:
            self.command(0x43)
            self.uart.write(b"\xff\x00")
        else:
            raise LoadError("STM32 bootloader does not advertise mass erase")
        self.ack(4000)

    def write(self, address, data):
        if not data or len(data) > 60 or len(data) % 4 or address % 4:
            raise ValueError("STM32 writes require 4-byte alignment and 4..60 bytes")
        self.command(0x31)
        self.address(address)
        packet = bytes([len(data) - 1]) + data
        self.uart.write(packet + bytes([xor(packet)]))
        self.ack(4000)

    def read(self, address, count):
        if not 1 <= count <= 60:
            raise ValueError("STM32 read length must be 1..60")
        self.command(0x11)
        self.address(address)
        self.uart.write(bytes([count - 1, (count - 1) ^ 0xff]))
        self.ack()
        return self.uart.read(count)


def program(boot, image, verify_only=False):
    if not image:
        raise LoadError("empty image")
    blocks = sorted({address // 60 * 60 for address in image})
    if not verify_only:
        boot.erase()
        for offset in blocks:
            last = max(address for address in image if offset <= address < offset + 60)
            size = (last - offset + 4) & ~3
            data = bytes(image.get(offset + i, 0xff) for i in range(size))
            if any(x != 0xff for x in data):
                boot.write(FLASH_BASE + offset, data)
    for offset in blocks:
        last = max(address for address in image if offset <= address < offset + 60)
        size = last - offset + 1
        actual = boot.read(FLASH_BASE + offset, size)
        for i, value in enumerate(actual):
            if offset + i in image and image[offset + i] != value:
                raise LoadError(f"STM32 verification mismatch at 0x{FLASH_BASE + offset + i:08x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="Intel HEX with offsets from 0x08000000")
    parser.add_argument("--device", type=Path)
    parser.add_argument("--baud", type=int, default=9600)
    parser.add_argument("--flash-size", type=lambda x: int(x, 0), required=True,
                        help="target flash bytes, from its datasheet")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--reset-boot", action="store_true",
                        help="drive PA6 high and pulse PA7 low before sync")
    args = parser.parse_args()
    try:
        raw = parse_hex(args.image, FLASH_BASE + args.flash_size)
        if any(address < FLASH_BASE for address in raw):
            raise LoadError("STM32 image must use absolute flash addresses from 0x08000000")
        image = {address - FLASH_BASE: value for address, value in raw.items()}
        if args.check:
            print(f"validated {len(image)} STM32 flash bytes")
            return 0
        uart = UartProbe(args.device or find_probe())
        try:
            uart.configure(args.baud, "8e1")
            if args.reset_boot:
                uart.control(boot=True, reset=False)
                time.sleep(.02)
                uart.control(reset=True)
                time.sleep(.05)
            boot = Stm32Bootloader(uart)
            identity, version = boot.connect()
            print(f"STM32 ROM bootloader {version >> 4}.{version & 15}, ID {identity}")
            program(boot, image, args.verify_only)
            print("STM32 flash verified")
        finally:
            uart.close()
    except (LoadError, OSError, ValueError) as exc:
        parser.exit(1, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
