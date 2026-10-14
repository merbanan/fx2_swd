#!/usr/bin/env python3
"""Program an ATmega328P with the FX2LP AVR ISP bridge (Linux hidraw)."""

import argparse
import time
from pathlib import Path

if __package__:
    from .load_fx2 import LoadError, parse_hex
    from .hid_transport import HidTransport, find_probe
else:
    from load_fx2 import LoadError, parse_hex
    from hid_transport import HidTransport, find_probe

FLASH_SIZE = 32768
PAGE_SIZE = 128
SIGNATURE = bytes.fromhex("1e950f")


class AvrProbe:
    def __init__(self, path):
        self.hid = HidTransport(path)

    def close(self):
        self.hid.close()

    def enter(self):
        if self.hid.exchange(0x90)[1]:
            raise LoadError("AVR did not echo 0x53; check reset, clock, and wiring")

    def transfer_many(self, commands):
        if not 1 <= len(commands) <= 15 or any(len(x) != 4 for x in commands):
            raise ValueError("expected 1..15 four-byte SPI instructions")
        result = self.hid.exchange(0x91, bytes([len(commands)]) + b"".join(commands))
        if result[1]:
            raise LoadError("AVR ISP mode is not active")
        return [result[2 + 4 * i:6 + 4 * i] for i in range(len(commands))]

    def transfer(self, command):
        return self.transfer_many([command])[0]

    def leave(self):
        self.hid.exchange(0x92)

    def signature(self):
        return bytes(self.transfer(bytes([0x30, 0, i, 0]))[3] for i in range(3))

    def wait_ready(self):
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            if not (self.transfer(b"\xf0\x00\x00\x00")[3] & 1):
                return
        raise LoadError("AVR busy timeout")

    def erase(self):
        self.transfer(b"\xac\x80\x00\x00")
        self.wait_ready()

    def read_flash(self, address, count):
        commands = []
        for byte_address in range(address, address + count):
            word = byte_address >> 1
            commands.append(bytes([0x28 if byte_address & 1 else 0x20,
                                   (word >> 8) & 0xff, word & 0xff, 0]))
        data = bytearray()
        for offset in range(0, len(commands), 15):
            data.extend(reply[3] for reply in self.transfer_many(commands[offset:offset + 15]))
        return bytes(data)

    def write_page(self, base, data):
        if base % PAGE_SIZE or len(data) != PAGE_SIZE:
            raise ValueError("page must be 128 bytes and aligned")
        commands = []
        for offset in range(0, PAGE_SIZE, 2):
            if data[offset] == data[offset + 1] == 0xff:
                continue
            word_offset = offset >> 1
            commands.append(bytes([0x40, 0, word_offset, data[offset]]))
            commands.append(bytes([0x48, 0, word_offset, data[offset + 1]]))
        for offset in range(0, len(commands), 15):
            self.transfer_many(commands[offset:offset + 15])
        word = base >> 1
        self.transfer(bytes([0x4c, word >> 8, word & 0xff, 0]))
        self.wait_ready()


def program(probe, image, verify_only=False):
    if not verify_only:
        probe.erase()
        for base in range(0, FLASH_SIZE, PAGE_SIZE):
            if not any(base <= address < base + PAGE_SIZE and value != 0xff
                       for address, value in image.items()):
                continue
            page = bytes(image.get(base + i, 0xff) for i in range(PAGE_SIZE))
            probe.write_page(base, page)
            if probe.read_flash(base, PAGE_SIZE) != page:
                raise LoadError(f"verification failed on page at 0x{base:04x}")
            print(f"programmed and verified 0x{base:04x}..0x{base + PAGE_SIZE - 1:04x}")
    for base in sorted({address & ~(PAGE_SIZE - 1) for address in image}):
        actual = probe.read_flash(base, PAGE_SIZE)
        for address, value in image.items():
            if base <= address < base + PAGE_SIZE and actual[address - base] != value:
                raise LoadError(f"verification mismatch at 0x{address:04x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="ATmega328P Intel HEX image")
    parser.add_argument("--device", type=Path, help="hidraw path; detected if omitted")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    try:
        image = parse_hex(args.image, FLASH_SIZE)
        print(f"validated {len(image)} ATmega328P flash bytes")
        if args.check:
            return 0
        probe = AvrProbe(args.device or find_probe())
        try:
            probe.enter()
            signature = probe.signature()
            if signature != SIGNATURE:
                raise LoadError(f"expected ATmega328P signature {SIGNATURE.hex()}, got {signature.hex()}")
            program(probe, image, args.verify_only)
            print("AVR flash verified")
        finally:
            probe.leave()
            probe.close()
    except (LoadError, OSError) as exc:
        parser.exit(1, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
