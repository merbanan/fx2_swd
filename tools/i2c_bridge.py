#!/usr/bin/env python3
"""Access 7-bit I2C devices over FX2LP open-drain GPIO."""

import argparse
from pathlib import Path

if __package__:
    from .load_fx2 import LoadError
    from .hid_transport import HidTransport, find_probe
else:
    from load_fx2 import LoadError
    from hid_transport import HidTransport, find_probe


class I2cProbe:
    def __init__(self, path):
        self.hid = HidTransport(path)

    def close(self):
        self.hid.exchange(0xe2)
        self.hid.close()

    def open(self):
        if self.hid.exchange(0xe0)[1]:
            raise LoadError("I2C lines did not rise; check external pullups")

    def transaction(self, address, tx=b"", rx_count=0):
        if not 0 <= address <= 0x7f or len(tx) > 60 or not 0 <= rx_count <= 60:
            raise ValueError("I2C supports 7-bit addresses and up to 60 bytes each way")
        r = self.hid.exchange(0xe1, bytes([address, len(tx), rx_count]) + tx)
        if r[1]:
            raise LoadError({1: "I2C NACK", 2: "I2C clock stretch timeout",
                             3: "I2C mode is not active"}.get(r[1], "I2C error"))
        return bytes(r[2:2 + rx_count])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", type=lambda x: int(x, 0), help="7-bit target address")
    parser.add_argument("--device", type=Path)
    parser.add_argument("--write-hex", default="", help="bytes before optional repeated-start read")
    parser.add_argument("--read", type=int, default=0, help="bytes to read")
    args = parser.parse_args()
    try:
        tx = bytes.fromhex(args.write_hex)
        probe = I2cProbe(args.device or find_probe())
        try:
            probe.open()
            print(probe.transaction(args.address, tx, args.read).hex(" "))
        finally:
            probe.close()
    except (LoadError, OSError, ValueError) as exc:
        parser.exit(1, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
