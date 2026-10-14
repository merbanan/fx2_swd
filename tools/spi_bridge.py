#!/usr/bin/env python3
"""Exchange bytes with a target over the FX2LP SPI master pins."""

import argparse
from pathlib import Path

if __package__:
    from .load_fx2 import LoadError
    from .hid_transport import HidTransport, find_probe
else:
    from load_fx2 import LoadError
    from hid_transport import HidTransport, find_probe


class SpiProbe:
    def __init__(self, path):
        self.hid = HidTransport(path)

    def close(self):
        self.hid.exchange(0xd2)
        self.hid.close()

    def open(self, mode=0):
        if mode not in range(4):
            raise ValueError("SPI mode must be 0..3")
        if self.hid.exchange(0xd0, bytes([mode]))[1]:
            raise LoadError("SPI setup failed")

    def exchange(self, data):
        response = bytearray()
        for offset in range(0, len(data), 60):
            part = data[offset:offset + 60]
            r = self.hid.exchange(0xd1, bytes([len(part)]) + part)
            if r[1]:
                raise LoadError("SPI session is not active")
            response.extend(r[2:2 + len(part)])
        return bytes(response)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", type=Path)
    parser.add_argument("--mode", type=int, choices=range(4), default=0)
    parser.add_argument("--tx-hex", required=True, help="hex bytes, e.g. 9f000000 for JEDEC ID")
    args = parser.parse_args()
    try:
        data = bytes.fromhex(args.tx_hex)
        if not data:
            raise ValueError("transmit data is empty")
        probe = SpiProbe(args.device or find_probe())
        try:
            probe.open(args.mode)
            print(probe.exchange(data).hex(" "))
        finally:
            probe.close()
    except (LoadError, OSError, ValueError) as exc:
        parser.exit(1, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
