#!/usr/bin/env python3
"""Program PIC16F18313 or PIC16LF18313 flash over low-voltage ICSP."""

import argparse
import time
from pathlib import Path

if __package__:
    from .load_fx2 import LoadError, parse_hex
    from .hid_transport import HidTransport, find_probe
else:
    from load_fx2 import LoadError, parse_hex
    from hid_transport import HidTransport, find_probe

FLASH_WORDS = 2048
ROW_WORDS = 32
IDS = {"pic16f18313": 0x3066, "pic16lf18313": 0x3068}


class PicProbe:
    def __init__(self, path):
        self.hid = HidTransport(path)

    def close(self):
        self.hid.exchange(0xc2)
        self.hid.close()

    def enter(self):
        if self.hid.exchange(0xc0)[1]:
            raise LoadError("PIC low-voltage ICSP entry failed")

    def command(self, opcode, mode=0, value=0):
        packet = bytes([opcode, mode]) + value.to_bytes(4, "little")
        reply = self.hid.exchange(0xc1, packet)
        if reply[1]:
            raise LoadError("PIC ICSP mode is not active")
        return int.from_bytes(reply[2:4], "little")

    def load_pc(self, address):
        self.command(0x1d, 2, address)

    def read_words(self, base, count):
        self.load_pc(base)
        return [self.command(0x24, 3) for _ in range(count)]

    def device_id(self):
        return self.read_words(0x8006, 1)[0]

    def write_row(self, base, words):
        if base % ROW_WORDS or len(words) != ROW_WORDS:
            raise ValueError("PIC row must contain 32 aligned words")
        self.load_pc(base)
        self.command(0x05)  # row erase
        time.sleep(.006)
        self.load_pc(base)
        for index, word in enumerate(words):
            self.command(0x22 if index < ROW_WORDS - 1 else 0x02, 1, word)
        self.command(0x08)  # internally timed row write
        time.sleep(.010)


def image_words(image):
    words = {}
    for address, value in image.items():
        if address >= FLASH_WORDS * 2:
            continue
        word = address // 2
        words.setdefault(word, {})[address & 1] = value
    return words


def program(probe, image, verify_only=False):
    requested = image_words(image)
    for base in sorted({word & ~(ROW_WORDS - 1) for word in requested}):
        actual = probe.read_words(base, ROW_WORDS)
        desired = actual[:]
        for address in range(base, base + ROW_WORDS):
            if address in requested:
                low = requested[address].get(0, desired[address - base] & 255)
                high = requested[address].get(1, desired[address - base] >> 8)
                desired[address - base] = low | (high << 8)
                if desired[address - base] > 0x3fff:
                    raise LoadError(f"PIC program word at 0x{address:04x} exceeds 14 bits")
        if actual != desired and not verify_only:
            probe.write_row(base, desired)
            actual = probe.read_words(base, ROW_WORDS)
            if actual != desired:
                raise LoadError(f"PIC row verification mismatch at 0x{base:04x}")
        for address in range(base, base + ROW_WORDS):
            if address in requested and actual[address - base] != desired[address - base]:
                raise LoadError(f"PIC verification mismatch at word 0x{address:04x}")
        print(f"verified PIC row 0x{base:04x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="PIC Intel HEX, two bytes per 14-bit word")
    parser.add_argument("--part", choices=IDS, default="pic16f18313")
    parser.add_argument("--device", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--flash-only", action="store_true",
                        help="allow and skip configuration, EEPROM, and ID records")
    args = parser.parse_args()
    try:
        image = parse_hex(args.image, 0x20000)
        other = sorted(address for address in image if address >= FLASH_WORDS * 2)
        if other and not args.flash_only:
            raise LoadError(f"image contains non-flash data at 0x{other[0]:x}; "
                            "use --flash-only to explicitly skip it")
        if not image_words(image):
            raise LoadError("image contains no PIC program flash words")
        if args.check:
            print(f"validated {len(image_words(image))} PIC flash words")
            return 0
        probe = PicProbe(args.device or find_probe())
        try:
            probe.enter()
            identity = probe.device_id()
            if identity != IDS[args.part]:
                raise LoadError(f"expected {args.part} ID 0x{IDS[args.part]:04x}, got 0x{identity:04x}")
            program(probe, image, args.verify_only)
        finally:
            probe.close()
    except (LoadError, OSError, ValueError) as exc:
        parser.exit(1, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
