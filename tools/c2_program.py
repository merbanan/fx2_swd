#!/usr/bin/env python3
"""Program C8051F300 flash through the FX2LP C2 vendor commands (Linux hidraw)."""

import argparse
from pathlib import Path

if __package__:
    from .load_fx2 import LoadError, parse_hex
    from .hid_transport import HidTransport, find_probe
else:
    from load_fx2 import LoadError, parse_hex
    from hid_transport import HidTransport, find_probe

PAGE_SIZE = 512
FLASH_LIMIT = 0x1e00  # 0x1e00..0x1fff are factory reserved
LOCK_BYTE = 0x1dff
C2_ERRORS = {
    1: "C2 frame timed out", 2: "C2 programming interface timed out",
    3: "C2 programming interface rejected command", 4: "target is not a C8051F30x",
    5: "invalid address or length", 6: "C2 programming mode is not initialized",
}


class C2Probe:
    def __init__(self, path):
        self.hid = HidTransport(path)

    def close(self):
        self.hid.close()

    def command(self, command, args=b""):
        response = self.hid.exchange(command, args)
        code = response[1]
        if code:
            raise LoadError(C2_ERRORS.get(code, f"C2 error {code}"))
        return response[2:]

    def init(self):
        revision = self.command(0x80)[0]
        return revision

    def erase_page(self, page):
        self.command(0x81, bytes([page]))

    def write(self, address, data):
        if not 1 <= len(data) <= 59:
            raise ValueError("C2 write size must be 1..59")
        self.command(0x82, address.to_bytes(2, "little") + bytes([len(data)]) + data)

    def read(self, address, count):
        if not 1 <= count <= 60:
            raise ValueError("C2 read size must be 1..60")
        return self.command(0x83, address.to_bytes(2, "little") + bytes([count]))[:count]

    def reset(self):
        self.command(0x84)


def read_region(probe, address, count):
    result = bytearray()
    while len(result) < count:
        n = min(60, count - len(result))
        result.extend(probe.read(address + len(result), n))
    return bytes(result)


def program_pages(probe, image, verify_only=False):
    pages = sorted({address // PAGE_SIZE for address in image})
    for page in pages:
        start = page * PAGE_SIZE
        previous = read_region(probe, start, PAGE_SIZE)
        desired = bytearray(previous)
        for address, value in image.items():
            if start <= address < start + PAGE_SIZE:
                desired[address - start] = value
        if verify_only:
            mismatches = [address for address in image
                          if start <= address < start + PAGE_SIZE and previous[address - start] != image[address]]
            if mismatches:
                raise LoadError(f"verification mismatch at 0x{min(mismatches):04x}")
            print(f"verified page {page}")
            continue
        if previous == desired:
            print(f"page {page} already matches")
            continue
        probe.erase_page(page)
        offset = 0
        while offset < PAGE_SIZE:
            while offset < PAGE_SIZE and desired[offset] == 0xff:
                offset += 1
            if offset >= PAGE_SIZE:
                break
            end = offset
            while end < PAGE_SIZE and desired[end] != 0xff and end - offset < 59:
                end += 1
            probe.write(start + offset, bytes(desired[offset:end]))
            offset = end
        actual = read_region(probe, start, PAGE_SIZE)
        if actual != desired:
            difference = next(i for i in range(PAGE_SIZE) if actual[i] != desired[i])
            raise LoadError(f"verification mismatch at 0x{start + difference:04x}")
        print(f"programmed and verified page {page}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="Intel HEX image for C8051F300")
    parser.add_argument("--device", type=Path, help="hidraw path; detected automatically if omitted")
    parser.add_argument("--check", action="store_true", help="validate image without touching target")
    parser.add_argument("--verify-only", action="store_true", help="compare flash with the image")
    parser.add_argument("--allow-lock-byte", action="store_true", help="permit programming flash lock byte at 0x1dff")
    args = parser.parse_args()
    try:
        image = parse_hex(args.image)
        if max(image) >= FLASH_LIMIT:
            raise LoadError("image enters factory-reserved C8051F300 flash at 0x1e00")
        if LOCK_BYTE in image and not args.allow_lock_byte:
            raise LoadError("image contains the flash lock byte; use --allow-lock-byte if intended")
        print(f"validated {len(image)} target bytes across {len({x // PAGE_SIZE for x in image})} pages")
        if args.check:
            return 0
        path = args.device or find_probe()
        probe = C2Probe(path)
        try:
            revision = probe.init()
            print(f"C8051F30x revision 0x{revision:02x} on {path}")
            program_pages(probe, image, args.verify_only)
            if not args.verify_only:
                probe.reset()
        finally:
            probe.close()
    except (LoadError, OSError) as exc:
        parser.exit(1, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
