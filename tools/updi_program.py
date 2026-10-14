#!/usr/bin/env python3
"""Program ATtiny1614 flash through the FX2LP UPDI one-wire port."""

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

FLASH_SIZE = 16384
PAGE_SIZE = 64
FLASH_BASE = 0x8000
ACK = 0x40


class UpdiProbe:
    def __init__(self, uart):
        self.uart = uart

    def check_ack(self, tx=b""):
        value = self.uart.exchange(tx, 1)[0]
        if value != ACK:
            raise LoadError(f"UPDI expected ACK 0x40, received 0x{value:02x}")

    def break_sync(self):
        self.uart.send_break(26)
        self.stcs(0x03, 0x08)  # disable collision detection for half-duplex link
        self.stcs(0x02, 0x80)  # insert inter-byte delay on target responses
        if self.ldcs(0x00) == 0:
            raise LoadError("UPDI did not respond after BREAK")

    def ldcs(self, address):
        return self.uart.exchange(bytes([0x55, 0x80 | address]), 1)[0]

    def stcs(self, address, value):
        self.uart.write(bytes([0x55, 0xc0 | address, value]))

    def lds(self, address):
        return self.uart.exchange(bytes([0x55, 0x04, address & 255, address >> 8]), 1)[0]

    def sts(self, address, value):
        self.check_ack(bytes([0x55, 0x44, address & 255, address >> 8]))
        self.check_ack(bytes([value]))

    def sts16(self, address, value):
        self.check_ack(bytes([0x55, 0x45, address & 255, address >> 8]))
        self.check_ack(value.to_bytes(2, "little"))

    def key(self, signature):
        self.uart.write(b"\x55\xe0" + signature.to_bytes(8, "little"))

    def reset(self):
        self.stcs(0x08, 0x59)
        self.stcs(0x08, 0x00)

    def wait_status(self, mask, expected, timeout=2.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = self.ldcs(0x0b)
            if value & mask == expected:
                return value
        raise LoadError(f"UPDI status timeout (mask 0x{mask:02x}, expected 0x{expected:02x})")

    def chip_erase(self):
        self.key(0x4e564d4572617365)
        if not self.ldcs(0x07) & 0x08:
            raise LoadError("UPDI chip erase key was not accepted")
        self.reset()
        self.wait_status(0x01, 0, 5.0)

    def enter_programming(self):
        self.key(0x4e564d50726f6720)
        if not self.ldcs(0x07) & 0x10:
            raise LoadError("UPDI NVM programming key was not accepted")
        self.reset()
        self.wait_status(0x08, 0x08)

    def wait_nvm(self):
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            status = self.lds(0x1002)
            if status & 4:
                raise LoadError("ATtiny1614 NVM write error")
            if not status & 3:
                return
        raise LoadError("ATtiny1614 NVM busy timeout")

    def set_ptr(self, address):
        self.check_ack(bytes([0x55, 0x69, address & 255, address >> 8]))

    def read(self, address, count):
        if not 1 <= count <= 60:
            raise ValueError("UPDI read count must be 1..60")
        self.set_ptr(address)
        # REPEAT N runs the next indirect load N+1 times.
        return self.uart.exchange(bytes([0x55, 0xa0, count - 1, 0x55, 0x24]), count)

    def write_page(self, base, data):
        if base % PAGE_SIZE or len(data) != PAGE_SIZE:
            raise ValueError("ATtiny1614 page must be aligned and 64 bytes")
        self.wait_nvm()
        self.sts(0x1000, 4)  # clear page buffer
        self.wait_nvm()
        for offset in range(0, PAGE_SIZE, 2):
            self.sts16(FLASH_BASE + base + offset,
                       int.from_bytes(data[offset:offset + 2], "little"))
        self.sts(0x1000, 3)  # erase and write page
        self.wait_nvm()


def program(probe, image, verify_only=False, erase=False):
    if erase and verify_only:
        raise ValueError("--erase and --verify-only are mutually exclusive")
    if erase:
        probe.chip_erase()
    probe.enter_programming()
    pages = sorted({address // PAGE_SIZE * PAGE_SIZE for address in image})
    for base in pages:
        actual = bytearray()
        for offset in (0, 60):
            actual.extend(probe.read(FLASH_BASE + base + offset, min(60, PAGE_SIZE - offset)))
        expected = bytearray(actual if not erase else bytes([0xff] * PAGE_SIZE))
        for offset in range(PAGE_SIZE):
            if base + offset in image:
                expected[offset] = image[base + offset]
        if not verify_only and actual != expected:
            probe.write_page(base, expected)
            actual = bytearray()
            for offset in (0, 60):
                actual.extend(probe.read(FLASH_BASE + base + offset, min(60, PAGE_SIZE - offset)))
        for offset, value in enumerate(actual):
            if (not verify_only or base + offset in image) and value != expected[offset]:
                raise LoadError(f"ATtiny1614 verification mismatch at 0x{base + offset:04x}")
        print(f"verified flash page 0x{base:04x}")
    probe.reset()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="ATtiny1614 flash Intel HEX image")
    parser.add_argument("--device", type=Path)
    parser.add_argument("--baud", type=int, default=9600)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--erase", action="store_true", help="erase entire chip first (can erase EEPROM)")
    args = parser.parse_args()
    try:
        image = parse_hex(args.image, FLASH_SIZE)
        if args.check:
            print(f"validated {len(image)} ATtiny1614 flash bytes")
            return 0
        uart = UartProbe(args.device or find_probe())
        try:
            uart.configure(args.baud, "updi")
            probe = UpdiProbe(uart)
            probe.break_sync()
            program(probe, image, args.verify_only, args.erase)
        finally:
            uart.close()
    except (LoadError, OSError, ValueError) as exc:
        parser.exit(1, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
